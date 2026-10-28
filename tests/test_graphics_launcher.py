"""Launcher integration: cancel before children, child-only environment."""
import os
import json
from pathlib import Path
import sys
import tempfile
import subprocess
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

    def test_installed_graphics_helpers_import_with_isolated_python(self):
        folder = self.root / 'Launcher'; folder.mkdir()
        script = folder / 'launch_local.py'
        script.write_bytes((ROOT / 'tools/offline-ui/launch_local.py').read_bytes())
        (folder / 'graphics_choice.py').write_text(
            "def choose_launch_environment(root): return {'fixture':'selected'}\n"
            "def game_arguments(root): return ('-vSynch','-noMapInMainMenu')\n")
        code = ('import importlib.util,json,sys; '
                'spec=importlib.util.spec_from_file_location("installed_launcher",sys.argv[1]); '
                'm=importlib.util.module_from_spec(spec); spec.loader.exec_module(m); '
                'print(json.dumps([m.choose_graphics_environment(None),m.graphics_game_arguments(None)]))')
        result = subprocess.run([sys.executable, '-I', '-B', '-c', code, str(script)],
                                capture_output=True, timeout=10, creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertEqual(json.loads(result.stdout), [{'fixture': 'selected'}, ['-vSynch', '-noMapInMainMenu']])

    def test_local_uses_selected_environment_only_for_game(self):
        selected = {'S7_TEST_GPU': 'fixture'}
        with patch.object(launch_local, 'choose_graphics_environment', return_value=selected), \
                patch.object(launch_local, 'service_ready', return_value=({}, 9111)), \
                patch.object(launch_local, 'write_configuration', return_value=self.runtime), \
                patch.object(launch_local.subprocess, 'Popen', side_effect=[self.service, self.game]) as child:
            self.assertEqual(launch_local.run(self.args), 0)
        self.assertNotIn('env', child.call_args_list[0].kwargs)
        self.assertIs(child.call_args_list[1].kwargs['env'], selected)
        self.assertEqual(child.call_args_list[1].args[0], [str(self.root / 'Base/Bin/Settlers7R.exe')])
        self.service.terminate.assert_called_once()

    def test_simple_menu_arguments_reach_only_the_owned_local_game(self):
        data = self.root / 'LocalData'; data.mkdir()
        (data / 'graphics-choice.json').write_text(json.dumps({'format': 's7-graphics-choice-v1',
            'choice': None, 'simple_menu': True}))
        with patch.object(launch_local, 'choose_graphics_environment', return_value={}), \
                patch.object(launch_local, 'service_ready', return_value=({}, 9111)), \
                patch.object(launch_local, 'write_configuration', return_value=self.runtime), \
                patch.object(launch_local.subprocess, 'Popen', side_effect=[self.service, self.game]) as child:
            self.assertEqual(launch_local.run(self.args), 0)
        self.assertNotIn('-noMapInMainMenu', child.call_args_list[0].args[0])
        self.assertEqual(child.call_args_list[1].args[0], [str(self.root / 'Base/Bin/Settlers7R.exe'),
                                                       '-vSynch', '-noMapInMainMenu'])

    def test_preflight_never_opens_graphics_dialog(self):
        self.args.launch_game = False
        with patch.object(launch_local, 'choose_graphics_environment') as choose, \
                patch.object(launch_local, 'graphics_game_arguments') as arguments, \
                patch.object(launch_local, 'service_ready', return_value=({}, 9111)), \
                patch.object(launch_local, 'write_configuration', return_value=self.runtime), \
                patch.object(launch_local.subprocess, 'Popen', return_value=self.service) as child:
            self.assertEqual(launch_local.run(self.args), 0)
        choose.assert_not_called()
        arguments.assert_not_called()
        self.assertEqual(child.call_count, 1)

    def test_manual_lan_passes_fixed_menu_arguments_only_to_game(self):
        data = self.root / 'LocalData'; data.mkdir()
        (data / 'graphics-choice.json').write_text(json.dumps({'format': 's7-graphics-choice-v1',
            'choice': None, 'simple_menu': True}))
        args = SimpleNamespace(bundle=str(self.root), local='10.1.2.3', peer='10.1.2.4',
                               role='host', check_only=False)
        selected = {'S7_TEST_GPU': 'fixture'}
        with patch.object(launch_lan.compatibility, 'verify', return_value={'content_id': 'fixture'}), \
                patch.object(launch_lan.socket, 'socket'), \
                patch.object(launch_local, 'choose_graphics_environment', return_value=selected), \
                patch.object(launch_local, 'service_ready', return_value=({}, launch_lan.UDP_PORT)), \
                patch.object(launch_local, 'write_configuration', return_value=self.runtime), \
                patch.object(launch_lan, 'check_content'), \
                patch.object(launch_lan.subprocess, 'Popen', side_effect=[self.service, self.game]) as child:
            self.assertEqual(launch_lan.run(args), 0)
        self.assertNotIn('env', child.call_args_list[0].kwargs)
        self.assertNotIn('-noMapInMainMenu', child.call_args_list[0].args[0])
        self.assertEqual(child.call_args_list[1].args[0], [str(self.root / 'Base/Bin/Settlers7R.exe'),
                                                       '-vSynch', '-noMapInMainMenu'])
        self.assertIs(child.call_args_list[1].kwargs['env'], selected)

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
