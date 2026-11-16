"""Known old adapters remain repairable; fresh payloads require the current pin."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tests'),str(ROOT/'tools/installer')]
import test_platform_standalone as history_fixtures
import test_gold_installer as gold_fixtures
import compatibility as content
import platform_standalone as platform
import release_policy
import graphics_choice
import gold_support

sha=lambda data:hashlib.sha256(data).hexdigest()


class AdapterMigrationTests(unittest.TestCase):
    def history(self):
        case=history_fixtures.PlatformStandaloneTests('test_missing_descriptor_never_falls_back_to_shared_documents')
        case.setUp();self.addCleanup(case.doCleanups)
        return case

    def rotate_history(self,old):
        for name,value in [('HISTORY_PINS',{(False,False):sha(b'new native adapter'),(True,True):sha(b'new native adapter')}),
                           ('LEGACY_HISTORY_PINS',(old,))]:
            guard=patch.object(platform,name,value);guard.start();self.addCleanup(guard.stop)

    def test_known_old_descriptor_still_loads_after_current_pin_advances(self):
        case=self.history();root=case.prepare()
        self.rotate_history(content.digest(root/'Base/Bin/uplay_r1_loader.dll'))
        value,_,_=platform.load(root)
        self.assertEqual(value['edition'],'history')
        self.assertTrue(platform.enabled(root))

    def test_old_adapter_alone_prevents_profile_fallback_and_isolation_downgrade(self):
        case=self.history();old=b'known previous native adapter';self.rotate_history(sha(old))
        root=case.fixture.root/'Damaged';path=root/'Base/Bin/uplay_r1_loader.dll'
        path.parent.mkdir(parents=True);path.write_bytes(old)
        with self.assertRaises(ValueError):platform.enabled(root)
        self.assertTrue(release_policy.protected_game(root))
        with self.assertRaisesRegex(ValueError,'platform_isolation_required'):
            release_policy.preserve_game(root,{'platform_independent':False})

    def test_legacy_pin_does_not_authorize_fresh_preparation(self):
        case=self.history();f=case.fixture
        old=(f.package/'Payload/uplay_r1_loader.dll').read_bytes();self.rotate_history(sha(old))
        fresh=f.root/'Fresh';shutil.copytree(f.source,fresh)
        path=fresh/'Base/Bin/uplay_r1_loader.dll';path.write_bytes(old)
        before=content.inventory(fresh,('Base',))
        with self.assertRaisesRegex(ValueError,'Ungeprüfter Standalone-Adapter'):
            platform.prepare(fresh,f.package/'Payload/Platform')
        self.assertEqual(content.inventory(fresh,('Base',)),before)
        self.assertFalse((fresh/platform.RECORD).exists())
        path.write_bytes(b'new native adapter')
        (fresh/'Launcher').mkdir()
        platform.prepare(fresh,f.package/'Payload/Platform')
        platform.load(fresh)

    def test_self_consistent_foreign_adapter_is_not_a_legacy_adapter(self):
        case=self.history();root=case.prepare()
        self.rotate_history(content.digest(root/'Base/Bin/uplay_r1_loader.dll'))
        path=root/'Base/Bin/uplay_r1_loader.dll';path.write_bytes(b'foreign adapter')
        value=json.loads((root/platform.RECORD).read_text())
        value['files']['Base/Bin/uplay_r1_loader.dll']=content.digest(path)
        (root/platform.RECORD).write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError,'Ungeprüftes Standalone-Adapterprofil'):
            platform.load(root)

    def test_extended_graphics_recognizes_current_and_exact_legacy_history(self):
        case=self.history();root=case.prepare();path=root/'Base/Bin/uplay_r1_loader.dll'
        self.rotate_history(content.digest(path))
        self.assertTrue(graphics_choice.visual_supported(root))
        path.write_bytes(b'new native adapter');self.assertTrue(graphics_choice.visual_supported(root))
        path.write_bytes(b'foreign adapter');self.assertFalse(graphics_choice.visual_supported(root))


class GoldPayloadMigrationTests(unittest.TestCase):
    def setUp(self):
        self.case=gold_fixtures.GoldInstallerTests('test_rejects_unknown_adapter_even_when_manifest_matches_its_bytes')
        self.case.setUp();self.addCleanup(self.case.doCleanups)
        self.runtime=gold_fixtures.installer;self.source=self.case.app
        self.old_adapter=(self.source/'Payload/weplay_gold_startup.dll').read_bytes()
        self.new_adapter=b'new verified native Gold adapter'
        self.native=self.case.root/'current.dll';self.native.write_bytes(self.new_adapter)
        for name,value in [('GOLD',sha(self.new_adapter)),('LEGACY_GOLD_PINS',(sha(self.old_adapter),)),
                           ('PAYLOAD',{'policy.fixture':sha(b'authored policy')})]:
            guard=patch.object(platform,name,value);guard.start();self.addCleanup(guard.stop)
        path=self.source/'Payload/Platform/policy.fixture';path.parent.mkdir();path.write_bytes(b'authored policy')
        for name in (*platform.TOOLS,'compatibility.py'):
            path=self.source/'Tools/gold'/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'authored fixture tool')
        self.case.manifest.update(platform_independent=True,menu_sha256=self.runtime.SOURCE_MENU_PINS[0])
        self.case.manifest['files']=self.runtime.inventory(self.source,('Tools','Runtime','Payload','Definitions','Licenses'))
        self.case.save_manifest()

    def test_legacy_package_is_build_input_only_and_foreign_native_is_rejected(self):
        with self.assertRaises(ValueError):self.runtime.verify_package(self.source)
        self.runtime.verify_package(self.source,source_package=True)
        path=self.source/'Payload/weplay_gold_startup.dll';path.write_bytes(b'foreign native')
        self.case.manifest['files'][path.relative_to(self.source).as_posix()]=content.digest(path)
        self.case.save_manifest()
        with self.assertRaises(ValueError):self.runtime.verify_package(self.source,source_package=True)

    def test_bundle_requires_current_native_before_staging_and_replaces_legacy_payload(self):
        target=self.case.root/'NewPackage';repo=self.case.root/'Source'
        for name in self.case.manifest['files']:
            if name.startswith('Tools/gold/'):
                path=repo/'tools/installer'/Path(name).name;path.parent.mkdir(parents=True,exist_ok=True)
                path.write_bytes(b'current fixture helper')
        for folder,names in [('gold',('release_interface.py','graphics_assets.py')),
                             ('midsummer',('profile.py','archive_append.py','camera_preferences.lua','runtime.lua'))]:
            for name in names:
                path=repo/'tools'/folder/name;path.parent.mkdir(parents=True,exist_ok=True);path.write_bytes(b'current authored source')
        before=self.runtime.inventory(self.source,('Tools','Runtime','Payload','Definitions','Licenses'))
        with patch.object(gold_support,'module',return_value=self.runtime):
            with self.assertRaisesRegex(ValueError,'current verified Gold production adapter'):
                gold_support.bundle(self.source,target,source_root=repo)
            self.assertFalse(target.exists())
            gold_support.bundle(self.source,target,source_root=repo,adapter=self.native)
            value=self.runtime.verify_package(target)
        self.assertEqual((target/'Payload/weplay_gold_startup.dll').read_bytes(),self.new_adapter)
        self.assertEqual(value['files']['Payload/weplay_gold_startup.dll'],sha(self.new_adapter))
        self.assertEqual(value['menu_sha256'],self.runtime.gold.MENU_SHA256)
        self.assertEqual(self.runtime.inventory(self.source,('Tools','Runtime','Payload','Definitions','Licenses')),before)


if __name__=='__main__':unittest.main()
