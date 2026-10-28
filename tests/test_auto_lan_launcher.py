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
from lobby_directory import LanInterface, LobbyEndpoint


class AutomaticLauncherTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT'))
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)
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
        with patch.object(launcher, 'choose', side_effect=cancel), \
                patch.object(launcher, 'start_hub') as hub:
            with self.assertRaises(RuntimeError): launcher.run(self.args)
            hub.assert_not_called()
        with patch.object(launcher, 'choose', return_value=None), \
                patch.object(launcher.subprocess, 'Popen') as game:
            self.assertEqual(launcher.run(self.args), 0)
            game.assert_not_called()

    def test_check_only_has_no_dialog_or_children(self):
        self.args.check_only = True
        with patch.object(launcher, 'choose') as choose, patch.object(launcher.subprocess, 'Popen') as child:
            self.assertEqual(launcher.run(self.args), 0)
            choose.assert_not_called(); child.assert_not_called()

    def test_hub_entrypoint_boots_with_isolated_python_module_search(self):
        result = subprocess.run([sys.executable, '-I', '-B', str(ROOT / 'tools/installer/lan_hub.py'), '--help'],
                                capture_output=True, timeout=10)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        self.assertIn(b'--bundle', result.stdout)

    def test_group_admission_precedes_configuration_and_game_without_killing_hub(self):
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
            order.append('game'); return game
        with patch.object(launcher, 'choose', return_value=choice), \
                patch.object(launcher.launch_local, 'choose_graphics_environment', return_value={}), \
                patch.object(launcher, 'start_hub') as hub, \
                patch.object(launcher, 'Membership', return_value=membership), \
                patch.object(launcher.launch_lan, 'remote_configuration', return_value={'content_id': 'a' * 64}), \
                patch.object(launcher.launch_local, 'write_configuration', side_effect=write) as config, \
                patch.object(launcher.subprocess, 'Popen', side_effect=spawn):
            self.assertEqual(launcher.run(self.args), 0)
        self.assertEqual(order, ['admit', 'config', 'game', 'leave'])
        self.assertEqual(config.call_args.kwargs['lan_group'], self.interface.address)
        self.assertEqual(config.call_args.kwargs['display_name'], 'Anna')
        self.assertNotIn('lan', config.call_args.kwargs)
        self.assertFalse(runtime.exists())
        hub.return_value.terminate.assert_not_called()

    def test_join_does_not_start_coordinator_and_leaves_on_configuration_error(self):
        choice = dict(action='join', network=0, display_name='Anna', endpoint=LobbyEndpoint('lan-direct', '192.168.1.20'))
        membership = Mock()
        with patch.object(launcher, 'choose', return_value=choice), \
                patch.object(launcher.launch_local, 'choose_graphics_environment', return_value={}), \
                patch.object(launcher, 'start_hub') as hub, \
                patch.object(launcher, 'Membership', return_value=membership), \
                patch.object(launcher.launch_lan, 'remote_configuration', return_value={'content_id': 'b' * 64}), \
                patch.object(launcher.subprocess, 'Popen') as game:
            with self.assertRaises(ValueError): launcher.run(self.args)
        hub.assert_not_called(); game.assert_not_called(); membership.close.assert_called_once()


if __name__ == '__main__': unittest.main()
