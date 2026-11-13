"""Authored files only. Actual loader, image and WFP evidence is separate."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

sys.path.insert(0,str(Path(__file__).resolve().parents[1]/'tools/installer'))
import test_installer as fixtures
import compatibility as content
import install
import isolated_process
import network_peer_policy
import platform_standalone as platform
import display_settings


def sha(value):return hashlib.sha256(value).hexdigest()


class PlatformStandaloneTests(unittest.TestCase):
    def setUp(self):
        self.fixture=fixtures.InstallerTests('test_prepares_separate_copy_without_identity_or_original_changes')
        self.fixture.setUp();self.addCleanup(self.fixture.doCleanups)
        f=self.fixture
        self.original_miles=b'authored original Miles library'
        self.original_steam=b'authored original Steam SDK library'
        self.steam_executable=b'authored original packed Steam executable'
        self.payloads={'mss32.dll':b'authored forwarding gate','steam_api.dll':b'authored unavailable boundary',
                       'weplay_network_policy.exe':b'authored Windows policy helper'}
        for name,value in self.payloads.items():f.write(f.package,'Payload/Platform/'+name,value)
        for name in platform.TOOLS:f.write(f.package,'Tools/installer/'+name,(fixtures.TOOLS/name).read_bytes())
        f.write(f.source,'Base/Bin/mss32.dll',self.original_miles)
        f.write(f.source,'Base/Bin/steam_api.dll',self.original_steam)
        f.write(f.source,'Base/Bin/Settlers7R.exe',self.steam_executable)
        constants={'MILES_ORIGINAL':sha(self.original_miles),'STEAM_ORIGINAL':sha(self.original_steam),
                   'MILES_GATE':sha(self.payloads['mss32.dll']),'STEAM_API':sha(self.payloads['steam_api.dll']),
                   'POLICY':sha(self.payloads['weplay_network_policy.exe']),
                   'HISTORY_PINS':{(False,False):content.digest(f.package/'Payload/uplay_r1_loader.dll')},
                   'PAYLOAD':{name:sha(value) for name,value in self.payloads.items()}}
        for key,value in constants.items():
            guard=patch.object(platform,key,value);guard.start();self.addCleanup(guard.stop)
        guard=patch.object(install,'STEAM_EXE',sha(self.steam_executable));guard.start();self.addCleanup(guard.stop)
        guard=patch.dict(isolated_process.PROFILES,{sha(self.steam_executable):(2,)})
        guard.start();self.addCleanup(guard.stop)
        f.manifest();f.data.update(platform_independent=True,steam_history=True)
        self.manifest()

    def manifest(self):
        f=self.fixture
        f.data['files']=content.inventory(f.package,('Payload','Runtime','Tools'))
        (f.package/'package.json').write_text(json.dumps(f.data))

    def prepare(self):
        return self.fixture.prepare()

    def test_original_steam_exe_and_source_stay_pristine_and_sdk_repair_bytes_are_retained(self):
        f=self.fixture;before=content.inventory(f.source,('Base',))
        root=self.prepare();content.verify(root)
        self.assertEqual(content.digest(root/'Base/Bin/Settlers7R.exe'),sha(self.steam_executable))
        self.assertEqual(content.inventory(f.source,('Base',)),before)
        self.assertEqual((root/'RepairSource/Base/Bin/mss32.dll').read_bytes(),self.original_miles)
        self.assertEqual((root/'RepairSource/Base/Bin/steam_api.dll').read_bytes(),self.original_steam)
        self.assertEqual((root/'Base/Bin/weplay_miles_original.dll').read_bytes(),self.original_miles)
        self.assertEqual((root/'Base/Bin/steam_api.dll').read_bytes(),self.payloads['steam_api.dll'])

    def test_managed_repair_works_after_original_source_is_removed_and_live_generated_files_are_corrupt(self):
        f=self.fixture;root=self.prepare()
        save=platform.options_path(root).parent/'Saves/newer.s7s';save.parent.mkdir();save.write_bytes(b'newer progress')
        f.source.rename(f.root/'SourceUnavailable')
        for name in ('mss32.dll','steam_api.dll','uplay_r1_loader.dll'):(root/'Base/Bin'/name).write_bytes(b'corrupt generated file')
        (root/'Base/Bin/Settlers7R.exe').unlink()
        repaired=f.root/'Repaired'
        install.prepare(f.package,root,repaired,managed=True,emit=lambda _:None)
        content.verify(repaired);platform.load(repaired)
        self.assertEqual(save.read_bytes(),b'newer progress')
        self.assertEqual((repaired/'Base/Bin/Settlers7R.exe').read_bytes(),self.steam_executable)
        self.assertEqual((repaired/'Base/Bin/steam_api.dll').read_bytes(),self.payloads['steam_api.dll'])

    def test_portable_profile_and_descriptor_resolve_after_relocation_without_absolute_paths(self):
        f=self.fixture;root=self.prepare()
        old=platform.options_path(root);old.write_bytes(b'portable settings')
        moved=f.root/'Moved';root.rename(moved)
        self.assertEqual(platform.options_path(moved).read_bytes(),b'portable settings')
        self.assertTrue(platform.options_path(moved).is_relative_to(moved/'LocalData/ProfileRoot'))
        self.assertNotIn(str(f.root),(moved/platform.RECORD).read_text())
        platform.load(moved);content.verify(moved)

    def test_missing_descriptor_never_falls_back_to_shared_documents(self):
        root=self.prepare();(root/platform.RECORD).unlink()
        with self.assertRaises(ValueError):platform.enabled(root)
        (root/'Launcher/platform_standalone.py').unlink()
        self.assertTrue(display_settings.uses_portable_profile(root))
        with self.assertRaises(ValueError):display_settings.options_path(root)

    def test_self_consistent_foreign_sdk_cannot_be_adopted(self):
        root=self.prepare();sdk=root/'Base/Bin/steam_api.dll';sdk.write_bytes(b'foreign SDK')
        record=json.loads((root/platform.RECORD).read_text())
        record['files']['Base/Bin/steam_api.dll']=content.digest(sdk)
        (root/platform.RECORD).write_text(json.dumps(record))
        with self.assertRaises(ValueError):platform.load(root)

    def test_package_rejects_missing_or_self_consistent_foreign_policy_payload_before_copy(self):
        f=self.fixture
        helper=f.package/'Payload/Platform/weplay_network_policy.exe'
        helper.write_bytes(b'foreign policy');self.manifest()
        with self.assertRaises(ValueError):self.prepare()
        self.assertFalse(f.destination.exists())
        helper.unlink();self.manifest()
        with self.assertRaises(ValueError):self.prepare()
        self.assertFalse(f.destination.exists())

    def test_child_environment_strips_platform_variables_without_touching_callers_settings(self):
        before={'SteamAppId':'invented','steamFoo':'invented','UBISOFT_CACHE':'invented','UplayId':'invented',
                'PATH':'authored path','WEPLAY_SETTING':'local'}
        self.assertEqual(isolated_process.child_environment(before),{'PATH':'authored path','WEPLAY_SETTING':'local'})
        self.assertEqual(before['SteamAppId'],'invented')

    def test_gold_dxvk_config_survives_working_directory_changes_and_relocation(self):
        inherited={'DXVK_CONFIG':'d3d9.floatEmulation = False','DxVk_CoNfIg_FiLe':'foreign.conf',
                   'DXVK_LOG_PATH':'private logs','PATH':'authored path'}
        for name in ('OriginalCopy','Relocated Gold Copy'):
            exe=self.fixture.root/name/'Data/Base/_Dbg/Bin/Release/Settlers7R.exe'
            result=platform.launch_environment({'edition':'gold'},exe,inherited)
            self.assertEqual(result,{'DXVK_CONFIG_FILE':str(exe.with_name('dxvk.conf').resolve()),
                                     'DXVK_LOG_PATH':'private logs','PATH':'authored path'})
        self.assertEqual(inherited['DXVK_CONFIG'],'d3d9.floatEmulation = False')
        self.assertEqual(platform.launch_environment({'edition':'history'},exe,inherited),inherited)

    def test_lease_configuration_rejects_public_addresses_noncanonical_values_and_unknown_fields(self):
        good={'group':False,'local':'192.168.1.2','coordinator':'192.168.1.3','peer':'192.168.1.4'}
        self.assertEqual(network_peer_policy.configuration(good),good)
        for changed in ({**good,'peer':'8.8.8.8'},{**good,'peer':'127.0.0.1'},
                        {**good,'peer':'192.168.1.0'},{**good,'peer':'192.168.01.4'},
                        {**good,'group':'false'},{**good,'extra':'value'}):
            with self.assertRaises(ValueError):
                network_peer_policy.configuration(changed)

    def test_desktop_repair_preserves_portable_progress_and_refuses_to_replace_a_newer_target_save(self):
        import desktop_backend
        root=self.prepare();f=self.fixture
        save=platform.options_path(root).parent/'Saves/progress.s7s';save.parent.mkdir();save.write_bytes(b'current progress')
        fresh=f.root/'Fresh';fresh.mkdir()
        target=fresh/'LocalData/ProfileRoot/THE SETTLERS 7 - History Edition/Saves/progress.s7s'
        dummy=SimpleNamespace(emit=lambda *a,**k:None)
        dummy.preference_bytes=lambda old:desktop_backend.Desktop.preference_bytes(dummy,old)
        desktop_backend.Desktop.transfer_preferences(dummy,root,fresh)
        self.assertEqual(target.read_bytes(),b'current progress')
        target.write_bytes(b'newer independently created progress')
        with self.assertRaises(ValueError):desktop_backend.Desktop.transfer_preferences(dummy,root,fresh)
        self.assertEqual(target.read_bytes(),b'newer independently created progress')


if __name__=='__main__':unittest.main()
