"""Invented files/streams only: resumable staging and whole-transfer deadlines."""
import io
import os
from pathlib import Path
import sys
import tempfile
import threading
import time
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/installer'))
import graphics_profile as graphics
import reshade_runtime as runtime


class ReShadeRecoveryTests(unittest.TestCase):
    def setUp(self):
        self.temp=tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT'))
        self.addCleanup(self.temp.cleanup);self.base=Path(self.temp.name)
        self.folder=self.base/'profile-1-0123456789ab'
        self.files={'ReShade32.dll':b'fixture dll','ReShade32.json':b'fixture manifest',
                    'Shaders/a.fx':b'shader','Textures/a.png':b'texture','ReShade.ini':b'config'}

    def test_restart_after_every_atomic_publication_and_directory_creation(self):
        for event in range(1,11):
            with self.subTest(event=event):
                folder=self.base/str(event)/self.folder.name;counter=[0]
                create=graphics.atomic_create;mkdir=Path.mkdir
                def interrupt():
                    counter[0]+=1
                    if counter[0]==event:raise SystemExit('power loss')
                def write(path,data,staging):
                    create(path,data,staging);interrupt()
                def directory(path,*args,**kwargs):
                    result=mkdir(path,*args,**kwargs)
                    if '.preparing' in str(path):interrupt()
                    return result
                with patch.object(graphics,'atomic_create',write),patch.object(Path,'mkdir',directory):
                    try:runtime._publish_profile(folder,self.files)
                    except SystemExit:pass
                if not folder.exists():runtime._publish_profile(folder,self.files)
                self.assertEqual({name:(folder/name).read_bytes() for name in self.files},self.files)

    def test_failed_partial_file_and_unknown_staging_are_preserved_while_retry_succeeds(self):
        original=graphics.atomic_create
        def fail(path,data,staging):
            if path.name=='ReShade32.dll':path.write_bytes(b'partial');raise OSError('disk full')
            original(path,data,staging)
        with patch.object(graphics,'atomic_create',fail):
            with self.assertRaises(OSError):runtime._publish_profile(self.folder,self.files)
        previous=next(self.base.glob('*.preparing-*'));(previous/'unknown.txt').write_bytes(b'foreign')
        runtime._publish_profile(self.folder,self.files)
        self.assertEqual((previous/'ReShade32.dll').read_bytes(),b'partial')
        self.assertEqual((previous/'unknown.txt').read_bytes(),b'foreign')
        self.assertEqual((self.folder/'ReShade32.dll').read_bytes(),self.files['ReShade32.dll'])

    def test_legacy_unowned_staging_is_not_adopted_or_deleted(self):
        legacy=self.folder.with_name(self.folder.name+'.preparing');legacy.mkdir()
        (legacy/'keep').write_bytes(b'unknown')
        runtime._publish_profile(self.folder,self.files)
        self.assertEqual((legacy/'keep').read_bytes(),b'unknown')

    def test_missing_required_file_before_publish_blocks_then_safely_resumes(self):
        original=graphics.atomic_create
        def remove_earlier_file(path,data,staging):
            original(path,data,staging)
            if path.name=='ReShade.ini':(staging/'ReShade32.dll').unlink()
        with patch.object(graphics,'atomic_create',remove_earlier_file):
            with self.assertRaisesRegex(ValueError,'reshade_preparation_changed'):
                runtime._publish_profile(self.folder,self.files)
        self.assertFalse(self.folder.exists())
        runtime._publish_profile(self.folder,self.files)
        self.assertEqual({name:(self.folder/name).read_bytes() for name in self.files},self.files)

    def test_linked_staging_is_never_followed(self):
        outside=self.base/'outside';outside.mkdir();(outside/'keep').write_bytes(b'foreign')
        link=self.folder.with_name(self.folder.name+'.preparing')
        try:link.symlink_to(outside,target_is_directory=True)
        except OSError:self.skipTest('symlink privilege unavailable')
        runtime._publish_profile(self.folder,self.files)
        self.assertTrue(link.is_symlink());self.assertEqual((outside/'keep').read_bytes(),b'foreign')

    def test_cancelled_staging_resumes_without_changed_bytes(self):
        count=[0]
        def cancelled():count[0]+=1;return count[0]>=4
        with self.assertRaisesRegex(ValueError,'cancelled'):runtime._publish_profile(self.folder,self.files,cancelled=cancelled)
        runtime._publish_profile(self.folder,self.files)
        self.assertEqual((self.folder/'ReShade.ini').read_bytes(),b'config')

    def test_download_bound_size_and_success(self):
        class Stream(io.BytesIO):pass
        progress=[]
        result=runtime._download('https://fixture.invalid',opener=lambda *a,**k:Stream(b'fixture'),progress=lambda *a:progress.append(a))
        self.assertEqual(result,b'fixture');self.assertTrue(progress)
        with self.assertRaisesRegex(ValueError,'reshade_download_invalid'):
            runtime._download('https://fixture.invalid',opener=lambda *a,**k:Stream(b'x'*(8*1024*1024+1)))

    def test_continuous_slow_stream_has_total_deadline_and_closes(self):
        closed=threading.Event()
        class Slow:
            def __enter__(self):return self
            def __exit__(self,*args):closed.set()
            def read1(self,size):time.sleep(.02);return b'x'
        started=time.monotonic()
        with self.assertRaisesRegex(ValueError,'reshade_download_timeout'):
            runtime._download('https://fixture.invalid',timeout=.12,opener=lambda *a,**k:Slow())
        self.assertLess(time.monotonic()-started,.8);self.assertTrue(closed.wait(.5))

    def test_cancel_during_blocked_headers_returns_without_waiting_for_network(self):
        started=threading.Event();release=threading.Event();closed=threading.Event()
        class Stream(io.BytesIO):
            def close(self):super().close();closed.set()
        def opener(*args,**kwargs):started.set();release.wait(2);return Stream(b'fixture')
        begin=time.monotonic()
        try:
            with self.assertRaisesRegex(ValueError,'cancelled'):
                runtime._download('https://fixture.invalid',opener=opener,cancelled=started.is_set)
            self.assertLess(time.monotonic()-begin,.8)
        finally:release.set()
        self.assertTrue(closed.wait(1))


if __name__=='__main__':unittest.main()
