"""Real loopback SINGLE Gold service; authored protocol inputs, no game launch."""
import hmac
import http.client
import json
from pathlib import Path
import socket
import struct
import sys
import time
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/gold'), str(ROOT / 'tests')]
import gold_session as service
import login_server as protocol
from gold_login_tests import incoming, request, login_args
from account_config import ROUTE


class GoldSessionTests(unittest.TestCase):
    def setUp(self):
        self.key, self.session = bytes(range(32)), bytes(range(16))
        self.identity = (42).to_bytes(4, 'little') + bytes(12)
        self.name = 'local-fixture'
        self.owner = service.GoldSessionService(name=self.name, client_key=self.key,
            session=self.session, identity=self.identity)
        self.addCleanup(self.owner.close)

    def client(self, address='127.0.0.2'):
        sock = socket.socket(socket.AF_INET, socket.SOCK_DGRAM)
        self.addCleanup(sock.close); sock.bind((address, 0)); sock.setblocking(False)
        return sock

    def exchange(self, sock, port, data, count):
        sock.sendto(data, ('127.0.0.1', port)); replies = []; deadline = time.monotonic() + 2
        while len(replies) < count and time.monotonic() < deadline:
            self.owner.pump(.001)
            try: replies.append(sock.recv(65536))
            except BlockingIOError: pass
        self.assertEqual(len(replies), count)
        return replies

    def test_domain_separated_secret_matches_native_vector_and_is_not_in_summary(self):
        derived = service.local_secret(self.key, self.session)
        self.assertEqual(derived, '9b88c19d84020f1d655a60c3a23287aba853d838cd30ca552ec12a5c3accd12e')
        self.assertNotEqual(derived, service.local_secret(self.key, b's' * 16))
        self.assertNotEqual(derived, service.local_secret(b'k' * 32, self.session))
        self.assertEqual(self.owner.adapter.password, derived.encode('ascii'))
        summary = json.dumps(self.owner.summary())
        for value in (derived, self.key.hex(), self.session.hex(), self.name): self.assertNotIn(value, summary)
        self.assertFalse(self.owner.summary()['game_session_admission'])

    def test_one_real_principal_three_owned_loopback_sockets_and_no_phantom_peer(self):
        self.assertEqual(self.owner.adapter.principal, 42)
        self.assertEqual(self.owner.adapter.client_address, '127.0.0.2')
        self.assertEqual(self.owner.probes.adapters, (self.owner.adapter,))
        self.assertEqual(self.owner.gatherings.records, {})
        self.assertEqual(self.owner.summary()['configured_gold_identities'], 1)
        for sock in (self.owner.auth, self.owner.backend, self.owner.config.socket):
            address, port = sock.getsockname()
            self.assertEqual(address, '127.0.0.1'); self.assertTrue(1024 <= port <= 65535)

    def test_original_authentication_backend_and_registration_sequences_work_with_one_owner(self):
        adapter = self.owner.adapter; auth = self.client(); backend = self.client()
        response = self.exchange(auth, self.owner.auth_port, incoming(0), 1)[0]
        auth_server = int.from_bytes(response[10:14], 'little')
        self.exchange(auth, self.owner.auth_port, incoming(1, 48, 17, auth_server, 1, b'', 0x12345678), 1)
        args = login_args(name=self.name, password=service.local_secret(self.key, self.session))
        self.exchange(auth, self.owner.auth_port, incoming(2, 48, 17, auth_server, 2, request(10, 1, 2, args)), 2)
        self.assertEqual(adapter.events['login_answered'], 1)
        self.exchange(auth, self.owner.auth_port, incoming(2, 48, 17, auth_server, 3,
            request(10, 2, 3, struct.pack('<II', 42, 1))), 2)
        response = self.exchange(backend, self.owner.backend_port, incoming(0), 1)[0]
        backend_server = int.from_bytes(response[10:14], 'little')
        plain = struct.pack('<III', 42, 1, 0xffffffff)
        encrypted = protocol.rc4(plain, adapter.session_key)
        encrypted += hmac.digest(adapter.session_key, encrypted, 'md5')
        payload = protocol.wrap(struct.pack('<I', len(adapter.opaque_ticket)) + adapter.opaque_ticket
                                + struct.pack('<I', len(encrypted)) + encrypted)
        self.exchange(backend, self.owner.backend_port,
            incoming(1, 48, 17, backend_server, 1, payload, 0x12345678), 1)
        urls = struct.pack('<I', 1) + protocol.text(
            f'prudps:/address=127.0.0.2;port={backend.getsockname()[1]};sid=15;stream=3')
        self.exchange(backend, self.owner.backend_port, incoming(2, 48, 17, backend_server, 2,
            request(11, 3, 1, urls)), 2)
        self.assertTrue(adapter.registered); self.assertEqual(adapter.events['registered'], 1)
        self.assertEqual(len(adapter.connections), 2)
        self.assertFalse(self.owner.summary()['game_session_admission'])

    def test_wrong_loopback_source_cannot_open_authentication(self):
        sock = self.client('127.0.0.3')
        sock.sendto(incoming(0), ('127.0.0.1', self.owner.auth_port)); self.owner.pump(.01)
        with self.assertRaises(BlockingIOError): sock.recv(65536)
        self.assertFalse(self.owner.adapter.connections)

    def test_config_uses_original_route_host_and_only_the_owned_gold_source(self):
        for source, status in (('127.0.0.2', 200), ('127.0.0.3', 400)):
            connection = http.client.HTTPConnection('127.0.0.1', self.owner.config_port,
                timeout=2, source_address=(source, 0)); self.addCleanup(connection.close)
            connection.request('GET', ROUTE); self.owner.pump(.01)
            response = connection.getresponse(); body = response.read()
            self.assertEqual(response.status, status)
            if status == 200:
                values = json.loads(body)
                self.assertEqual(values, [{'Name': 'SandboxUrl', 'Values': [
                    f'prudp:/address=127.0.0.1;port={self.owner.auth_port};stream=3;sid=1']}])
        self.assertEqual(self.owner.config.responses, 1)

    def test_close_releases_all_sockets_and_memory_without_expiring_a_healthy_game(self):
        self.owner.gatherings.records[1] = object(); self.owner.probes.jobs[1] = object()
        self.owner.close(); self.owner.close()
        for sock in (self.owner.auth, self.owner.backend, self.owner.config.socket): self.assertEqual(sock.fileno(), -1)
        self.assertFalse(self.owner.adapter.connections); self.assertFalse(self.owner.gatherings.records)
        self.assertFalse(self.owner.probes.jobs); self.assertIsNone(self.owner.adapter.profile)
        with self.assertRaises(ValueError): self.owner.pump()

    def test_failed_partial_construction_closes_already_owned_sockets(self):
        original = socket.socket; opened = []
        def create(*args, **kwargs):
            result = original(*args, **kwargs); opened.append(result); return result
        with patch.object(service.socket, 'socket', side_effect=create), \
                patch.object(service, 'ConfigService', side_effect=RuntimeError('fixture')):
            with self.assertRaises(RuntimeError):
                service.GoldSessionService(name=self.name, client_key=self.key, session=self.session, identity=self.identity)
        self.assertEqual(len(opened), 2)
        self.assertTrue(all(sock.fileno() == -1 for sock in opened))

    def test_invalid_identity_or_secret_is_rejected_before_opening_a_socket(self):
        valid = dict(name=self.name, client_key=self.key, session=self.session, identity=self.identity)
        changes = [{'identity': value} for value in (bytes(16), b'i' * 16, self.identity[:-1],
                                                     (0x80000000).to_bytes(4, 'little') + bytes(12))]
        changes += [{field: value} for field, size in (('client_key', 32), ('session', 16))
                    for value in (bytes(size), b'x' * (size - 1), 'a' * (size * 2))]
        changes += [{'name': value} for value in ('', 'Ü', 'line\nname', 'x' * 65)]
        with patch.object(service.socket, 'socket') as create:
            for change in changes:
                with self.assertRaises(ValueError): service.GoldSessionService(**{**valid, **change})
            create.assert_not_called()
        for principal in (True, 0, -1, 0x80000000, '42'):
            with self.assertRaises(ValueError): protocol.GoldLoginAdapter('fixture', 'a' * 64, 55123, principal=principal)


if __name__ == '__main__': unittest.main()
