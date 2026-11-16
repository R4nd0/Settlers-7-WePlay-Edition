"""Authored pair admission and real local services; no native game/VPN launch."""
import base64
from dataclasses import replace
import hmac
import json
from pathlib import Path
import socket
import sys
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tests')]
import crossplay_host as host
import crossplay_invitation as invitation
from crossplay_broker import HEADER, PINS
import test_crossplay_content as fixtures
from internet_room import Room

ROOM = Room('a' * 32, 'b' * 64, '10.203.77.0/24')


class CrossplayHostTests(unittest.TestCase):
    def setUp(self):
        self.fixture = fixtures.CrossplayContentTests()
        self.fixture.setUp(); self.addCleanup(self.fixture.doCleanups)
        self.catalog = self.fixture.catalog(); self.fixture.approve(self.catalog)
        self.pair = host.Host(self.fixture.roots[1], self.catalog, history=3, name='WePlay Host')
        self.addCleanup(self.pair.close)

    def client(self, slot):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM); self.addCleanup(sock.close)
        sock.bind(('127.0.0.' + str(slot + 1), 0)); sock.settimeout(2)
        sock.connect(('127.0.0.1', self.pair.broker.port))
        return sock

    def send(self, sock, slot, kind, sequence, payload):
        frame = HEADER.pack(b'WPX1', 1, kind, self.pair.broker.session, sequence,
                            slot, 0 if kind == 1 else 3 - slot, 0, len(payload)) + payload
        sock.sendall(frame + hmac.digest(self.pair.broker.launches[slot - 1].key, frame, 'sha256'))

    def receive(self, sock, slot):
        def exact(size):
            result = bytearray()
            while len(result) < size:
                data = sock.recv(size - len(result)); self.assertTrue(data); result.extend(data)
            return bytes(result)
        header = exact(HEADER.size); fields = HEADER.unpack(header)
        body = exact(fields[-1]); tag = exact(32)
        self.assertEqual(tag, hmac.digest(self.pair.broker.launches[slot - 1].key, header + body, 'sha256'))
        return fields, body

    def test_host_binds_service_ports_identity_and_catalog_to_one_epoch(self):
        binding = self.pair.binding
        self.assertEqual(binding.edition, 1); self.assertEqual(binding.peer, 3); self.assertEqual(binding.role, 'host')
        self.assertEqual(binding.port, self.pair.broker.port)
        self.assertEqual(binding.gold_auth_port, self.pair.gold.auth_port)
        self.assertEqual(binding.gold_config_port, self.pair.gold.config_port)
        self.assertEqual(int.from_bytes(binding.identity[:4], 'little'), self.pair.gold.adapter.principal)
        self.assertEqual(binding.content, self.catalog.digest)
        launches = self.pair.broker.launches
        self.assertNotEqual(launches[0].key, launches[1].key)
        self.assertNotEqual(launches[0].native_identity, launches[1].native_identity)

    def test_private_invitation_contains_only_guest_channel_material_and_roundtrips(self):
        value = self.pair.invitation(ROOM); token = value.token()
        self.assertLessEqual(len(token), 2048)
        restored = invitation.parse(token); self.assertEqual(value, restored)
        body = token[len(invitation.PREFIX):]
        decoded = json.loads(base64.urlsafe_b64decode(body + '=' * (-len(body) % 4)))
        self.assertEqual(decoded['key'], self.pair.broker.launches[1].key.hex())
        self.assertNotIn(self.pair.binding.key.hex(), json.dumps(decoded))
        self.assertNotIn(ROOM.secret, repr(value)); self.assertNotIn(repr(value.key), repr(value))
        binding = restored.binding(self.fixture.roots[3], self.catalog, port=12345, name='WePlay Guest')
        self.assertEqual(binding.edition, 3); self.assertEqual(binding.peer, 1); self.assertEqual(binding.role, 'guest')
        self.assertIsNone(binding.gold_auth_port); self.assertIsNone(binding.gold_config_port)

    def test_content_mismatch_or_unapproved_local_profile_fails_before_guest_launch(self):
        value = self.pair.invitation(ROOM)
        with self.assertRaisesRegex(ValueError, 'content_changed'):
            replace(value, content=b'x' * 32).binding(self.fixture.roots[3], self.catalog, port=12345, name='Guest')
        with patch.object(fixtures.content, 'APPROVED_DIGESTS', frozenset()):
            with self.assertRaisesRegex(ValueError, 'catalog_unapproved'):
                value.binding(self.fixture.roots[3], self.catalog, port=12345, name='Guest')
            with patch.object(host, 'CrossplayBroker') as broker:
                with self.assertRaisesRegex(ValueError, 'catalog_unapproved'):
                    host.Host(self.fixture.roots[1], self.catalog, history=3, name='Host')
                broker.assert_not_called()

    def test_existing_authenticated_wire_relays_original_opaque_payloads_with_owned_pump(self):
        initial = self.pair
        for edition in (2, 3):
            self.pair = initial if edition == 3 else host.Host(self.fixture.roots[1], self.catalog,
                history=edition, name='WePlay Host')
            self.addCleanup(self.pair.close)
            self.pair.start(); left, right = self.client(1), self.client(2)
            launches = self.pair.broker.launches
            for slot, sock in ((1, left), (2, right)):
                launch, peer = launches[slot - 1], launches[2 - slot]
                hello = bytes((launch.edition, peer.edition, 0, 0)) + PINS[launch.edition] + launch.content + launch.native_identity
                self.send(sock, slot, 1, 1, hello)
            for slot, sock in ((1, left), (2, right)):
                fields, body = self.receive(sock, slot); self.assertEqual(fields[2], 2); self.assertTrue(body)
            payload = b'authored opaque native event\0\xff'
            self.send(left, 1, 3, 2, payload)
            fields, body = self.receive(right, 2)
            self.assertEqual(fields[2], 3); self.assertEqual(fields[5:7], (1, 2)); self.assertEqual(body, payload)
            self.pair.close(); self.pair.runtime.check()
            self.assertTrue(self.pair.gold.closed)
            with self.assertRaises(ValueError): self.pair.invitation(ROOM)

    def test_failed_gold_service_construction_releases_the_already_owned_broker(self):
        original = host.CrossplayBroker; created = []
        def create(*args, **kwargs):
            result = original(*args, **kwargs); created.append(result); return result
        with patch.object(host, 'CrossplayBroker', side_effect=create), \
                patch.object(host, 'gold_service_class', return_value=Mock(side_effect=RuntimeError('fixture'))):
            with self.assertRaises(RuntimeError): host.Host(self.fixture.roots[1], self.catalog, history=2, name='Host')
        self.assertEqual(len(created), 1); self.assertEqual(created[0].listener.fileno(), -1)

    def test_rejects_bad_or_noncanonical_invites_and_impossible_host_roles(self):
        valid = self.pair.invitation(ROOM).token(); body = valid[len(invitation.PREFIX):]
        data = json.loads(base64.urlsafe_b64decode(body + '=' * (-len(body) % 4)))
        variants = [None, '', 'weplay1.invalid', valid + '=', valid + '!', 'x' * 2049]
        for field, value in (('v', True), ('edition', 1), ('key', 'A' * 64), ('identity', '0' * 32),
                              ('content', '0' * 64), ('room', 'invalid'), ('extra', 1)):
            raw = json.dumps({**data, field: value}, sort_keys=True, separators=(',', ':')).encode()
            variants.append(invitation.PREFIX + base64.urlsafe_b64encode(raw).decode().rstrip('='))
        raw = json.dumps(data, indent=2).encode()
        variants.append(invitation.PREFIX + base64.urlsafe_b64encode(raw).decode().rstrip('='))
        for value in variants:
            with self.assertRaisesRegex(ValueError, 'invitation_invalid'): invitation.parse(value)
        for edition in (1, True, 4, '3'):
            with self.assertRaisesRegex(ValueError, 'configuration_invalid'):
                host.Host(self.fixture.roots[1], self.catalog, history=edition, name='Host')


if __name__ == '__main__': unittest.main()
