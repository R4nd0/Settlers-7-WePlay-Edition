"""Authored files only. Actual loader, image and WFP evidence is separate."""
import hashlib
import json
from pathlib import Path
import shutil
import sys
import threading
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

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
        for name in platform.GOLD_SERVICE_TOOLS:f.write(f.package,'Tools/gold/'+name,(fixtures.TOOLS.parent/'gold'/name).read_bytes())
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

    def test_old_pinned_policy_is_readable_for_repair_but_cannot_prepare_new_copies(self):
        root=self.prepare()
        old=b'invented previous pinned helper'
        legacy_hash=sha(old)
        helper=root/platform.POLICY_FILE
        helper.write_bytes(old)
        record=json.loads((root/platform.RECORD).read_bytes())
        record['files'][platform.POLICY_FILE]=legacy_hash
        (root/platform.RECORD).write_text(json.dumps(record),encoding='utf-8')
        runtime=self.fixture.package/'Payload/Platform'
        (runtime/'weplay_network_policy.exe').write_bytes(old)
        with patch.object(platform,'POLICY_V1',legacy_hash):
            self.assertEqual(platform.load(root)[0]['files'][platform.POLICY_FILE],legacy_hash)
            self.assertEqual(platform.payload(runtime,allow_legacy=True),runtime.resolve())
            with self.assertRaisesRegex(ValueError,'Ungeprüfte Standalone-Laufzeit'):
                platform.payload(runtime)

    def test_known_tcp_isolation_error_refreshes_and_retries_only_once(self):
        from standalone_entry import NETWORK_PROBE_EXIT_PREFIX, NativeStartupError
        root=self.prepare()
        failure=NativeStartupError(11,NETWORK_PROBE_EXIT_PREFIX | (3<<20) | (9<<16))
        game=SimpleNamespace()
        with patch.object(platform.subprocess,'run',return_value=SimpleNamespace(returncode=0)), \
                patch.object(platform,'configure') as configure, \
                patch.object(isolated_process,'launch',side_effect=(failure,game)) as launch:
            self.assertIs(platform.launch(root,environment={}),game)
            self.assertEqual(launch.call_count,2)
            self.assertEqual(launch.call_args_list[0],launch.call_args_list[1])
            configure.assert_called_once_with(root,'refresh')
        with patch.object(platform.subprocess,'run',return_value=SimpleNamespace(returncode=0)), \
                patch.object(platform,'configure') as configure, \
                patch.object(isolated_process,'launch',side_effect=(failure,failure)) as launch:
            with self.assertRaises(NativeStartupError):
                platform.launch(root,environment={})
            self.assertEqual(launch.call_count,2)
            configure.assert_called_once_with(root,'refresh')
        with patch.object(platform.subprocess,'run',return_value=SimpleNamespace(returncode=0)), \
                patch.object(platform,'configure',side_effect=PermissionError) as configure, \
                patch.object(isolated_process,'launch',side_effect=failure) as launch:
            with self.assertRaises(NativeStartupError):
                platform.launch(root,environment={})
            launch.assert_called_once()
            configure.assert_called_once_with(root,'refresh')

    def test_unrelated_native_start_error_does_not_refresh_the_policy(self):
        from standalone_entry import FAILURE_EXIT_BASE,NativeStartupError
        root=self.prepare()
        with patch.object(platform.subprocess,'run',return_value=SimpleNamespace(returncode=0)), \
                patch.object(platform,'configure') as configure, \
                patch.object(isolated_process,'launch',side_effect=NativeStartupError(12,FAILURE_EXIT_BASE+12)) as launch:
            with self.assertRaises(NativeStartupError):
                platform.launch(root,environment={})
            configure.assert_not_called()
            launch.assert_called_once()

    def test_failed_lan_policy_cleanup_preserves_native_stage_and_exit_code(self):
        from standalone_entry import FAILURE_EXIT_BASE, NativeStartupError
        root = self.prepare()
        failure = NativeStartupError(11, FAILURE_EXIT_BASE + 11)
        scope = Mock()
        scope.close.side_effect = TimeoutError()
        lan = dict(group=True, local='192.168.1.2', coordinator='192.168.1.3')
        with patch.object(platform.subprocess, 'run', return_value=SimpleNamespace(returncode=0)), \
                patch.object(network_peer_policy, 'Session', return_value=scope), \
                patch.object(isolated_process, 'launch', side_effect=failure):
            with self.assertRaises(NativeStartupError) as error:
                platform.launch(root, environment={}, lan=lan)
        self.assertIs(error.exception, failure)
        self.assertEqual(error.exception.native_start_stage, 11)
        self.assertEqual(error.exception.native_exit_code, FAILURE_EXIT_BASE + 11)
        scope.close.assert_called_once()

    def test_package_requires_complete_local_gold_service_dependencies(self):
        f=self.fixture
        (f.package/'Tools/gold/account_config.py').unlink();self.manifest()
        with self.assertRaisesRegex(ValueError,'Plattformisolation'):
            self.prepare()
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

    def test_peer_policy_closes_confirmed_failed_worker_and_reaps_owned_files(self):
        session = network_peer_policy.Session.__new__(network_peer_policy.Session)
        session.stop, session.thread = threading.Event(), None
        session.nonce = 'a' * 32
        session.path = self.fixture.root / 'peers.ini'
        session.ready = self.fixture.root / 'peers.ready'
        session.path.write_bytes(f'nonce={session.nonce}\n'.encode('ascii'))
        session.ready.write_bytes(f'{session.nonce}\n'.encode('ascii'))
        session._publish = Mock()
        child = session.process = Mock()
        child.wait.side_effect = [TimeoutError(), 2]

        with self.assertRaises(TimeoutError):
            session.close()
        session._publish.assert_called_once_with(expired=True)
        child.close.assert_not_called()
        self.assertIs(session.process, child)
        self.assertTrue(session.path.exists())

        with self.assertRaisesRegex(RuntimeError, 'Windows-Netzhelfer'):
            session.close()
        session.close()
        child.close.assert_called_once()
        self.assertIsNone(session.process)
        self.assertFalse(session.ready.exists())
        self.assertFalse(session.path.exists())

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
