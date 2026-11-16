"""Fresh Gold runtime copies and trusted-worker inventory with authored inputs."""
import hashlib
import json
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/installer'))
import compatibility as content
import gold_support
import easytier_elevation
import install
import platform_standalone


class GoldNetworkTests(unittest.TestCase):
    def setUp(self):
        temp=tempfile.TemporaryDirectory(dir=ROOT/'work');self.addCleanup(temp.cleanup)
        self.home=Path(temp.name);self.package=self.home/'Package';self.game=self.home/'Game'
        self.sources=[('installer/worker.py','worker.py'),('gold/service.py','service.py'),
                      ('installer/worker.py','worker.py')]
        self.files={}
        for name in ('tools/installer/worker.py','tools/gold/service.py','tools/license.txt',
                     'runtime/python/python.exe','runtime/dotnet/dotnet.exe','runtime/easytier/easytier-core.exe'):
            path=self.package/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(name.encode())
            self.files[name]=content.digest(path)
        self.manifest=dict(files=self.files,platform_independent=True,internet_easytier=True,windows_helpers=True)

    def test_copies_complete_runtime_and_deduplicates_helper_targets_without_touching_native(self):
        native=self.game/'Launcher/native.dll';native.parent.mkdir(parents=True);native.write_bytes(b'owned native')
        tracked=[]
        with patch.object(install,'package_manifest',return_value=self.manifest), \
                patch.object(install,'launcher_sources',return_value=self.sources):
            copied=gold_support.prepare_network_runtime(self.package,self.game,tracker=lambda path,digest:tracked.append((path,digest)))
        self.assertEqual(len(copied),6);self.assertEqual(len(tracked),6)
        self.assertEqual(native.read_bytes(),b'owned native')
        for path,digest in tracked:self.assertEqual(content.digest(path),digest)
        self.assertTrue((self.game/'Runtime/python/python.exe').is_file())
        self.assertTrue((self.game/'Runtime/dotnet/dotnet.exe').is_file())
        self.assertTrue((self.game/'Runtime/easytier/easytier-core.exe').is_file())

    def test_incomplete_capabilities_or_changed_source_do_not_overwrite_a_target(self):
        for capability in ('platform_independent','internet_easytier','windows_helpers'):
            with patch.object(install,'package_manifest',return_value={**self.manifest,capability:False}):
                with self.assertRaises(ValueError):gold_support.prepare_network_runtime(self.package,self.game)
        self.assertFalse(self.game.exists())
        path=self.game/'Launcher/worker.py';path.parent.mkdir(parents=True);path.write_bytes(b'preserved existing')
        with patch.object(install,'package_manifest',return_value=self.manifest), \
                patch.object(install,'launcher_sources',return_value=self.sources):
            with self.assertRaises(ValueError):gold_support.prepare_network_runtime(self.package,self.game)
        self.assertEqual(path.read_bytes(),b'preserved existing')
        path.unlink();(self.package/'tools/installer/worker.py').write_bytes(b'foreign helper')
        with patch.object(install,'package_manifest',return_value=self.manifest), \
                patch.object(install,'launcher_sources',return_value=self.sources):
            with self.assertRaises(ValueError):gold_support.prepare_network_runtime(self.package,self.game)
        self.assertFalse(path.exists())

    def test_network_preparation_adds_runtime_to_gold_manifest_and_preserves_game_inventory(self):
        app=self.package/gold_support.FOLDER;app.mkdir(parents=True)
        native_record={'files':{'Data/native.bin':hashlib.sha256(b'owned native').hexdigest()},'platform_independent':True}
        def prepare(*_args,**_kwargs):
            self.game.mkdir();(self.game/'Launcher').mkdir()
            (self.game/'gold-install.json').write_text(json.dumps(native_record))
            return native_record
        runtime=SimpleNamespace(prepare_copy=prepare,read_json=lambda path:json.loads(path.read_text()),
            inventory=lambda root,folders:content.inventory(root,folders),verify_root=lambda root:json.loads((root/'gold-install.json').read_text()))
        with patch.object(gold_support,'verify_package'),patch.object(gold_support,'module',return_value=runtime), \
                patch.object(install,'package_manifest',return_value=self.manifest), \
                patch.object(install,'launcher_sources',return_value=self.sources):
            result=gold_support.prepare(self.package,self.home/'Original',self.game,network_runtime=True)
        self.assertIs(result['network_runtime'],True)
        self.assertEqual(result['files']['Data/native.bin'],native_record['files']['Data/native.bin'])
        self.assertIn('runtime/python/python.exe',{name.casefold() for name in result['files']})
        self.assertIn('launcher/service.py',{name.casefold() for name in result['files']})

    def test_portable_worker_extras_are_checked_against_package_payloads(self):
        launcher=self.game/'Launcher';launcher.mkdir(parents=True)
        for name,data in [('worker.py',b'approved worker'),('LICENSE.txt',b'license'),
                          ('weplay_network_policy.exe',b'approved policy'),
                          ('weplay_gold_startup.dll',b'approved Gold adapter'),
                          ('platform-standalone.json',b'authored validated descriptor')]:
            (launcher/name).write_bytes(data)
        runtime=self.game/'Runtime/python';runtime.mkdir(parents=True);(runtime/'python.exe').write_bytes(b'python')
        files={'tools/installer/worker.py':content.digest(launcher/'worker.py'),
               'tools/license.txt':content.digest(launcher/'LICENSE.txt'),
               'payload/platform/weplay_network_policy.exe':content.digest(launcher/'weplay_network_policy.exe'),
               'payload/gold/payload/weplay_gold_startup.dll':content.digest(launcher/'weplay_gold_startup.dll'),
               **content.inventory(self.game,('Runtime/python',))}
        profile={'edition':'gold'}
        with patch.object(install,'package_manifest',return_value={'files':files,'platform_independent':True}), \
                patch.object(install,'launcher_sources',return_value=[('installer/worker.py','worker.py')]), \
                patch.object(platform_standalone,'load',return_value=(profile,self.game/'Data/game.exe',launcher/'weplay_gold_startup.dll')):
            easytier_elevation.verify_worker(self.game,package=self.package)
            (launcher/'weplay_gold_startup.dll').write_bytes(b'foreign adapter')
            with self.assertRaises(ValueError):easytier_elevation.verify_worker(self.game,package=self.package)


if __name__=='__main__':unittest.main()
