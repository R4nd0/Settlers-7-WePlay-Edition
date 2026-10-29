"""Update IPC boundaries and compatibility with existing managed installations."""
import json
import os
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(Path(__file__).parent)]
import desktop_backend as backend
import update_client
import test_desktop_backend as fixtures


class UpdateBackendTests(unittest.TestCase):
    def desktop(self):
        return SimpleNamespace(app=ROOT/'work/update-backend-fixture',package=ROOT/'work/update-backend-fixture/Package',
            read=Mock(return_value={'update_patcher':True}),progress=Mock())

    def test_startup_network_check_does_not_hold_game_operation_lock(self):
        desktop=SimpleNamespace(check_updates=Mock(return_value={'available':False}),
            lock=Mock(side_effect=AssertionError('startup check locked out the game')))
        self.assertEqual(backend.dispatch(desktop,dict(format=backend.REQUEST,action='update-check',data={})),{'available':False})
        desktop.lock.assert_not_called()

    def test_legacy_package_never_silently_gains_update_capability(self):
        desktop=self.desktop();desktop.read.return_value={}
        with patch.object(update_client,'check') as check:
            with self.assertRaisesRegex(ValueError,'update_unavailable'):backend.Desktop.check_updates(desktop)
        check.assert_not_called()

    def test_only_verified_launcher_parent_can_prepare_handoff(self):
        desktop=self.desktop()
        with patch.object(backend.graphics,'ensure_closed'),patch.object(update_client,'process_info',
                return_value={'pid':os.getppid(),'path':str(ROOT/'work/other.exe'),'started_ticks':123}), \
                patch.object(update_client,'prepare') as prepare:
            with self.assertRaisesRegex(ValueError,'update_parent_invalid'):
                backend.Desktop.install_update(desktop,{'sequence':2})
        prepare.assert_not_called()

    def test_selected_release_and_exact_process_token_reach_the_verified_handoff(self):
        desktop=self.desktop();parent=dict(pid=os.getppid(),path=str(desktop.app/'S7Launcher.exe'),started_ticks=123456)
        with patch.object(backend.graphics,'ensure_closed'),patch.object(update_client,'process_info',return_value=parent), \
                patch.object(update_client,'prepare',return_value={'job':'private-owned-job','version':'2.0.0-rc3.5'}) as prepare, \
                patch.object(update_client,'start_handoff',return_value={'helper_pid':4321,'version':'2.0.0-rc3.5'}) as start:
            result=backend.Desktop.install_update(desktop,{'sequence':2})
        prepare.assert_called_once_with(desktop.app,parent['pid'],parent['started_ticks'],progress=desktop.progress,expected_sequence=2)
        start.assert_called_once_with(desktop.app,'private-owned-job')
        self.assertEqual(result,{'handoff':True,'version':'2.0.0-rc3.5'})

    def test_running_game_is_rejected_before_download_or_process_handoff(self):
        with patch.object(backend.graphics,'ensure_closed',side_effect=ValueError('game running')), \
                patch.object(update_client,'process_info') as process:
            with self.assertRaises(ValueError):backend.Desktop.install_update(self.desktop(),{'sequence':2})
        process.assert_not_called()

    def test_public_request_never_controls_parent_executable_installer_or_arguments(self):
        for value in ({'sequence':True},{'sequence':0},{'sequence':2,'path':'untrusted.exe'},{'sequence':2,'parent_pid':1}):
            with self.assertRaisesRegex(ValueError,'request_invalid'):backend.Desktop.install_update(self.desktop(),value)

    def test_update_failures_remain_fixed_localizable_codes(self):
        self.assertEqual(backend.error_code(update_client.UpdateError('update_signature_invalid')),'update_signature_invalid')

    def test_false_optional_achievement_flag_preserves_legacy_game_contract(self):
        fixture=fixtures.DesktopTests();fixture.setUp();self.addCleanup(fixture.doCleanups)
        desktop=fixture.desktop;before=desktop.package_id()
        path=desktop.package/'package.json';manifest=json.loads(path.read_text())
        manifest['achievement_notifications_disabled']=False;path.write_text(json.dumps(manifest),encoding='utf-8')
        self.assertEqual(desktop.package_id(),before)

    def test_failed_handoff_is_visible_without_returning_private_job_data(self):
        fixture=fixtures.DesktopTests();fixture.setUp();self.addCleanup(fixture.doCleanups)
        desktop=fixture.desktop;path=desktop.package/'package.json';manifest=json.loads(path.read_text())
        manifest['update_patcher']=True;path.write_text(json.dumps(manifest),encoding='utf-8')
        updates=desktop.state/'Updates';updates.mkdir()
        status=updates/'status.json';status.write_text(json.dumps(dict(format='weplay-update-status-v1',phase='failed',
            code='update_install_failed',private_job_value='must not enter snapshot')),encoding='utf-8')
        result=desktop.snapshot()
        self.assertEqual(result['update_last_error'],'update_install_failed')
        self.assertNotIn('must not enter snapshot',repr(result))
        status.write_text('{invalid',encoding='utf-8')
        self.assertEqual(desktop.snapshot()['update_last_error'],'update_invalid','broken update state must not prevent the game UI opening')


if __name__=='__main__':unittest.main()
