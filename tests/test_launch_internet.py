import json
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/offline-ui')]
import launch_internet as launch
from internet_room import Room, InternetError
from lobby_directory import CONTROL_FORMAT, MAX_MEMBERS, LanInterface

ROOM = Room('a' * 32, 'b' * 64, '10.203.77.0/24')
EXPECTED = 'c' * 64


def status(active, *, expected=EXPECTED):
    return dict(format=CONTROL_FORMAT, content_id=expected, members=max(1, active),
                active_members=active, max_members=MAX_MEMBERS)


class LaunchTests(unittest.TestCase):
    def setUp(self):
        self.args = SimpleNamespace(bundle=ROOT / 'work/fictional-internet-copy', player_name='Anna',
                                    saved_graphics=True,internet_consent=1)
        self.overlay = Mock()
        self.host_interface = LanInterface(ROOM.coordinator, 24, 'WePlay-id')
        self.overlay.start.return_value = self.host_interface
        self.overlay.snapshot.return_value = self.host_interface, {}
        self.factory = Mock(return_value=self.overlay)
        self.physical = patch.object(launch, 'enumerate_interfaces', return_value=[LanInterface('192.168.1.7', 24)])
        self.physical.start(); self.addCleanup(self.physical.stop)

    def game_ready(self, args, picker, network):
        interfaces, directory = network()
        picker(None, directory, interfaces, EXPECTED)
        args.before_game()

    def test_lost_private_tunnel_is_reported_while_game_remains_running(self):
        self.args.invitation = ROOM.token()
        interface = LanInterface('10.203.77.2', 24, 'WePlay-id')
        self.overlay.start.return_value = interface
        self.overlay.snapshot.return_value = interface, {ROOM.coordinator: 'p2p'}
        self.args.game_monitor = Mock(return_value=Mock())
        game = Mock(poll=Mock(return_value=None))
        warn = self.args.warn = Mock()

        def wait(*, timeout=None):
            if timeout is not None and game.wait.call_count <= 2:
                self.overlay.alive.return_value = False
                raise subprocess.TimeoutExpired('fixture game', timeout)
            if timeout is not None:
                warn.assert_called_once_with('internet_disconnected')
                self.overlay.close.assert_not_called()
            game.poll.return_value = 0
            return 0

        game.wait.side_effect = wait

        def run(args, *, picker, network):
            self.game_ready(args, picker, network)
            during_game = getattr(args, 'during_game', None)
            options = {'during_game': during_game} if during_game is not None else {}
            return launch.launch_auto_lan.launch_local.wait_for_graphics_game(
                args.bundle, game, enabled=False, monitor=args.game_monitor, **options)

        with patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                self.assertRaisesRegex(InternetError, '^internet_disconnected$'):
            launch.run(self.args, overlay_factory=self.factory)
        warn.assert_called_once_with('internet_disconnected')
        self.args.game_monitor.assert_called_once_with(game)
        self.args.game_monitor.return_value.close.assert_called_once()
        game.terminate.assert_not_called()
        self.overlay.close.assert_called_once()

    def test_missing_or_unchecked_consent_starts_no_overlay(self):
        for value in (None,False,True,0,'1',2):
            self.args.internet_consent=value
            with self.assertRaisesRegex(InternetError,'internet_consent_required'):
                launch.run(self.args,overlay_factory=self.factory)
        self.factory.assert_not_called()

    def test_private_room_can_start_from_an_uplink_without_private_ipv4(self):
        import lan_interfaces
        def run(args, *, picker, network):
            self.game_ready(args, picker, network)
            return 0
        with patch.object(launch, 'enumerate_interfaces', lan_interfaces.enumerate_interfaces), \
                patch.object(lan_interfaces.sys, 'platform', 'win32'), \
                patch('windows_system.command', return_value=b'[]'), \
                patch.object(launch, 'create', return_value=ROOM) as create, \
                patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                patch.object(launch, 'request', return_value=status(0)):
            self.assertEqual(launch.run(self.args, overlay_factory=self.factory), 0)
        self.assertEqual(create.call_args.args[0], [])
        self.overlay.start.assert_called_once()

    def test_host_uses_only_overlay_and_emits_private_invitation_after_readiness(self):
        event = Mock()
        def run(args, *, picker, network):
            self.overlay.start.assert_not_called(); event.assert_not_called()
            interfaces, directory = network()
            self.assertEqual(interfaces, [self.host_interface])
            self.assertEqual(directory.interfaces, (self.host_interface,))
            self.assertEqual(picker(None, directory, interfaces, EXPECTED)['action'], 'create')
            event.assert_not_called()
            args.before_game()
            return 0
        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                patch.object(launch, 'request', return_value=status(0)):
            self.assertEqual(launch.run(self.args, invitation_ready=event, overlay_factory=self.factory), 0)
        event.assert_called_once_with(ROOM.token())
        self.overlay.close.assert_called_once()
        self.assertTrue(self.factory.call_args.kwargs['host'])

    def test_coordinator_failure_does_not_expose_invitation_or_ready(self):
        event=Mock();progress=self.args.progress=Mock()
        def run(args,*,picker,network):
            network();event.assert_not_called()
            raise OSError('coordinator unreachable')
        with patch.object(launch,'create',return_value=ROOM), \
                patch.object(launch.launch_auto_lan,'run',side_effect=run):
            with self.assertRaises(OSError):
                launch.run(self.args,invitation_ready=event,overlay_factory=self.factory)
        event.assert_not_called();progress.assert_not_called()
        self.overlay.close.assert_called_once()

    def test_before_game_failure_does_not_report_ready_or_expose_invitation(self):
        event=Mock();progress=self.args.progress=Mock()
        self.args.before_game=Mock(side_effect=OSError('preflight failed'))
        def run(args,*,picker,network):
            network();args.before_game()
        with patch.object(launch,'create',return_value=ROOM), \
                patch.object(launch.launch_auto_lan,'run',side_effect=run):
            with self.assertRaises(OSError):
                launch.run(self.args,invitation_ready=event,overlay_factory=self.factory)
        event.assert_not_called();progress.assert_not_called()
        self.overlay.close.assert_called_once()

    def test_default_route_flag_change_does_not_invalidate_same_private_adapter(self):
        import dataclasses
        self.overlay.snapshot.return_value = dataclasses.replace(self.host_interface, default_route=True), {}
        def run(args, *, picker, network):
            self.game_ready(args, picker, network)
            return 0
        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                patch.object(launch, 'request', return_value=status(0)):
            self.assertEqual(launch.run(self.args, overlay_factory=self.factory), 0)

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
        def run(args, *, picker, network):
            self.game_ready(args, picker, network)
            return 0
        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                patch.object(launch, 'request', side_effect=[status(1), status(0)]), \
                patch.object(launch.time, 'sleep'):
            self.assertEqual(launch.run(self.args, overlay_factory=self.factory), 0)
        self.assertEqual([call.args[0] for call in progress.call_args_list], ['internet_ready', 'internet_hosting'])
        self.assertEqual(self.overlay.snapshot.call_count, 2)
        self.overlay.close.assert_called_once()

    def test_creator_rejects_changed_tunnel_binding_while_hosting_after_own_game_exit(self):
        changed = LanInterface(ROOM.coordinator, 24, 'Different device')
        self.overlay.snapshot.side_effect = [(self.host_interface, {}), (changed, {})]
        def run(args, *, picker, network):
            self.game_ready(args, picker, network)
            return 0
        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                patch.object(launch, 'request', side_effect=[status(1), status(0)]), \
                patch.object(launch.time, 'sleep'), \
                self.assertRaisesRegex(InternetError, '^internet_disconnected$'):
            launch.run(self.args, overlay_factory=self.factory)
        self.overlay.close.assert_called_once()

    def test_creator_rejects_an_unrelated_coordinator_after_own_game_exit(self):
        def run(args, *, picker, network):
            self.game_ready(args, picker, network)
            return 0

        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                patch.object(launch, 'request', side_effect=[status(1, expected='d' * 64), status(0)]) as request, \
                patch.object(launch.time, 'sleep'), \
                self.assertRaisesRegex(InternetError, '^internet_status_invalid$'):
            launch.run(self.args, overlay_factory=self.factory)
        request.assert_called_once_with(ROOM.coordinator, self.host_interface.address, 'status',
                                        expected=EXPECTED, timeout=2)
        self.overlay.close.assert_called_once()

    def test_creator_survives_transient_coordinator_status_failures(self):
        def run(args, *, picker, network):
            self.game_ready(args, picker, network)
            return 0

        responses = [OSError(), OSError(), status(1), OSError(), OSError(), status(0)]
        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                patch.object(launch, 'request', side_effect=responses) as request, \
                patch.object(launch.time, 'sleep'):
            self.assertEqual(launch.run(self.args, overlay_factory=self.factory), 0)
        self.assertEqual(request.call_count, len(responses))
        self.assertEqual(self.overlay.snapshot.call_count, 2)
        self.overlay.close.assert_called_once()

    def test_creator_reports_persistent_coordinator_failure(self):
        def run(args, *, picker, network):
            self.game_ready(args, picker, network)
            return 0

        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                patch.object(launch, 'request', side_effect=OSError()) as request, \
                patch.object(launch.time, 'sleep'):
            with self.assertRaisesRegex(InternetError, 'internet_disconnected'):
                launch.run(self.args, overlay_factory=self.factory)
        self.assertEqual(request.call_count, 3)
        self.overlay.close.assert_called_once()

    def test_failed_game_retains_exit_code_after_idle_coordinator_is_stopped(self):
        def run(args, *, picker, network):
            self.game_ready(args, picker, network)
            return 1

        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=run), \
                patch.object(launch, 'request', side_effect=ConnectionRefusedError):
            self.assertEqual(launch.run(self.args, overlay_factory=self.factory), 1)
        self.overlay.close.assert_called_once()

    def test_cleanup_failure_preserves_original_start_diagnostic(self):
        primary = OSError('native start failed')
        self.overlay.close.side_effect = InternetError('internet_cleanup_failed')
        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=primary):
            with self.assertRaises(OSError) as error:
                launch.run(self.args, overlay_factory=self.factory)
        self.assertIs(error.exception, primary)
        self.assertEqual(primary.__notes__, ['internet_cleanup_failed'])

    def test_python310_cleanup_keeps_native_start_error(self):
        class Python310Error(OSError):
            def __getattribute__(self, name):
                if name == 'add_note':
                    raise AttributeError(name)
                return super().__getattribute__(name)

        primary = Python310Error('native start failed')
        self.overlay.close.side_effect = InternetError('internet_cleanup_failed')
        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=primary):
            with self.assertRaises(Python310Error) as reported:
                launch.run(self.args, overlay_factory=self.factory)
        self.assertIs(reported.exception, primary)

    def test_game_start_failure_closes_only_owned_overlay(self):
        with patch.object(launch, 'create', return_value=ROOM), \
                patch.object(launch.launch_auto_lan, 'run', side_effect=RuntimeError('fixture')):
            with self.assertRaisesRegex(RuntimeError, 'fixture'):
                launch.run(self.args, overlay_factory=self.factory)
        self.overlay.close.assert_called_once()

    def test_explicit_browser_switch_closes_old_room_only_after_game_exit(self):
        switch=self.args.switch_requested=Mock(return_value=True)
        def run(args,*,picker,network):
            self.game_ready(args, picker, network)
            switch.assert_not_called()
            return 0
        with patch.object(launch,'create',return_value=ROOM), \
                patch.object(launch.launch_auto_lan,'run',side_effect=run), \
                patch.object(launch,'request') as request:
            self.assertEqual(launch.run(self.args,overlay_factory=self.factory),0)
        switch.assert_called_once();request.assert_not_called();self.overlay.close.assert_called_once()

    def test_renderer_preflight_finishes_before_full_launcher_verification(self):
        preflight=Mock()
        def run(args,*,picker,network):
            preflight.assert_called_once_with(self.host_interface,ROOM.coordinator,True)
            self.overlay.start.assert_called_once()
            self.assertEqual(network()[0],[self.host_interface])
            return 0
        with patch.object(launch,'create',return_value=ROOM), \
                patch.object(launch.launch_auto_lan,'run',side_effect=run), \
                patch.object(launch,'request') as request:
            self.assertEqual(launch.run(self.args,network_ready=preflight,overlay_factory=self.factory),0)
        request.assert_not_called()

    def test_guest_refuses_stale_or_changed_interface_before_game(self):
        self.args.invitation = ROOM.token()
        self.overlay.snapshot.return_value = self.host_interface, {}
        def run(args, *, picker, network): network(); args.before_game()
        with patch.object(launch.launch_auto_lan, 'run', side_effect=run):
            with self.assertRaises(InternetError): launch.run(self.args, overlay_factory=self.factory)
        self.overlay.close.assert_called_once()


if __name__ == '__main__': unittest.main()
