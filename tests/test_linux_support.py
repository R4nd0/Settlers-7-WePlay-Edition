"""Linux boundary regressions; synthetic files only, no game or public network."""
import hashlib
import gzip
import io
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/linux'), str(ROOT / 'tools/installer'), str(ROOT / 'tools/offline-ui'), str(ROOT / 'tools/lan')]
import compatibility
import linux_runtime
import portable_lock
import host_runtime
import graphics_profile
import linux_profiles
import bba
import shutil


class LinuxBoundaryTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(); self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_fresh_source_installer_reaches_copy_without_prepare_module_collision(self):
        script = '''import sys
from pathlib import Path
from unittest.mock import patch
sys.path.insert(0, sys.argv[1])
import launcher, install
root = Path(sys.argv[2])
with patch.object(install, 'package_manifest', return_value={'linux_native': True}), \\
     patch.object(install, 'prepare', side_effect=RuntimeError('copy boundary reached')):
    try:
        launcher.prepare(root, root, root / 'new')
    except RuntimeError as error:
        assert str(error) == 'copy boundary reached', str(error)
    else:
        raise AssertionError('copy boundary was not called')
'''
        result = subprocess.run([sys.executable, '-I', '-B', '-c', script,
                                 str(ROOT / 'tools/linux'), str(self.root)],
                                capture_output=True, timeout=30)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))

    def test_installed_launcher_and_solo_dependencies_work_without_installer_tree(self):
        import install
        manifest = dict(linux_native=True, automatic_lan=True, graphics_choice=True, display_selection=True,
                        files={'tools/installer/' + name: 'fixture' for name in
                               ('portable_lock.py', 'host_runtime.py', 'lan_autoconnect.py')})
        manifest['files'].update({'tools/linux/' + name:'fixture' for name in
                                 ('linux_network.py','linux_elevation.py','linux_profiles.py')})
        directory = self.root/'Launcher'; directory.mkdir()
        for source, target in install.launcher_sources(manifest):
            shutil.copyfile(ROOT/'tools'/source, directory/target)
        environment = dict(os.environ); environment.pop('PYTHONPATH', None)
        help_result = subprocess.run([sys.executable,'-B',str(directory/'linux_launcher.py'),'--help'],
            cwd=self.root, env=environment, capture_output=True, timeout=20)
        # On Windows the platform check is after argparse's --help exit.
        self.assertEqual(help_result.returncode,0,help_result.stderr.decode(errors='replace'))
        imported = subprocess.run([sys.executable,'-I','-B','-c',
            'import sys;sys.path.insert(0,sys.argv[1]);import linux_network', str(directory)],
            cwd=self.root, env=environment, capture_output=True, timeout=20)
        self.assertEqual(imported.returncode,0,imported.stderr.decode(errors='replace'))

    def test_manifest_member_resolves_actual_case_and_rejects_escape(self):
        target = self.root / 'Base/Bin/Settlers7R.exe'; target.parent.mkdir(parents=True); target.write_bytes(b'fixture')
        self.assertEqual(compatibility.member(self.root, 'base/bin/settlers7r.exe'), target)
        for name in ('../escape', '/Base', 'C:/Base', 'base\\bin', 'base/missing'):
            with self.subTest(name=name), self.assertRaises(ValueError): compatibility.member(self.root, name)

    @unittest.skipUnless(sys.platform == 'linux', 'case-sensitive filesystem check')
    def test_ambiguous_case_and_symlink_are_rejected(self):
        (self.root / 'Base').mkdir(); (self.root / 'base').mkdir()
        with self.assertRaises(ValueError): compatibility.member(self.root, 'base')
        (self.root / 'link').symlink_to(self.root / 'Base', target_is_directory=True)
        with self.assertRaises(ValueError): compatibility.member(self.root, 'link')

    def test_streaming_hash_works_without_python311_file_digest(self):
        data = b'abc123' * 400000; path = self.root / 'fixture'; path.write_bytes(data)
        self.assertEqual(compatibility.digest(path), hashlib.sha256(data).hexdigest())

    def test_archive_compression_matches_the_canonical_release_stream_on_every_os(self):
        payload=bytes(range(256))*24+b'WePlay canonical gzip fixture'
        expected=io.BytesIO()
        with gzip.GzipFile(fileobj=expected,mode='wb',mtime=0,compresslevel=9) as stream:stream.write(payload)
        self.assertEqual(bba.canonical_gzip(payload),expected.getvalue())
        self.assertEqual(gzip.decompress(bba.canonical_gzip(payload)),payload)
        for os_byte in (0,3,255):
            raw=bytearray(expected.getvalue());raw[9]=os_byte
            with patch.object(bba.gzip,'compress',return_value=bytes(raw)):
                self.assertEqual(bba.canonical_gzip(payload),expected.getvalue())

    def test_windows_and_linux_tools_share_game_identity_but_keep_local_guard(self):
        def inventory(name):
            root = self.root / name
            for file, data in {'Base/Bin/Settlers7R.exe':b'game', 'Base/Bin/uplay_r1_loader.dll':b'adapter',
                               'Base/BBA/Interface.bba':b'ui', 'Server/service-' + name:b'service',
                               'Launcher/launcher-' + name:b'launcher'}.items():
                target=root/file; target.parent.mkdir(parents=True,exist_ok=True); target.write_bytes(data)
            value=compatibility.create(root)
            (root/'compatibility.json').write_text(json.dumps(value))
            return root,value
        left,a=inventory('windows'); right,b=inventory('linux')
        self.assertNotEqual(a['content_id'],b['content_id'])
        self.assertEqual(compatibility.network_id(a),compatibility.network_id(b))
        (right/'Server/service-linux').write_bytes(b'modified')
        with self.assertRaises(ValueError): compatibility.verify(right)

    def test_wine_prefix_and_documents_are_owned_and_inherited_prefix_is_removed(self):
        value=dict(format=linux_runtime.FORMAT,runner='wine',executable='/usr/bin/wine',steam=None)
        with patch.object(linux_runtime,'settings',return_value=value):
            command, environment=linux_runtime.game_command(self.root, ('-vSynch',), {
                'HOME':'/unrelated/home','WINEPREFIX':'/unrelated/prefix','PROTON_LOG':'1','SteamAppId':'965320'})
        self.assertEqual(command, ['/usr/bin/wine',str(self.root/'Base/Bin/Settlers7R.exe'),'-vSynch'])
        self.assertEqual(environment['WINEPREFIX'],str(self.root/'WinePrefix'))
        self.assertEqual(environment['WINEARCH'],'win32')
        self.assertEqual(environment['HOME'],str(self.root/'LocalData/WineHome'))
        self.assertNotIn('PROTON_LOG',environment)
        self.assertEqual(environment['SteamAppId'],'965320')

    def test_proton_argv_keeps_spaces_and_never_uses_shell(self):
        value=dict(format=linux_runtime.FORMAT,runner='proton',executable='/Steam/Proton Test/proton',steam='/Steam')
        with patch.object(linux_runtime,'settings',return_value=value):
            command,environment=linux_runtime.game_command(self.root,(),{})
        self.assertEqual(command[:2],['/Steam/Proton Test/proton','waitforexitandrun'])
        self.assertEqual(environment['STEAM_COMPAT_DATA_PATH'],str(self.root/'ProtonData'))
        self.assertEqual(environment['STEAM_COMPAT_CLIENT_INSTALL_PATH'],'/Steam')

    def test_unknown_runner_settings_are_not_overwritten(self):
        path=self.root/'LocalData/linux-runtime.json';path.parent.mkdir();path.write_text('{"unknown":true}')
        with self.assertRaises(ValueError):linux_runtime.configure(self.root)
        self.assertEqual(path.read_text(),'{"unknown":true}')

    @unittest.skipUnless(sys.platform == 'linux', 'WSL filesystem guard')
    def test_wsl_windows_mount_is_rejected_before_game_start(self):
        result=subprocess.CompletedProcess([],0,stdout='9p\n',stderr='')
        with patch.object(linux_runtime.subprocess,'run',return_value=result):
            with self.assertRaises(ValueError):linux_runtime.require_linux_filesystem(self.root)
        result.stdout='ext2/ext3\n'
        with patch.object(linux_runtime.subprocess,'run',return_value=result):
            linux_runtime.require_linux_filesystem(self.root)

    def test_update_keeps_owned_saves_and_timestamps_without_copying_runtime_or_other_documents(self):
        old=self.root/'old';new=self.root/'new';old.mkdir();new.mkdir()
        files={'LocalData/local-profile-id.txt':b'fixture-id',
               'LocalData/saves/1001.s7local':b'fixture-save',
               'LocalData/WineHome/Documents/Settlers7/Profiles/Player/Options.ini':b'fixture-options',
               'LocalData/s7-local-runtime.ini':b'transient configuration',
               'LocalData/WineHome/Documents/Unrelated/document.txt':b'unrelated'}
        for name,raw in files.items():
            path=old/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(raw)
        saved=old/'LocalData/saves/1001.s7local';before=saved.stat().st_mtime_ns
        self.assertEqual(linux_profiles.migrate(old,new),3)
        self.assertEqual((new/'LocalData/saves/1001.s7local').read_bytes(),b'fixture-save')
        self.assertEqual((new/'LocalData/saves/1001.s7local').stat().st_mtime_ns,before)
        self.assertEqual(saved.stat().st_mtime_ns,before)
        self.assertFalse((new/'LocalData/s7-local-runtime.ini').exists())
        self.assertFalse((new/'LocalData/WineHome/Documents/Unrelated').exists())

    def test_first_prefix_starts_windowed_without_replacing_imported_settings(self):
        home=self.root/'LocalData/WineHome';home.mkdir(parents=True)
        linux_runtime.initial_documents(self.root,home,'wine')
        path=home/'Documents/THE SETTLERS 7 - History Edition/Options.ini'
        self.assertEqual(path.read_bytes(),b'[Quality]\nFullScreen = 0\nScreenWidth = 1280\nScreenHeight = 720\n')
        path.write_bytes(b'imported settings');linux_runtime.initial_documents(self.root,home,'wine')
        self.assertEqual(path.read_bytes(),b'imported settings')
        path.unlink();(self.root/'WinePrefix').mkdir()
        linux_runtime.initial_documents(self.root,home,'wine');self.assertFalse(path.exists())

    @unittest.skipUnless(sys.platform == 'linux', 'real Wine document symlinks')
    def test_update_imports_history_edition_documents_from_real_prefix_directory(self):
        old=self.root/'old';new=self.root/'new';old.mkdir();new.mkdir()
        user=old/'WinePrefix/drive_c/users/weplay';user.mkdir(parents=True)
        docs=user/'Documents/THE SETTLERS 7 - History Edition'
        docs.mkdir(parents=True);(docs/'Options.ini').write_bytes(b'fixture-options')
        (user/'My Documents').symlink_to(user/'Documents',target_is_directory=True)
        (user/'account-cache').write_bytes(b'never imported')
        self.assertEqual(linux_profiles.migrate(old,new),1)
        self.assertEqual((new/'LocalData/WineHome/Documents'/docs.name/'Options.ini').read_bytes(),b'fixture-options')
        self.assertFalse((new/'WinePrefix').exists())

    @unittest.skipUnless(sys.platform == 'linux', 'real Wine document symlinks')
    def test_moved_export_rebases_only_its_owned_documents(self):
        old=self.root/'old';old.mkdir();linux_profiles.relocate(old)
        docs=old/'LocalData/WineHome/Documents';docs.mkdir(parents=True)
        (docs/'save').write_bytes(b'fixture-save')
        user=old/'WinePrefix/drive_c/users/weplay';user.mkdir(parents=True)
        (user/'Documents').symlink_to(docs,target_is_directory=True)
        new=self.root/'new';old.rename(new)
        self.assertEqual(linux_profiles.relocate(new),1)
        self.assertEqual((new/'WinePrefix/drive_c/users/weplay/Documents').resolve(),new/'LocalData/WineHome/Documents')
        self.assertEqual((new/'LocalData/WineHome/Documents/save').read_bytes(),b'fixture-save')
        self.assertEqual(linux_profiles.relocate(new),0)

    @unittest.skipUnless(sys.platform == 'linux', 'real Wine document symlinks')
    def test_foreign_document_link_fails_before_replacing_any_profile(self):
        user=self.root/'WinePrefix/drive_c/users/weplay';user.mkdir(parents=True)
        foreign=self.root.parent/'unrelated-documents'
        link=user/'Documents';link.symlink_to(foreign,target_is_directory=True)
        with self.assertRaises(ValueError):linux_profiles.relocate(self.root)
        self.assertEqual(os.readlink(link),str(foreign))
        self.assertFalse((self.root/'LocalData/linux-location.json').exists())

    def test_exclusive_graphics_publication_preserves_occupied_target(self):
        target=self.root/'d3d9.dll';target.write_bytes(b'foreign')
        with self.assertRaises(FileExistsError):graphics_profile.atomic_create(target,b'owned',self.root)
        self.assertEqual(target.read_bytes(),b'foreign')
        self.assertEqual(list(self.root.iterdir()),[target])

    @unittest.skipUnless(sys.platform == 'linux', 'real POSIX process locks')
    def test_lock_excludes_another_process_and_releases_on_exit(self):
        script='import sys,fcntl;f=open(sys.argv[1],"a+b");fcntl.flock(f,fcntl.LOCK_EX|fcntl.LOCK_NB)'
        path=self.root/'session.lock'
        with path.open('a+b') as lock:
            portable_lock.locking(lock.fileno(),portable_lock.LK_NBLCK,1)
            child=subprocess.run([sys.executable,'-c',script,str(path)],capture_output=True)
            self.assertNotEqual(child.returncode,0)
            portable_lock.locking(lock.fileno(),portable_lock.LK_UNLCK,1)
        self.assertEqual(subprocess.run([sys.executable,'-c',script,str(path)],capture_output=True).returncode,0)

    @unittest.skipUnless(sys.platform == 'linux', 'native Linux service lifetime')
    def test_service_job_execs_native_child_and_close_reaps_only_it(self):
        from lan_child_job import ServiceJob
        job=ServiceJob()
        child=job.start([sys.executable,'-c','import time;time.sleep(30)'])
        self.addCleanup(job.close)
        self.assertIsNone(child.poll())
        job.close();self.assertIsNotNone(child.poll())

    @unittest.skipUnless(sys.platform == 'linux', 'native Linux coordinator session lifetime')
    def test_coordinator_survives_creator_session_teardown(self):
        import signal
        import time
        directory=self.root/'Launcher';directory.mkdir();(self.root/'LocalData').mkdir()
        helper=directory/'lan_hub.py'
        helper.write_text('from pathlib import Path\nimport time,os,json\n'
            'root=Path(__file__).resolve().parents[1]\n'
            '(root/"child.json").write_text(json.dumps({"pid":os.getpid()}))\n'
            'while True:\n (root/"heartbeat").write_text(str(time.monotonic_ns()))\n time.sleep(.05)\n')
        creator=self.root/'creator.py'
        creator.write_text('from pathlib import Path\nfrom types import SimpleNamespace as S\nimport sys,time,json\n'
            f'root=Path({str(self.root)!r})\nrepo=Path({str(ROOT)!r})\n'
            'sys.path[:0]=[str(repo/"tools"/p) for p in ("installer","offline-ui","linux","lan")]\n'
            'import launch_auto_lan as launch\n'
            'launch.uuid.uuid4=lambda:S(hex="a"*32)\n'
            'interface=S(address="192.168.50.1",prefix_length=24)\n'
            'discovery=S(list_rooms=lambda **kw:[S(room_id="a"*32,endpoint=S(host=interface.address))])\n'
            'launch.start_hub(root,interface,"b"*64,"Synthetic",discovery)\n'
            'while True:time.sleep(1)\n')
        parent=subprocess.Popen([sys.executable,'-B',str(creator)],start_new_session=True,
            stdin=subprocess.DEVNULL,stdout=subprocess.DEVNULL,stderr=subprocess.DEVNULL)
        child=None
        try:
            deadline=time.monotonic()+10
            while not (self.root/'heartbeat').exists():
                self.assertIsNone(parent.poll());self.assertLess(time.monotonic(),deadline);time.sleep(.05)
            child=json.loads((self.root/'child.json').read_text())['pid']
            first=(self.root/'heartbeat').read_text()
            os.killpg(parent.pid,signal.SIGTERM);parent.wait(timeout=5)
            time.sleep(.2)
            self.assertNotEqual((self.root/'heartbeat').read_text(),first,
                'The coordinator died with its creator session instead of retaining admitted peers')
        finally:
            if parent.poll() is None:os.killpg(parent.pid,signal.SIGTERM);parent.wait(timeout=5)
            if child is not None:
                process=Path('/proc')/str(child)
                if process.exists() and str(helper).encode() in (process/'cmdline').read_bytes().split(b'\0'):
                    os.kill(child,signal.SIGTERM)


if __name__ == '__main__': unittest.main()
