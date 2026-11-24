import json
import os
from pathlib import Path
import subprocess
import sys
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/linux'))
import linux_elevation as elevation
from internet_room import InternetError, Room


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

    @unittest.skipUnless(sys.platform == 'linux', 'requires native Linux pipes')
    def test_start_reports_buffered_failure_from_real_exited_worker(self):
        record = json.dumps(dict(format=elevation.FORMAT, phase='error'))
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
            with self.assertRaisesRegex(InternetError, 'internet_connect_failed'):
                self.overlay.start()
        self.assertIsNone(self.overlay.child)


if __name__ == '__main__':
    unittest.main()
