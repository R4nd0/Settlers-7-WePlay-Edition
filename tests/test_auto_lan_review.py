"""Independent lifecycle/rate/route regressions. All identities are invented."""
import ctypes
import http.client
from dataclasses import replace
import os
from pathlib import Path
import socket
import subprocess
import sys
import tempfile
import threading
import time
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/offline-ui')]
import lan_child_job
import lan_control
import lan_discovery
import lan_hub
import player_settings
from lobby_directory import CONTROL_FORMAT, LanInterface, LobbyEndpoint, LobbyRoom, encode


class DiscoveryReviewTests(unittest.TestCase):
    def setUp(self):
        self.interface = LanInterface('192.168.7.10', 24)
        self.room = LobbyRoom('a' * 32, 'Test', 'b' * 64,
            LobbyEndpoint('lan-direct', self.interface.address), self.interface)

    def test_rotating_sources_cannot_evade_global_beacon_budget(self):
        now = [10.0]
        beacon = lan_discovery.LanBeacon(self.room, clock=lambda: now[0],
            socket_factory=Mock(), route_socket=Mock())
        packet = lan_discovery.query_message('c' * 32)
        replies = sum(beacon._answer(packet, (f'192.168.7.{i % 200 + 1}', 1234)) is not None
                      for i in range(1000))
        self.assertEqual(replies, 32)
        self.assertLessEqual(len(beacon.rates), 128)
        now[0] += 1
        self.assertIsNotNone(beacon._answer(packet, ('192.168.7.201', 1234)))
        self.assertIsNone(beacon._answer(packet, ('192.168.7.201', 1234)))
        beacon.close()

    def test_scan_packet_budget_is_exact_across_three_interfaces(self):
        sockets = [Mock() for _ in range(3)]
        for sock in sockets:
            sock.recvfrom.return_value = (b'bad', ('192.168.7.20', 9112))
        directory = lan_discovery.LanDiscovery([self.interface] * 3,
            socket_factory=Mock(side_effect=sockets), clock=lambda: 0, route_socket=Mock())
        self.assertEqual(directory.list_rooms(timeout=.1), [])
        self.assertEqual(sum(sock.recvfrom.call_count for sock in sockets), 256)
        for sock in sockets:
            sock.close.assert_called_once()

    def test_scan_checks_deadline_between_interfaces(self):
        now = [0.0]
        sockets = [Mock() for _ in range(3)]
        def receive(*_):
            now[0] += .2
            raise socket.timeout()
        for sock in sockets:
            sock.recvfrom.side_effect = receive
        directory = lan_discovery.LanDiscovery([self.interface] * 3,
            socket_factory=Mock(side_effect=sockets), clock=lambda: now[0], route_socket=Mock())
        directory.list_rooms(timeout=.1)
        self.assertEqual(sum(sock.recvfrom.call_count for sock in sockets), 1)

    @staticmethod
    def table(entries):
        rows = []
        for address, index in entries:
            row = lan_discovery.IpAddressRow()
            row.address = int.from_bytes(socket.inet_aton(address), 'little')
            row.index = index
            rows.append(bytes(row))
        return len(rows).to_bytes(4, 'little') + b''.join(rows)

    def test_route_index_requires_exact_unique_address_and_one_address_per_nic(self):
        lookup = lan_discovery.interface_index_from_table
        self.assertEqual(lookup(self.table([('192.168.7.10', 257), ('192.168.8.10', 9)]), self.interface.address), 257)
        for entries in ([], [('192.168.7.10', 0)], [('192.168.7.10', 0x1000000)],
                        [('192.168.7.10', 4), ('192.168.7.10', 5)],
                        [('192.168.7.10', 4), ('192.168.7.11', 4)]):
            with self.subTest(entries=entries), self.assertRaises(ValueError):
                lookup(self.table(entries), self.interface.address)
        with self.assertRaises(ValueError): lookup(b'\x01\0\0\0', self.interface.address)

    def test_socket_route_uses_network_byte_order_without_route_changes(self):
        table = self.table([('192.168.7.10', 257)])
        def read_table(buffer, size, ordered):
            ctypes.memmove(buffer, table, len(table))
            ctypes.cast(size, ctypes.POINTER(ctypes.c_uint32))[0] = len(table)
            return 0
        function = Mock(side_effect=read_table)
        with patch.object(lan_discovery.ctypes, 'WinDLL', return_value=SimpleNamespace(GetIpAddrTable=function)):
            sock = Mock()
            lan_discovery.select_outgoing_interface(sock, self.interface)
        sock.setsockopt.assert_called_once_with(socket.IPPROTO_IP, 31, b'\0\0\x01\x01')

    def test_route_failure_closes_beacon_socket_before_binding(self):
        sock = Mock()
        with self.assertRaises(ValueError):
            lan_discovery.LanBeacon(self.room, socket_factory=Mock(return_value=sock),
                route_socket=Mock(side_effect=ValueError('fixture')))
        sock.close.assert_called_once()
        sock.bind.assert_not_called()


class JobReviewTests(unittest.TestCase):
    @staticmethod
    def api():
        return SimpleNamespace(CreateJobObjectW=Mock(return_value=0x12345678),
            SetInformationJobObject=Mock(return_value=1),
            AssignProcessToJobObject=Mock(return_value=1), CloseHandle=Mock(return_value=1))

    def test_windows_layout_and_limits_preserve_pointer_width(self):
        pointer_size = ctypes.sizeof(ctypes.c_void_p)
        self.assertEqual(ctypes.sizeof(lan_child_job.BasicLimits), 64 if pointer_size == 8 else 48)
        self.assertEqual(ctypes.sizeof(lan_child_job.ExtendedLimits), 144 if pointer_size == 8 else 112)
        api = self.api()
        def capture(handle, kind, pointer, length):
            limits = ctypes.cast(pointer, ctypes.POINTER(lan_child_job.ExtendedLimits)).contents
            self.assertEqual(limits.BasicLimitInformation.LimitFlags, 0x2000)
            self.assertEqual(kind, 9)
            self.assertEqual(length, ctypes.sizeof(limits))
            return 1
        api.SetInformationJobObject.side_effect = capture
        job = lan_child_job.ServiceJob(api=api)
        child = Mock(_handle=0x3456789a)
        with patch.object(lan_child_job.subprocess, 'Popen', return_value=child):
            self.assertIs(job.start(['owned-fixture']), child)
        api.AssignProcessToJobObject.assert_called_once_with(0x12345678, 0x3456789a)
        api.CreateJobObjectW.assert_called_once_with(None, None)
        job.close(); job.close()
        api.CloseHandle.assert_called_once_with(0x12345678)

    def test_assignment_failure_terminates_and_reaps_only_created_child(self):
        api = self.api(); api.AssignProcessToJobObject.return_value = 0
        job = lan_child_job.ServiceJob(api=api)
        child = Mock(_handle=999); child.poll.return_value = None
        with patch.object(lan_child_job.subprocess, 'Popen', return_value=child):
            with self.assertRaises(OSError): job.start(['owned-fixture'])
        child.terminate.assert_called_once(); child.wait.assert_called_once_with(timeout=10)
        job.close()

    def test_configuration_failure_closes_job(self):
        api = self.api(); api.SetInformationJobObject.return_value = 0
        with self.assertRaises(OSError): lan_child_job.ServiceJob(api=api)
        api.CloseHandle.assert_called_once_with(0x12345678)

    @unittest.skipUnless(os.name == 'nt', 'Windows job contract')
    def test_real_job_close_ends_owned_fixture_child(self):
        job = lan_child_job.ServiceJob()
        child = None
        try:
            child = job.start([sys.executable, '-c', 'import time; time.sleep(60)'],
                creationflags=subprocess.CREATE_NO_WINDOW)
            self.assertIsNone(child.poll())
            job.close()
            self.assertIsNotNone(child.wait(timeout=5))
        finally:
            job.close()
            if child is not None and child.poll() is None:
                child.terminate(); child.wait(timeout=5)

    @unittest.skipUnless(os.name == 'nt', 'Windows job contract')
    def test_abrupt_hub_exit_also_ends_owned_fixture_child(self):
        # Separate pure-Python fixture, never the service/game or a user's PID.
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as directory:
            ready = Path(directory) / 'fixture-ready.txt'
            script = ('import sys,time; from pathlib import Path; '
                f'sys.path.insert(0,{str(ROOT / "tools/installer")!r}); '
                'from lan_child_job import ServiceJob; j=ServiceJob(); '
                'p=j.start([sys.executable,"-c","import time; time.sleep(60)"]); '
                f'Path({str(ready)!r}).write_text(str(p.pid)); time.sleep(60)')
            hub = subprocess.Popen([sys.executable, '-c', script], creationflags=subprocess.CREATE_NO_WINDOW)
            api = ctypes.WinDLL('kernel32', use_last_error=True)
            api.OpenProcess.argtypes = [ctypes.c_uint32, ctypes.c_int, ctypes.c_uint32]
            api.OpenProcess.restype = ctypes.c_void_p
            api.WaitForSingleObject.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            api.WaitForSingleObject.restype = ctypes.c_uint32
            api.TerminateProcess.argtypes = [ctypes.c_void_p, ctypes.c_uint32]
            api.CloseHandle.argtypes = [ctypes.c_void_p]
            handle = None
            try:
                deadline = time.monotonic() + 5
                while not ready.exists() and hub.poll() is None and time.monotonic() < deadline:
                    time.sleep(.02)
                self.assertTrue(ready.exists(), 'Owned fixture did not start')
                handle = api.OpenProcess(0x100001, False, int(ready.read_text()))
                self.assertTrue(handle)
                hub.terminate(); hub.wait(timeout=5)
                self.assertEqual(api.WaitForSingleObject(handle, 5000), 0)
            finally:
                if hub.poll() is None:
                    hub.terminate(); hub.wait(timeout=5)
                if handle:
                    if api.WaitForSingleObject(handle, 0) != 0:
                        api.TerminateProcess(handle, 1)
                    api.CloseHandle(handle)


class SettingsReviewTests(unittest.TestCase):
    def test_duplicate_fields_are_not_silently_replaced(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as directory:
            root = Path(directory); (root / 'LocalData').mkdir()
            path = root / player_settings.FILE
            content = b'{"format":"s7-player-settings-v1","display_name":"One","display_name":"Two"}'
            path.write_bytes(content)
            with self.assertRaises(ValueError): player_settings.load(root)
            with self.assertRaises(ValueError): player_settings.save(root, 'Three')
            self.assertEqual(path.read_bytes(), content)


class MembershipShutdownReviewTests(unittest.TestCase):
    def test_slow_renew_cannot_publish_after_close_or_delete_a_new_session_file(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as directory:
            root = Path(directory)
            local, coordinator = '192.168.7.10', '192.168.7.20'
            reply = dict(format=CONTROL_FORMAT, content_id='b' * 64, revision=1,
                lease_seconds=15, peers=[local, coordinator])
            blocked, release = threading.Event(), threading.Event()
            def transport(*args, **kwargs):
                if args[2] == 'leave':
                    return None
                blocked.set()
                if not release.wait(3):
                    raise TimeoutError('Test release missing')
                return reply
            membership = lan_control.Membership(root, local, coordinator, 'b' * 64,
                transport=transport, clock=lambda: 10)
            membership.path = lan_control.write_peers(root, local, (coordinator,), clock=lambda: 0)
            membership.admitted = True
            worker = threading.Thread(target=membership.renew)
            membership.thread = worker
            worker.start()
            try:
                self.assertTrue(blocked.wait(1))
                # Force the join timeout without making this deterministic race
                # regression wait five seconds; the worker itself is real.
                with patch.object(worker, 'join'):
                    with self.assertRaises(RuntimeError): membership.close()
                self.assertFalse(membership.path.exists())
                new_path = lan_control.write_peers(root, local, (), clock=lambda: 1000)
                replacement = new_path.read_bytes()
                release.set(); worker.join(timeout=2)
                self.assertFalse(worker.is_alive())
                self.assertEqual(new_path.read_bytes(), replacement)
                self.assertIsNone(membership.last_success_ms)
            finally:
                release.set(); worker.join(timeout=3)

    def test_peer_response_rejects_unknown_fields(self):
        reply = dict(format=CONTROL_FORMAT, content_id='b' * 64, revision=1,
            lease_seconds=15, peers=['192.168.7.10', '192.168.7.20'])
        self.assertEqual(lan_control.validate_peers(reply, '192.168.7.10', '192.168.7.20', 'b' * 64),
            ('192.168.7.20',))
        reply['extension'] = 'ignored-before-review'
        with self.assertRaises(ValueError):
            lan_control.validate_peers(reply, '192.168.7.10', '192.168.7.20', 'b' * 64)

    def test_oversized_existing_peer_file_is_not_overwritten(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as directory:
            root = Path(directory); (root / 'LocalData').mkdir()
            path = root / 'LocalData/lan-peers.ini'
            original = b'format=s7-lan-peers-v1\n' + b'x' * 4096
            path.write_bytes(original)
            with self.assertRaises(ValueError):
                lan_control.write_peers(root, '192.168.7.10', (), clock=lambda: 0)
            self.assertEqual(path.read_bytes(), original)
            self.assertEqual(list(path.parent.glob('peers-*.tmp')), [])


class HttpDeadlineReviewTests(unittest.TestCase):
    @staticmethod
    def fixture_headers(connection, stop):
        request = b''
        while not stop.is_set() and len(request) < 4096:
            chunk = connection.recv(1024)
            if not chunk:
                return False  # EOF cannot make progress; never spin on b''.
            request += chunk
            if b'\r\n\r\n' in request:
                return True
        return False

    def test_fixture_eof_and_stop_before_headers_end_without_spinning(self):
        stop = threading.Event()
        connection = Mock()
        connection.recv.side_effect = [b'GET / HTTP/1.1\r\n', b'']
        self.assertFalse(self.fixture_headers(connection, stop))
        self.assertEqual(connection.recv.call_count, 2)
        stop.set(); connection.reset_mock()
        self.assertFalse(self.fixture_headers(connection, stop))
        connection.recv.assert_not_called()

    def slow_response(self, *, headers):
        listener = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        listener.bind(('127.0.0.1', 0)); listener.listen(1); listener.settimeout(2)
        port = listener.getsockname()[1]
        stop = threading.Event()
        entered = threading.Event()
        failures = []
        def serve():
            try:
                connection, _ = listener.accept()
                with connection:
                    connection.settimeout(1)
                    if not self.fixture_headers(connection, stop):
                        return
                    entered.set()
                    if headers:
                        connection.sendall(b'HTTP/1.1 200 OK\r\nX-Fixture: ')
                    else:
                        # HTTPConnection releases its own socket reference after
                        # parsing Connection:close. Deadline must retain ours.
                        connection.sendall(b'HTTP/1.1 200 OK\r\nConnection: close\r\nContent-Length: 2000\r\n\r\n')
                    deadline = time.monotonic() + 3
                    while not stop.is_set() and time.monotonic() < deadline:
                        connection.sendall(b'x')
                        stop.wait(.01)
            except OSError:
                pass  # Expected when deadline shuts down this fixture socket.
            except BaseException as error:
                failures.append(error)
        worker = threading.Thread(target=serve)
        worker.start()
        started = time.monotonic()
        try:
            # Only this test admits loopback and an ephemeral control port.
            with patch.object(lan_control, 'private_address', side_effect=lambda x: x), \
                    patch.object(lan_control, 'HTTP_PORT', port):
                with self.assertRaises((OSError, ValueError, http.client.HTTPException)):
                    lan_control.request('127.0.0.1', '127.0.0.1', 'status', timeout=.2)
            self.assertTrue(entered.is_set())
            self.assertLess(time.monotonic() - started, 1.0)
        finally:
            stop.set(); listener.close(); worker.join(timeout=3)
        self.assertFalse(worker.is_alive())
        self.assertEqual(failures, [])

    def test_header_drip_cannot_extend_total_deadline(self):
        self.slow_response(headers=True)

    def test_body_drip_after_connection_close_header_cannot_extend_deadline(self):
        self.slow_response(headers=False)

    def test_normal_completion_cancels_timer_before_any_late_shutdown(self):
        connection = Mock()
        response = Mock(status=200)
        response.read.return_value = encode(dict(format=CONTROL_FORMAT, content_id='b' * 64))
        connection.getresponse.return_value = response
        with patch.object(lan_control.http.client, 'HTTPConnection', return_value=connection):
            lan_control.request('192.168.7.10', '192.168.7.20', 'status', timeout=.03)
        time.sleep(.05)
        connection.sock.shutdown.assert_not_called()
        response.close.assert_called_once()
        connection.close.assert_called_once()

    def test_timer_start_failure_still_closes_connection_without_joining_unstarted_thread(self):
        connection = Mock()
        timer = Mock(ident=None)
        timer.start.side_effect = RuntimeError('fixture thread unavailable')
        with patch.object(lan_control.http.client, 'HTTPConnection', return_value=connection), \
                patch.object(lan_control.threading, 'Timer', return_value=timer):
            with self.assertRaises(RuntimeError):
                lan_control.request('192.168.7.10', '192.168.7.20', 'status')
        timer.cancel.assert_called_once()
        timer.join.assert_not_called()
        connection.close.assert_called_once()


if __name__ == '__main__':
    unittest.main()
