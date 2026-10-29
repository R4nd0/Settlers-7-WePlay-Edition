"""Updater trust/lifecycle checks; invented executables are never executed."""
import base64
import ctypes
from ctypes import wintypes
import hashlib
import json
import importlib.util
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/installer'))
import update_client as client


@unittest.skipUnless(os.name=='nt','CNG signing/DPAPI and Windows update handoff')
class UpdateTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        base=ROOT/'work/test-temp';base.mkdir(parents=True,exist_ok=True)
        cls.shared=tempfile.TemporaryDirectory(prefix='update-sign-fixture-',dir=base)
        cls.shared_path=Path(cls.shared.name);cls.private=cls.shared_path/'fixture.dpapi';cls.public=cls.shared_path/'key.json'
        created=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-File',str(ROOT/'tools/updates/sign_manifest.ps1'),
            '-Action','CreateKey','-KeyPath',str(cls.private),'-PublicPath',str(cls.public)],capture_output=True,text=True)
        if created.returncode:raise AssertionError('Temporary fixture signing key could not be created.')
        cls.pin=client.public_key(cls.public)

    @classmethod
    def tearDownClass(cls):cls.shared.cleanup()

    def setUp(self):
        self.temporary=tempfile.TemporaryDirectory(prefix='update-case-',dir=ROOT/'work/test-temp');self.addCleanup(self.temporary.cleanup)
        self.root=Path(self.temporary.name);self.app=self.root/'App';self.app.mkdir()
        (self.app/'desktop-release.json').write_bytes(client.canonical({'version':'2.0.0-rc3.4','update_sequence':1,'update_channel':'preview'}))
        (self.app/'State').mkdir();(self.app/'State/keep.json').write_bytes(b'private preferences fixture')
        (self.app/'Instances').mkdir();(self.app/'Instances/keep.s7local').write_bytes(b'opaque save fixture')
        self.setup=b'MZ invented setup fixture - must never be executed'
        self.patch=patch.object(client,'public_key',return_value=self.pin);self.patch.start();self.addCleanup(self.patch.stop)

    def value(self,sequence=2):
        return {'format':'weplay-update-v1','repository':client.REPOSITORY,'channel':'preview','sequence':sequence,
                'version':'2.0.0-rc3.'+str(sequence+3),'tag':'weplay-test-'+str(sequence),'notes':'A useful update.',
                'setup':{'name':'Setup.exe','url':'https://github.com/'+client.REPOSITORY+'/releases/download/weplay-test-'+str(sequence)+'/Setup.exe',
                         'size':len(self.setup),'sha256':hashlib.sha256(self.setup).hexdigest()}}

    def signed(self,value=None,payload=None):
        payload=payload if payload is not None else client.canonical(value or self.value())
        path=self.root/'payload.json';path.write_bytes(payload)
        result=subprocess.run(['powershell.exe','-NoProfile','-NonInteractive','-File',str(ROOT/'tools/updates/sign_manifest.ps1'),
            '-Action','Sign','-KeyPath',str(self.private),'-PayloadPath',str(path)],capture_output=True,text=True)
        self.assertEqual(result.returncode,0,'fixture signing must succeed')
        return client.canonical({'format':'weplay-signed-update-v1','key_id':self.pin['key_id'],
               'payload':base64.urlsafe_b64encode(payload).decode().rstrip('='),'signature':result.stdout.strip()})

    def fetcher(self,envelope):
        value=client.verify(envelope)
        def fetch(url,limit,**kwargs):
            if url==client.RELEASES:
                return json.dumps([{'draft':False,'prerelease':True,'tag_name':value['tag'],'assets':[
                    {'name':client.MANIFEST,'browser_download_url':'https://github.com/'+client.REPOSITORY+'/releases/download/'+value['tag']+'/'+client.MANIFEST}]}]).encode()
            if url.endswith('/'+client.MANIFEST):return envelope
            self.assertEqual(url,value['setup']['url']);Path(kwargs['destination']).write_bytes(self.setup)
            return len(self.setup),hashlib.sha256(self.setup).hexdigest()
        return fetch

    def processes(self,pid):
        return {'pid':pid,'path':str(self.app/('Package/Runtime/python/python.exe' if pid==os.getpid() else 'S7Launcher.exe')),'started_ticks':111}

    def prepare(self,envelope):
        fetch=self.fetcher(envelope);client.check(self.app,fetch=fetch)
        return client.prepare(self.app,os.getppid(),111,fetch=fetch,process_reader=self.processes)

    def test_cng_signature_and_standard_library_verifier_agree(self):
        raw=self.signed();self.assertEqual(client.verify(raw),self.value())
        envelope=json.loads(raw);envelope['signature']='A'+envelope['signature'][1:]
        with self.assertRaisesRegex(client.UpdateError,'signature'):client.verify(client.canonical(envelope))

    def test_maintainer_tool_signs_only_and_returns_reviewable_publish_arguments(self):
        spec=importlib.util.spec_from_file_location('weplay_release_fixture',ROOT/'tools/updates/build_release.py')
        module=importlib.util.module_from_spec(spec);spec.loader.exec_module(module)
        setup=self.root/'Setup.exe';setup.write_bytes(self.setup);output=self.root/'weplay-update.json'
        result=module.build(setup,output,version=self.value()['version'],sequence=2,tag=self.value()['tag'],notes='Fixture notes',key_path=self.private)
        self.assertEqual(client.verify(output.read_bytes())['setup']['sha256'],hashlib.sha256(self.setup).hexdigest())
        self.assertEqual(result['publish_command'][:3],['gh','release','create'])
        self.assertIn(client.REPOSITORY,result['publish_command'])
        self.assertEqual((self.app/'State/keep.json').read_bytes(),b'private preferences fixture')

    def test_signed_payload_is_canonical_and_duplicate_fields_are_refused(self):
        with self.assertRaisesRegex(client.UpdateError,'invalid'):
            client.verify(self.signed(payload=b'{"format":"weplay-update-v1","format":"duplicate"}'))

    def test_wrong_repository_external_setup_and_path_traversal_are_refused(self):
        for change in ({'repository':'attacker/other'},{'tag':'../escape'}, {'channel':'stable'},{'sequence':True}):
            with self.subTest(change=change),self.assertRaises(client.UpdateError):client.validate(dict(self.value(),**change))
        value=self.value();value['setup']['url']='https://attacker.invalid/Setup.exe'
        with self.assertRaises(client.UpdateError):client.validate(value)

    def test_unsigned_existing_prereleases_do_not_show_a_false_update(self):
        fetch=Mock(return_value=json.dumps([{'draft':False,'prerelease':True,'tag_name':'weplay-rc2.1','assets':[]}]).encode())
        result=client.check(self.app,fetch=fetch)
        self.assertFalse(result['available']);self.assertEqual(result['reason'],'no_signed_release');fetch.assert_called_once()

    def test_preview_release_list_announces_a_signed_update(self):
        result=client.check(self.app,fetch=self.fetcher(self.signed()))
        self.assertTrue(result['available']);self.assertEqual(result['sequence'],2)
        self.assertEqual(result['current_version'],'2.0.0-rc3.4');self.assertEqual(result['reason'],'available')
        self.assertTrue((self.app/'State/Updates/verified-envelope.json').is_file())

    def test_replay_cannot_lower_highest_seen_sequence_or_replace_cached_offer(self):
        newest=self.signed(self.value(3));client.check(self.app,fetch=self.fetcher(newest))
        before=(self.app/'State/Updates/verified-envelope.json').read_bytes()
        with self.assertRaisesRegex(client.UpdateError,'downgrade'):client.check(self.app,fetch=self.fetcher(self.signed(self.value(2))))
        self.assertEqual((self.app/'State/Updates/verified-envelope.json').read_bytes(),before)

    def test_prepare_verifies_download_without_touching_saves_preferences_or_active_app(self):
        result=self.prepare(self.signed());job=Path(result['job'])
        self.assertEqual((job/'Setup.exe').read_bytes(),self.setup)
        self.assertEqual((self.app/'State/keep.json').read_bytes(),b'private preferences fixture')
        self.assertEqual((self.app/'Instances/keep.s7local').read_bytes(),b'opaque save fixture')
        self.assertFalse((job/'ready.json').exists())

    def test_compiled_package_copies_and_launches_only_verified_exe_handoff(self):
        helper=self.app/'Package/Payload/Helpers/WePlay.UpdateHelper.exe';helper.parent.mkdir(parents=True)
        helper.write_bytes(b'MZ unexecuted helper fixture')
        (self.app/'Package/package.json').write_bytes(client.canonical({'windows_helpers':True,
            'files':{'payload/helpers/weplay.updatehelper.exe':hashlib.sha256(helper.read_bytes()).hexdigest()}}))
        job=Path(self.prepare(self.signed())['job'])
        self.assertEqual((job/helper.name).read_bytes(),helper.read_bytes())
        self.assertFalse((job/'update_handoff.ps1').exists())
        child=Mock();child.poll.return_value=2
        with patch.object(client.subprocess,'Popen',return_value=child) as start, \
                self.assertRaisesRegex(client.UpdateError,'handoff_failed'):
            client.start_handoff(self.app,job)
        self.assertEqual(start.call_args.args[0],[str(job/helper.name),'--job',str(job/'handoff.json')])
        self.assertFalse((job/'ready.json').exists())
        helper.write_bytes(b'modified')
        with self.assertRaisesRegex(client.UpdateError,'invalid'):client.handoff_source(self.app)

    def test_changed_download_never_becomes_an_executable_and_keeps_existing_state(self):
        envelope=self.signed();fetch=self.fetcher(envelope);client.check(self.app,fetch=fetch)
        def corrupt(url,limit,**kwargs):
            Path(kwargs['destination']).write_bytes(b'changed');return 7,hashlib.sha256(b'changed').hexdigest()
        with self.assertRaisesRegex(client.UpdateError,'hash_invalid'):
            client.prepare(self.app,os.getppid(),111,fetch=corrupt,process_reader=self.processes)
        self.assertEqual(list((self.app/'State/Updates').rglob('Setup.exe')),[])
        self.assertEqual(list((self.app/'State/Updates').rglob('Setup.partial')),[])
        self.assertEqual((self.app/'State/keep.json').read_bytes(),b'private preferences fixture')

    def test_cancelled_download_reclaims_only_its_partial_and_preserves_game(self):
        envelope=self.signed();client.check(self.app,fetch=self.fetcher(envelope))
        def interrupted(url,limit,**kwargs):
            Path(kwargs['destination']).write_bytes(b'partial download');raise RuntimeError('cancelled')
        with self.assertRaisesRegex(RuntimeError,'cancelled'):
            client.prepare(self.app,os.getppid(),111,fetch=interrupted,process_reader=self.processes)
        self.assertEqual(list((self.app/'State/Updates').rglob('Setup.partial')),[])
        self.assertEqual((self.app/'Instances/keep.s7local').read_bytes(),b'opaque save fixture')

    def test_wrong_parent_path_or_creation_time_blocks_before_download(self):
        envelope=self.signed();client.check(self.app,fetch=self.fetcher(envelope));fetch=Mock()
        with self.assertRaisesRegex(client.UpdateError,'parent_invalid'):
            client.prepare(self.app,os.getppid(),112,fetch=fetch,process_reader=self.processes)
        fetch.assert_not_called()

    def test_cache_lock_prevents_overlapping_commit(self):
        app,updates=client.paths(self.app)
        envelope=self.signed()
        with client.cache_lock(updates),self.assertRaisesRegex(client.UpdateError,'busy'):
            client.check(self.app,fetch=self.fetcher(envelope))

    def test_real_windows_process_identity_reports_exact_interpreter_and_start_token(self):
        identity=client.process_info(os.getpid())
        self.assertEqual(Path(identity['path']),Path(sys.executable).resolve())
        self.assertGreater(identity['started_ticks'],500000000000000000)

    def test_powershell_handoff_rechecks_signature_and_hash_without_executing_fixture(self):
        result=self.prepare(self.signed());job=Path(result['job'])
        trusted=self.app/'Package/Tools/installer';trusted.mkdir(parents=True)
        for target in (trusted/'update-key.json',job/'update-key.json'):shutil.copyfile(self.public,target)
        shutil.copyfile(ROOT/'tools/installer/update_handoff.ps1',trusted/'update_handoff.ps1')
        args=['powershell.exe','-NoProfile','-NonInteractive','-File',str(job/'update_handoff.ps1'),'-Job',str(job/'handoff.json'),'-ValidateOnly']
        valid=subprocess.run(args,capture_output=True,text=True,timeout=30)
        self.assertEqual(valid.returncode,0,valid.stdout+valid.stderr)
        (job/'Setup.exe').write_bytes(b'tampered')
        rejected=subprocess.run(args,capture_output=True,text=True,timeout=30)
        self.assertEqual(rejected.returncode,2);self.assertIn('update_hash_invalid',rejected.stdout)
        self.assertFalse((job/'ready.json').exists());self.assertFalse((self.app/'State/Updates/status.json').exists())

    def test_real_handoff_validates_process_handles_and_holds_mutex_before_app_exit(self):
        result=self.prepare(self.signed());job=Path(result['job'])
        trusted=self.app/'Package/Tools/installer';trusted.mkdir(parents=True)
        for target in (trusted/'update-key.json',job/'update-key.json'):shutil.copyfile(self.public,target)
        shutil.copyfile(ROOT/'tools/installer/update_handoff.ps1',trusted/'update_handoff.ps1')
        system=Path(os.environ['SystemRoot'])/'System32/cmd.exe'
        executables=[self.app/'S7Launcher.exe',self.app/'Package/Runtime/python/python.exe']
        children=[];helper=None
        try:
            for target in executables:
                target.parent.mkdir(parents=True,exist_ok=True);shutil.copyfile(system,target)
                children.append(subprocess.Popen([str(target),'/d','/q','/k'],stdin=subprocess.PIPE,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW))
            record=json.loads((job/'handoff.json').read_bytes());record['parent']=client.process_info(children[0].pid);record['backend']=client.process_info(children[1].pid)
            client.atomic(job/'handoff.json',record)
            helper=subprocess.Popen(['powershell.exe','-NoProfile','-NonInteractive','-File',str(job/'update_handoff.ps1'),'-Job',str(job/'handoff.json')],stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL,creationflags=subprocess.CREATE_NO_WINDOW)
            deadline=time.monotonic()+15
            while time.monotonic()<deadline and not (job/'ready.json').exists() and helper.poll() is None:time.sleep(.05)
            self.assertIsNone(helper.poll(),'handoff must remain waiting for both verified parents')
            self.assertEqual(json.loads((job/'ready.json').read_bytes())['helper_pid'],helper.pid)
            self.assertEqual(json.loads((self.app/'State/Updates/status.json').read_bytes())['phase'],'waiting')
            kernel=ctypes.WinDLL('kernel32',use_last_error=True)
            kernel.OpenMutexW.argtypes=[wintypes.DWORD,wintypes.BOOL,wintypes.LPCWSTR];kernel.OpenMutexW.restype=wintypes.HANDLE
            kernel.CloseHandle.argtypes=[wintypes.HANDLE];kernel.CloseHandle.restype=wintypes.BOOL
            mutex='Local\\S7Launcher-Update-'+hashlib.sha256(str(self.app).lower().encode()).hexdigest().upper()[:24]
            handle=kernel.OpenMutexW(0x100000,False,mutex)
            self.assertTrue(handle,'real helper must hold the exact launcher update mutex')
            if handle:kernel.CloseHandle(handle)
            self.assertTrue(all(child.poll() is None for child in children),'no fixture process is killed by the updater')
            self.assertEqual((job/'Setup.exe').read_bytes(),self.setup,'invented Setup remains unexecuted')
        finally:
            # Stop the waiting helper first; the fake installer must never run.
            if helper and helper.poll() is None:helper.terminate();helper.wait(timeout=5)
            for child in children:
                if child.poll() is None:child.terminate();child.wait(timeout=5)
                if child.stdin:child.stdin.close()


class TransferTests(unittest.TestCase):
    def test_redirect_never_reaches_an_unapproved_host(self):
        response=Mock(status=302);response.getheader.return_value='https://attacker.invalid/payload'
        connection=Mock(getresponse=Mock(return_value=response));factory=Mock(return_value=connection)
        with self.assertRaises(client.UpdateError):client.transfer('https://github.com/'+client.REPOSITORY+'/releases/download/test/Setup.exe',100,connection_factory=factory)
        factory.assert_called_once();connection.close.assert_called_once()

    def test_https_only_no_userinfo_or_nonstandard_ports(self):
        for url in ('http://github.com/x','https://user@github.com/x','https://github.com:444/x','https://github.com:bad/x','https://github.com.attacker.invalid/x'):
            with self.subTest(url=url),self.assertRaises(client.UpdateError):client.allowed_url(url)

    def test_oversized_response_is_rejected_before_writing(self):
        response=Mock(status=200);response.getheader.return_value='101'
        connection=Mock(getresponse=Mock(return_value=response))
        with self.assertRaises(client.UpdateError):client.transfer('https://api.github.com/repos/x',100,connection_factory=Mock(return_value=connection))
        response.read.assert_not_called()


if __name__=='__main__':unittest.main()
