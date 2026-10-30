import os
import json
from pathlib import Path
import sys
import subprocess
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/offline-ui')]
import launch_auto_lan as launcher
from lobby_directory import LanInterface, LobbyEndpoint, LobbyRoom


class AutomaticLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT'))
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
        executable = self.root / 'Base/Bin/Settlers7R.exe'
        executable.parent.mkdir(parents=True)
        executable.write_bytes(b'invented automatic LAN game')
        digest_patch = patch.object(launcher.launch_local, 'EXE_HASH', launcher.launch_local.digest(executable))
        digest_patch.start(); self.addCleanup(digest_patch.stop)
        self.interface = LanInterface('192.168.1.10', 24)
        self.args = SimpleNamespace(bundle=self.root, check_only=False)
        self.mocks = {}
        for target, value in [('compatibility.verify', {'content_id': 'a' * 64}),
                              ('launch_local.verify_bundle', None),
                              ('enumerate_interfaces', [self.interface])]:
            item = patch('launch_auto_lan.' + target, return_value=value)
            self.mocks[target] = item.start(); self.addCleanup(item.stop)

    def test_cancel_holds_lock_and_starts_nothing(self):
        def cancel(root, *_):
            with launcher.session_lock(root): pass
        with patch.object(launcher, '_fixture_picker', create=True, side_effect=cancel), \
                patch.object(launcher, 'start_hub') as hub:
            with self.assertRaises(RuntimeError): launcher.run(self.args,picker=launcher._fixture_picker)
            hub.assert_not_called()
        with patch.object(launcher, '_fixture_picker', create=True, return_value=None), \
                patch.object(launcher.subprocess, 'Popen') as game:
            self.assertEqual(launcher.run(self.args,picker=launcher._fixture_picker), 0)
            game.assert_not_called()

    def test_check_only_has_no_dialog_or_children(self):
        self.args.check_only = True
        with patch.object(launcher, '_fixture_picker', create=True) as choose, patch.object(launcher.subprocess, 'Popen') as child:
            self.assertEqual(launcher.run(self.args), 0)
            choose.assert_not_called(); child.assert_not_called()

    def test_hub_entrypoint_boots_with_isolated_python_module_search(self):
        result = subprocess.run([sys.executable, '-I', '-B', str(ROOT / 'tools/installer/lan_hub.py'), '--help'],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertIn(b'--bundle', result.stdout)

    def test_steam_group_admission_precedes_configuration_and_game_without_killing_hub(self):
        choice = dict(action='create', network=0, display_name='Anna')
        order = []
        membership = Mock()
        membership.start.side_effect = lambda: order.append('admit')
        membership.close.side_effect = lambda: order.append('leave')
        game = Mock(); game.wait.return_value = 0
        runtime = self.root / 'LocalData/s7-local-runtime.ini'
        runtime.parent.mkdir()
        (runtime.parent / 'graphics-choice.json').write_text(json.dumps({
            'format': 's7-graphics-choice-v1', 'choice': None, 'simple_menu': True}))
        def write(*args, **kwargs):
            order.append('config'); runtime.write_bytes(b'fixture'); return runtime
        def spawn(*args, **kwargs):
            self.assertEqual(args[0], [str(self.root / 'Base/Bin/Settlers7R.exe'),
                                      '-vSynch', '-noMapInMainMenu'])
            self.assertEqual(kwargs['env'], {'SteamAppId': '965320', 'SteamGameId': '965320'})
            order.append('game'); return game
        with patch.object(launcher.launch_local, 'STEAM_EXE_HASH', launcher.launch_local.EXE_HASH), \
                patch.object(launcher, '_fixture_picker', create=True, return_value=choice), \
                patch.object(launcher.launch_local, 'choose_graphics_environment', return_value={}), \
                patch.object(launcher, 'start_hub') as hub, \
                patch.object(launcher, 'Membership', return_value=membership), \
                patch.object(launcher.launch_lan, 'remote_configuration', return_value={'content_id': 'a' * 64}), \
                patch.object(launcher.launch_local, 'write_configuration', side_effect=write) as config, \
                patch.object(launcher.subprocess, 'Popen', side_effect=spawn):
            self.assertEqual(launcher.run(self.args,picker=launcher._fixture_picker), 0)
        self.assertEqual(order, ['admit', 'config', 'game', 'leave'])
        self.assertEqual(config.call_args.kwargs['lan_group'], self.interface.address)
        self.assertEqual(config.call_args.kwargs['display_name'], 'Anna')
        self.assertNotIn('lan', config.call_args.kwargs)
        self.assertFalse(runtime.exists())
        hub.return_value.terminate.assert_not_called()

    def test_join_does_not_start_coordinator_and_leaves_on_configuration_error(self):
        choice = dict(action='join', network=0, display_name='Anna', endpoint=LobbyEndpoint('lan-direct', '192.168.1.20'))
        membership = Mock()
        with patch.object(launcher, '_fixture_picker', create=True, return_value=choice), \
                patch.object(launcher.launch_local, 'choose_graphics_environment', return_value={}), \
                patch.object(launcher, 'start_hub') as hub, \
                patch.object(launcher, 'Membership', return_value=membership), \
                patch.object(launcher.launch_lan, 'remote_configuration', return_value={'content_id': 'b' * 64}), \
                patch.object(launcher.subprocess, 'Popen') as game:
            with self.assertRaises(ValueError): launcher.run(self.args,picker=launcher._fixture_picker)
        hub.assert_not_called(); game.assert_not_called(); membership.close.assert_called_once()

    def test_desktop_picker_and_saved_graphics_are_used_without_second_dialog(self):
        self.args.saved_graphics=True
        chosen=dict(action='create',network=0,display_name='Anna')
        picker=Mock(return_value=chosen);membership=Mock();game=Mock();game.wait.return_value=0
        runtime=self.root/'LocalData/s7-local-runtime.ini'
        def configure(*args,**kwargs):runtime.write_bytes(b'fixture');return runtime
        with patch('lobby_picker.choose') as old_picker, \
                patch.object(launcher.launch_local,'choose_graphics_environment') as old_graphics, \
                patch.object(launcher.launch_local,'saved_graphics_environment',return_value={'DESKTOP':'saved'}), \
                patch.object(launcher.launch_local,'graphics_game_arguments',return_value=('-vSynch',)), \
                patch.object(launcher,'start_hub'),patch.object(launcher,'Membership',return_value=membership), \
                patch.object(launcher.launch_lan,'remote_configuration',return_value={'content_id':'a'*64}), \
                patch.object(launcher.launch_local,'write_configuration',side_effect=configure), \
                patch.object(launcher.subprocess,'Popen',return_value=game) as child:
            self.assertEqual(launcher.run(self.args,picker=picker),0)
        old_picker.assert_not_called();old_graphics.assert_not_called();picker.assert_called_once()
        self.assertEqual(child.call_args.args[0][1:],['-vSynch'])
        self.assertEqual(child.call_args.kwargs['env'],{'DESKTOP':'saved'})

    def test_default_path_has_no_picker_and_preserves_discovered_network_and_name(self):
        other = LanInterface('10.20.1.10', 24, 'Kabel')
        selected = LobbyRoom('c' * 32, 'WePlay LAN', 'a' * 64,
            LobbyEndpoint('lan-direct', '10.20.1.20'), other)
        self.args.player_name = 'Spielerin'
        self.args.saved_graphics = True
        self.args.progress = Mock(); self.args.cancel_check = Mock()
        self.args.before_game = Mock(); self.args.game_monitor = Mock()
        runtime = self.root / 'LocalData/s7-local-runtime.ini'
        def configure(*args, **kwargs):
            runtime.write_text('fixture'); return runtime
        with patch('lobby_picker.choose') as picker, \
                patch.object(launcher.lan_autoconnect, 'connect', return_value=selected) as connect, \
                patch.object(launcher, 'start_hub') as hub, \
                patch.object(launcher.launch_local, 'saved_graphics_environment', return_value={}), \
                patch.object(launcher.launch_local, 'choose_graphics_environment') as graphics, \
                patch.object(launcher.launch_local, 'graphics_game_arguments', return_value=()), \
                patch.object(launcher, 'Membership') as membership, \
                patch.object(launcher.launch_lan, 'remote_configuration', return_value={'content_id': 'a' * 64}), \
                patch.object(launcher.launch_local, 'write_configuration', side_effect=configure) as config, \
                patch.object(launcher.launch_local, 'wait_for_graphics_game', return_value=0) as wait, \
                patch.object(launcher.subprocess, 'Popen'):
            self.assertEqual(launcher.run(self.args), 0)
        self.mocks['compatibility.verify'].assert_called_once_with(self.root)
        picker.assert_not_called(); graphics.assert_not_called(); hub.assert_not_called()
        membership.assert_called_once_with(self.root, '10.20.1.10', '10.20.1.20', 'a' * 64)
        self.assertEqual(config.call_args.kwargs['display_name'], 'Spielerin')
        self.args.before_game.assert_called_once()
        self.assertIs(wait.call_args.kwargs['monitor'], self.args.game_monitor)
        self.assertIs(connect.call_args.kwargs['cancel_check'], self.args.cancel_check)
        self.args.progress.assert_any_call('lan_ready')
        self.assertFalse(runtime.exists())

    def test_cancel_after_admission_leaves_group_and_starts_no_game(self):
        selected = LobbyRoom('c' * 32, 'WePlay LAN', 'a' * 64,
            LobbyEndpoint('lan-direct', '192.168.1.20'), self.interface)
        self.args.saved_graphics = True
        checks = [None, None, None, RuntimeError('cancelled')]
        self.args.cancel_check = Mock(side_effect=checks)
        with patch.object(launcher.lan_autoconnect, 'connect', return_value=selected), \
                patch.object(launcher.launch_local, 'saved_graphics_environment', return_value={}), \
                patch.object(launcher.launch_local, 'graphics_game_arguments', return_value=()), \
                patch.object(launcher, 'Membership') as membership, \
                patch.object(launcher.subprocess, 'Popen') as game:
            with self.assertRaisesRegex(RuntimeError, 'cancelled'):
                launcher.run(self.args)
        membership.return_value.start.assert_called_once()
        membership.return_value.close.assert_called_once()
        game.assert_not_called()

    def test_full_content_verification_owns_the_copy_lock(self):
        def verify(root):
            with self.assertRaises(RuntimeError):
                with launcher.session_lock(root):
                    self.fail('Concurrent installation could change verified files')
            return {'content_id': 'a' * 64}
        self.mocks['compatibility.verify'].side_effect = verify
        self.assertEqual(launcher.run(self.args, picker=lambda *_: None), 0)


if __name__ == '__main__': unittest.main()
