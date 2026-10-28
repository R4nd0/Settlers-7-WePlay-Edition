"""Launcher integration: cancel before children, child-only environment."""
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/offline-ui'), str(ROOT / 'tools/installer')]
import launch_local
import launch_lan


class GraphicsLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT'))
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        for name in ('dotnet.exe', 'service.dll'):
            (self.root / name).write_bytes(b'fixture')
        self.args = SimpleNamespace(bundle=str(self.root), dotnet=str(self.root / 'dotnet.exe'),
            server=str(self.root / 'service.dll'), local_connect_probe=True,
            local_session_display_probe=False, launch_game=True)
        self.service = Mock(); self.service.poll.return_value = None
        self.game = Mock(); self.game.wait.return_value = 0
        self.runtime = self.root / 'not-created-runtime'
        self.verify = patch.object(launch_local, 'verify_bundle').start()
        self.addCleanup(patch.stopall)

    def test_local_cancel_starts_no_service_and_holds_lock_during_dialog(self):
        def cancel(root):
            with (root / 'LocalData/session.lock').open('r+b') as lock:
                with self.assertRaises(OSError):
                    launch_local.msvcrt.locking(lock.fileno(), launch_local.msvcrt.LK_NBLCK, 1)
            return None
        with patch.object(launch_local, 'choose_graphics_environment', side_effect=cancel) as choose, \
                patch.object(launch_local.subprocess, 'Popen') as child:
            self.assertEqual(launch_local.run(self.args), 0)
            choose.assert_called_once_with(self.root)
            child.assert_not_called()

    def test_local_uses_selected_environment_only_for_game(self):
        selected = {'S7_TEST_GPU': 'fixture'}
        with patch.object(launch_local, 'choose_graphics_environment', return_value=selected), \
                patch.object(launch_local, 'service_ready', return_value=({}, 9111)), \
                patch.object(launch_local, 'write_configuration', return_value=self.runtime), \
                patch.object(launch_local.subprocess, 'Popen', side_effect=[self.service, self.game]) as child:
            self.assertEqual(launch_local.run(self.args), 0)
        self.assertNotIn('env', child.call_args_list[0].kwargs)
        self.assertIs(child.call_args_list[1].kwargs['env'], selected)
        self.service.terminate.assert_called_once()

    def test_preflight_never_opens_graphics_dialog(self):
        self.args.launch_game = False
        with patch.object(launch_local, 'choose_graphics_environment') as choose, \
                patch.object(launch_local, 'service_ready', return_value=({}, 9111)), \
                patch.object(launch_local, 'write_configuration', return_value=self.runtime), \
                patch.object(launch_local.subprocess, 'Popen', return_value=self.service) as child:
            self.assertEqual(launch_local.run(self.args), 0)
        choose.assert_not_called()
        self.assertEqual(child.call_count, 1)

    def test_lan_cancel_never_starts_host_service_or_connects_to_peer(self):
        args = SimpleNamespace(bundle=str(self.root), local='10.1.2.3', peer='10.1.2.4',
                               role='host', check_only=False)
        with patch.object(launch_lan.compatibility, 'verify', return_value={'content_id': 'fixture'}), \
                patch.object(launch_lan.socket, 'socket'), \
                patch.object(launch_local, 'choose_graphics_environment', return_value=None), \
                patch.object(launch_lan, 'remote_configuration') as remote, \
                patch.object(launch_lan.subprocess, 'Popen') as child:
            self.assertEqual(launch_lan.run(args), 0)
        child.assert_not_called()
        remote.assert_not_called()


if __name__ == '__main__':
    unittest.main()
