import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import time
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/linux'))
import linux_elevation as elevation
from internet_room import InternetError, Room
from lobby_directory import LanInterface


class LinuxTunnelTests(unittest.TestCase):
    def setUp(self):
        room = Room('a' * 32, 'b' * 64, '10.203.77.0/24')
        self.overlay = elevation.ElevatedEasyTier(ROOT / 'work/fictional-tunnel-copy', room, host=True)

    def test_request_pipe_closed_after_fast_elevation_exit_is_reaped(self):
        worker = Mock()
        worker.stdin.write.side_effect = BrokenPipeError
        worker.stdin.closed = False
        worker.poll.return_value = 126
        with patch.object(elevation.os, 'geteuid', return_value=1000, create=True), \
                patch('easytier_runtime.verify_runtime'), \
                patch.object(elevation.subprocess, 'Popen', return_value=worker):
            with self.assertRaisesRegex(InternetError, 'internet_elevation_cancelled'):
                self.overlay.start()
        worker.wait.assert_called_once_with(timeout=20)
        worker.stdin.close.assert_called_once()
        worker.stdout.close.assert_called_once()
        self.assertIsNone(self.overlay.child)

    def test_missing_polkit_returns_network_helper_error_without_starting_tunnel(self):
        with patch.object(elevation.os, 'geteuid', return_value=1000, create=True), \
                patch('easytier_runtime.verify_runtime'), \
                patch.object(elevation.subprocess, 'Popen', side_effect=FileNotFoundError):
            with self.assertRaisesRegex(InternetError, '^internet_elevation_failed$'):
                self.overlay.start()
        self.assertIsNone(self.overlay.child)

    def test_snapshot_reports_physical_route_overlap_as_network_overlap(self):
        self.overlay.child = Mock(poll=Mock(return_value=None))
        adapter = LanInterface('10.203.77.1', 24, 'WePlay-Test')
        self.overlay.latest = dict(format=elevation.FORMAT, phase='ready',
                                   address=adapter.address, device=adapter.name, peers={}, written_monotonic_ns=time.monotonic_ns())
        competing = LanInterface('10.203.1.2', 16, 'Corporate VPN')
        with patch.object(elevation, 'enumerate_interfaces', return_value=[adapter, competing]), \
                self.assertRaisesRegex(InternetError, '^internet_network_overlap$'):
            self.overlay.snapshot()

    def test_worker_exit_during_adapter_lookup_invalidates_the_checked_state(self):
        self.overlay.child = Mock(poll=Mock(side_effect=[None, 2]))
        adapter = LanInterface('10.203.77.1', 24, 'WePlay-Test')
        self.overlay.latest = dict(format=elevation.FORMAT, phase='ready',
                                   address=adapter.address, device=adapter.name, peers={}, written_monotonic_ns=time.monotonic_ns())
        with patch.object(elevation, 'enumerate_interfaces', return_value=[adapter]), \
                self.assertRaisesRegex(InternetError, '^internet_disconnected$'):
            self.overlay.snapshot()

    def test_recently_received_but_old_worker_sample_cannot_confirm_the_tunnel(self):
        self.overlay.child = Mock(poll=Mock(return_value=None))
        adapter = LanInterface('10.203.77.1', 24, 'WePlay-Test')
        self.overlay.latest = dict(format=elevation.FORMAT, phase='ready', address=adapter.address,
                                   device=adapter.name, peers={}, written_monotonic_ns=1_000_000_000)
        with patch.object(elevation.time, 'monotonic', return_value=20.0), \
                patch.object(elevation.time, 'monotonic_ns', return_value=20_000_000_000), \
                patch.object(elevation, 'enumerate_interfaces', return_value=[adapter]), \
                self.assertRaisesRegex(InternetError, '^internet_disconnected$'):
            self.overlay.snapshot()

    def test_linux_guest_waits_for_coordinator_in_elevated_ready_state(self):
        self.overlay.host = False
        self.overlay.progress = Mock()
        self.overlay.latest = {'phase': 'ready'}
        adapter = LanInterface('10.203.77.2', 24, 'WePlay-Test')
        coordinator = self.overlay.room.coordinator
        with patch.object(elevation.os, 'geteuid', return_value=1000, create=True), \
                patch('easytier_runtime.verify_runtime'), \
                patch.object(elevation.subprocess, 'Popen', return_value=Mock(poll=Mock(return_value=None))), \
                patch.object(elevation.threading, 'Thread'), patch.object(elevation.time, 'sleep'), \
                patch.object(self.overlay, 'snapshot', side_effect=[(adapter, {}), (adapter, {coordinator: 'p2p'})]) as snapshot:
            self.assertEqual(self.overlay.start(), adapter)
        self.assertEqual(snapshot.call_count, 2)
        self.overlay.progress.assert_called_once_with('internet_connected')

    def test_linux_room_cancelled_during_ready_probe_never_reports_connected(self):
        self.overlay.progress = Mock()
        self.overlay.latest = {'phase': 'ready'}
        cancelled = False

        def cancel():
            if cancelled:
                raise InternetError('internet_cancelled')

        def snapshot():
            nonlocal cancelled
            cancelled = True
            return LanInterface(self.overlay.room.coordinator, 24, 'WePlay-Test'), {}

        self.overlay.cancel_check = cancel
        with patch.object(elevation.os, 'geteuid', return_value=1000, create=True), \
                patch('easytier_runtime.verify_runtime'), patch.object(elevation.subprocess, 'Popen'), \
                patch.object(elevation.threading, 'Thread'), patch.object(self.overlay, 'close') as close, \
                patch.object(self.overlay, 'snapshot', side_effect=snapshot), \
                self.assertRaisesRegex(InternetError, '^internet_cancelled$'):
            self.overlay.start()
        close.assert_called_once()
        self.overlay.progress.assert_not_called()

    def test_linux_close_reports_worker_exit_failure_after_reaping(self):
        child = self.overlay.child = Mock(wait=Mock(return_value=2))
        child.stdin.closed = False
        with self.assertRaisesRegex(InternetError, '^internet_cleanup_failed$'):
            self.overlay.close()
        self.assertIsNone(self.overlay.child)
        child.stdin.close.assert_called_once()
        child.stdout.close.assert_called_once()

    def test_reader_drains_worker_error_already_in_exited_process_pipe(self):
        self.overlay.child = worker = Mock()
        worker.poll.return_value = 2
        worker.stdout.fileno.return_value = 456
        selector = Mock()
        selector.select.return_value = [(None, None)]
        line = json.dumps(dict(format=elevation.FORMAT, phase='error')).encode('ascii') + b'\n'
        with patch.object(elevation.selectors, 'DefaultSelector', return_value=selector), \
                patch.object(elevation.os, 'read', side_effect=[line, b'']) as read:
            self.overlay._receive()
        self.assertEqual(self.overlay.latest, dict(format=elevation.FORMAT, phase='error'))
        self.assertLessEqual(read.call_count, 2)
        worker.stdin.close.assert_called_once()
        selector.close.assert_called_once()

    def test_heartbeat_pipe_break_does_not_discard_final_worker_status(self):
        self.overlay.child = worker = Mock()
        worker.poll.side_effect = [None, 2]
        worker.stdin.write.side_effect = BrokenPipeError
        selector = Mock()
        selector.select.side_effect = [[], [(None, None)], [(None, None)]]
        record = dict(format=elevation.FORMAT, phase='error')
        line = json.dumps(record).encode('ascii') + b'\n'
        with patch.object(elevation.selectors, 'DefaultSelector', return_value=selector), \
                patch.object(elevation.os, 'read', side_effect=[line, b'']):
            self.overlay._receive()
        self.assertEqual(self.overlay.latest, record)
        worker.stdin.write.assert_called_once_with(b'ping\n')

    def test_worker_checks_parent_eof_before_starting_tunnel(self):
        value = dict(format=elevation.FORMAT, root=str(self.overlay.root),
                     room=self.overlay.room.token(), host=True)
        stdin = Mock()
        stdin.buffer.readline.return_value = json.dumps(value).encode('ascii') + b'\n'
        root = Mock()
        root.stat.return_value.st_uid = 1000
        selector = Mock()
        selector.select.return_value = [(None, None)]
        overlay = Mock()
        start_tunnel = Mock()
        def create_overlay(*_args, cancel_check=None, **_options):
            def start():
                if cancel_check:
                    cancel_check()
                start_tunnel()
            overlay.start.side_effect = start
            return overlay

        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as folder:
            private = Path(folder) / 'owned-tunnel'
            private.mkdir()
            with patch.object(elevation.os, 'geteuid', return_value=0, create=True), \
                    patch.dict(elevation.os.environ, {'PKEXEC_UID': '1000'}), \
                    patch.object(elevation.sys, 'stdin', stdin), \
                    patch.object(elevation.compatibility, 'plain', return_value=root), \
                    patch.object(elevation.tempfile, 'mkdtemp', return_value=str(private)), \
                    patch.object(elevation.selectors, 'DefaultSelector', return_value=selector), \
                    patch.object(elevation.os, 'read', return_value=b''), \
                    patch('easytier_runtime.EasyTier', side_effect=create_overlay):
                self.assertEqual(elevation.worker(), 0)
            self.assertFalse(private.exists())
        start_tunnel.assert_not_called()
        overlay.close.assert_called_once()

    @unittest.skipUnless(sys.platform == 'linux', 'requires native Linux pipes')
    def test_start_reports_buffered_failure_from_real_exited_worker(self):
        self.check_failure_from_real_worker({})

    @unittest.skipUnless(sys.platform == 'linux', 'requires native Linux pipes')
    def test_linux_worker_preserves_address_conflict_over_the_private_pipe(self):
        self.check_failure_from_real_worker({'code': 'internet_address_conflict'})

    def check_failure_from_real_worker(self, extra):
        record = json.dumps(dict(format=elevation.FORMAT, phase='error', **extra))
        script = f'import sys; sys.stdin.readline(); print({record!r}, flush=True); sys.exit(2)'
        spawn = subprocess.Popen
        receive = self.overlay._receive

        def fixture(_command, **options):
            return spawn([sys.executable, '-I', '-B', '-c', script], **options)

        def after_worker_exit():
            self.overlay.child.wait(timeout=3)
            receive()

        with patch.object(os, 'geteuid', return_value=1000), \
                patch('easytier_runtime.verify_runtime'), \
                patch.object(elevation.subprocess, 'Popen', side_effect=fixture), \
                patch.object(self.overlay, '_receive', side_effect=after_worker_exit):
            with self.assertRaisesRegex(InternetError, '^' + extra.get('code', 'internet_connect_failed') + '$'):
                self.overlay.start()
        self.assertIsNone(self.overlay.child)


if __name__ == '__main__':
    unittest.main()
