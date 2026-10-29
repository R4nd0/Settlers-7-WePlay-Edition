import json
from pathlib import Path
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/offline-ui')]
import launch_internet as launch
from internet_room import Room, InternetError
from lobby_directory import LanInterface

ROOM = Room('a' * 32, 'b' * 64, '10.203.77.0/24')


class LaunchTests(unittest.TestCase):
    def setUp(self):
        self.args = SimpleNamespace(bundle=ROOT / 'work/fictional-internet-copy', player_name='Anna',
                                    saved_graphics=True)
        self.overlay = Mock()
        self.host_interface = LanInterface(ROOM.coordinator, 24, 'WePlay-id')
        self.overlay.start.return_value = self.host_interface
        self.overlay.snapshot.return_value = self.host_interface, {}
        self.factory = Mock(return_value=self.overlay)
        self.physical = patch.object(launch, 'enumerate_interfaces', return_value=[LanInterface('192.168.1.7', 24)])
        self.physical.start(); self.addCleanup(self.physical.stop)

    def test_host_uses_only_overlay_and_emits_private_invitation_after_readiness(self):
        event = Mock()
        def run(args, *, picker, network):
            self.overlay.start.assert_not_called(); event.assert_not_called()
            interfaces, directory = network()
            self.assertEqual(interfaces, [self.host_interface])
            self.assertEqual(directory.interfaces, (self.host_interface,))
            self.assertEqual(picker(None, directory, interfaces, None)['action'], 'create')
            args.before_game()
            return 0
        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                patch.object(launch, 'request', return_value={'active_members': 0}):
            self.assertEqual(launch.run(self.args, invitation_ready=event, overlay_factory=self.factory), 0)
        event.assert_called_once_with(ROOM.token())
        self.overlay.close.assert_called_once()
        self.assertTrue(self.factory.call_args.kwargs['host'])

    def test_guest_cannot_elect_or_create_a_physical_lan_coordinator(self):
        self.args.invitation = ROOM.token()
        guest = LanInterface('10.203.77.2', 24, 'WePlay-id')
        self.overlay.start.return_value = guest
        self.overlay.snapshot.return_value = guest, {ROOM.coordinator: 'p2p'}
        def run(args, *, picker, network):
            interfaces, directory = network()
            selected = picker(None, directory, interfaces, None)
            self.assertEqual(selected['action'], 'join')
            self.assertEqual(selected['endpoint'].host, ROOM.coordinator)
            args.before_game()
            return 0
        event = Mock()
        with patch.object(launch.launch_auto_lan, 'run', side_effect=run), patch.object(launch, 'request') as request:
            self.assertEqual(launch.run(self.args, invitation_ready=event, overlay_factory=self.factory), 0)
        request.assert_not_called(); event.assert_not_called()
        self.assertFalse(self.factory.call_args.kwargs['host'])
        self.overlay.close.assert_called_once()

    def test_creator_keeps_overlay_until_guests_leave_after_own_game_exit(self):
        progress = self.args.progress = Mock()
        def run(args, *, picker, network): network(); return 0
        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                patch.object(launch, 'request', side_effect=[{'active_members': 1}, {'active_members': 0}]), \
                patch.object(launch.time, 'sleep'):
            self.assertEqual(launch.run(self.args, overlay_factory=self.factory), 0)
        progress.assert_called_once_with('internet_hosting')
        self.overlay.snapshot.assert_called_once()
        self.overlay.close.assert_called_once()

    def test_game_start_failure_closes_only_owned_overlay(self):
        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=RuntimeError('fixture')):
            with self.assertRaisesRegex(RuntimeError, 'fixture'):
                launch.run(self.args, overlay_factory=self.factory)
        self.overlay.close.assert_called_once()

    def test_renderer_preflight_finishes_before_full_launcher_verification(self):
        preflight=Mock()
        def run(args,*,picker,network):
            preflight.assert_called_once_with(self.host_interface,ROOM.coordinator,True)
            self.overlay.start.assert_called_once()
            self.assertEqual(network()[0],[self.host_interface])
            return 0
        with patch.object(launch,'create',return_value=ROOM), \
                patch.object(launch.launch_auto_lan,'run',side_effect=run), \
                patch.object(launch,'request',return_value={'active_members':0}):
            self.assertEqual(launch.run(self.args,network_ready=preflight,overlay_factory=self.factory),0)

    def test_guest_refuses_stale_or_changed_interface_before_game(self):
        self.args.invitation = ROOM.token()
        self.overlay.snapshot.return_value = self.host_interface, {}
        def run(args, *, picker, network): network(); args.before_game()
        with patch.object(launch.launch_auto_lan, 'run', side_effect=run):
            with self.assertRaises(InternetError): launch.run(self.args, overlay_factory=self.factory)
        self.overlay.close.assert_called_once()


if __name__ == '__main__': unittest.main()
