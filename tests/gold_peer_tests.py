"""Synthetic independent clients over actual loopback sockets; no game evidence."""
from dataclasses import replace
import hashlib
import hmac
import http.client
import json
from pathlib import Path
import socket
import struct
import sys
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/gold"))
import account_config
import gathering_codec as codec
import login_server as gold
import peer_server
import gold_login_tests as fixture


class Client:
    def __init__(self, service, index):
        self.service = service
        self.endpoint = service.endpoints[index]
        self.sockets = [socket.socket(socket.AF_INET, socket.SOCK_DGRAM) for _ in range(2)]
        for sock in self.sockets:
            sock.bind(("127.0.0.1", 0))
            sock.settimeout(1)
        self.servers = [0, 0]
        self.sequences = [2, 2]

    def close(self):
        for sock in self.sockets:
            sock.close()

    def exchange(self, backend, wire, count):
        sock = self.sockets[backend]
        sock.sendto(wire, ("127.0.0.1", self.endpoint["backend_port" if backend else "port"]))
        self.service.pump(0)
        return [sock.recv(4096) for _ in range(count)]

    def connect(self, backend=False, payload=b""):
        reply, = self.exchange(backend, fixture.incoming(0), 1)
        self.servers[backend] = int.from_bytes(reply[10:14], "little")
        return self.exchange(backend, fixture.incoming(1, 48, 17,
            self.servers[backend], 1, payload, 0x12345678), 1)

    def rpc(self, backend, protocol, method, args=b""):
        sequence = self.sequences[backend]
        self.sequences[backend] += 1
        replies = self.exchange(backend, fixture.incoming(2, 48, 17,
            self.servers[backend], sequence, fixture.request(protocol, sequence, method, args)), 2)
        # Actual client ACK; no pending response is acknowledged by the service itself.
        output_sequence = int.from_bytes(replies[1][8:10], "little")
        self.exchange(backend, fixture.incoming(2, 8, 17,
            self.servers[backend], output_sequence), 0)
        return gold.unwrap(replies[1][13:-1])

    def login(self, name, secret):
        self.connect()
        plain = self.rpc(False, 10, 2, fixture.login_args(name, secret))
        if plain[4:6] != b"\x0a\1":
            return False
        result, self.principal, length = struct.unpack_from("<III", plain, 14)
        assert result == 0x10001
        ticket = plain[26:26 + length]
        key = b"UbiDummyPwd"
        for _ in range(65000 + self.principal % 1024):
            key = hashlib.md5(key).digest()
        assert hmac.compare_digest(ticket[-16:], hmac.digest(key, ticket[:-16], "md5"))
        decoded = gold.rc4(ticket[:-16], key)
        self.session_key, opaque = decoded[:16], decoded[24:]
        encrypted = gold.rc4(struct.pack("<III", self.principal, 1, 7), self.session_key)
        encrypted += hmac.digest(self.session_key, encrypted, "md5")
        self.challenge = gold.wrap(struct.pack("<I", len(opaque)) + opaque
            + struct.pack("<I", len(encrypted)) + encrypted)
        reply, = self.connect(True, self.challenge)
        assert gold.unwrap(reply[16:-1]) == struct.pack("<II", 4, 8)
        registration = self.rpc(True, 11, 1, struct.pack("<I", 1)
            + gold.text("prudps:/address=127.0.0.1;port=50000;RVCID=0;sid=15;stream=3"))
        assert registration[4:6] == b"\x0b\1"
        return True


class PeerTests(unittest.TestCase):
    peers = [{"name": "fixture-host", "secret": "a" * 64},
        {"name": "fixture-guest", "secret": "b" * 64}]

    def setUp(self):
        self.service = peer_server.PeerLoginService(self.peers)
        self.addCleanup(self.service.__exit__)
        self.clients = [Client(self.service, i) for i in range(2)]
        for client in self.clients:
            self.addCleanup(client.close)
        self.value = codec.SessionGathering(codec.Gathering((0,) * 7, (0, 4), None, b""),
            ("map", "hash", "template", "host"), (0,) * 6, (False,) * 4)

    def authenticate(self):
        for client, peer in zip(self.clients, self.peers):
            self.assertTrue(client.login(peer["name"], peer["secret"]))

    def test_rendezvous_connection_identifiers_cannot_alias_another_peer(self):
        # Independent principals alone are insufficient: native echo uses RVCID.
        with patch.object(gold.secrets, "randbelow", side_effect=[10, 20, 11, 20]):
            with self.assertRaisesRegex(ValueError, "Local connection collision"):
                peer_server.PeerLoginService(self.peers)

    def host(self, client):
        value = replace(self.value, strings=("fixture-app", "fixture-data", "InLobby", "fixture-map"),
            words=(4, 3, 0, 0, 16, 0))
        identifier = int.from_bytes(client.rpc(True, 21, 1, codec.encode(value))[14:], "little")
        self.assertEqual(client.rpc(True, 21, 11,
            struct.pack("<I", identifier) + gold.text(""))[14:], b"\1")
        self.assertEqual(client.rpc(True, 21, 40, struct.pack("<I", identifier))[4:6], b"\x15\1")
        return identifier

    def test_native_catalogue_lists_real_active_host_and_removes_released_session(self):
        self.authenticate()
        host, guest = self.clients
        query = struct.pack("<III", 0, 20, 16)
        self.assertEqual(guest.rpc(True, 121, 1, query)[14:], b"\0" * 4)
        identifier = self.host(host)
        value = self.service.gatherings.records[identifier].value
        self.assertEqual(guest.rpc(True, 121, 1, query)[14:], struct.pack("<I", 1) + codec.encode(value))
        self.assertEqual(guest.rpc(True, 121, 1, struct.pack("<III", 1, 20, 16))[14:], b"\0" * 4)
        self.assertEqual(guest.rpc(True, 121, 5, b"\5" + struct.pack("<I", 16))[14:], b"\0" * 4)
        self.assertEqual(host.rpc(True, 21, 12, struct.pack("<I", identifier) + gold.text(""))[14:], b"\1")
        self.assertEqual(guest.rpc(True, 121, 1, query)[14:], b"\0" * 4)

    def test_catalogue_excludes_private_started_full_mismatched_and_expired_records(self):
        self.authenticate()
        identifier = self.host(self.clients[0])
        record = self.service.gatherings.records[identifier]
        registry = self.service.gatherings
        now = 0  # Both actually authenticated connections remain live.
        self.assertEqual(registry.catalogue(0, 20, 16, now), (record.value,))
        variants = [replace(record, host_claimed=False), replace(record, participating=False),
            replace(record, released=True),
            replace(record, value=replace(record.value, words=(4, 3, 0, 4, 16, 0))),
            replace(record, value=replace(record.value, words=(4, 0, 3, 0, 16, 0))),
            replace(record, value=replace(record.value, words=(4, 3, 0, 0, 17, 0))),
            replace(record, value=replace(record.value, strings=("app", "data", "InGame", "map"))),
            replace(record, value=replace(record.value, strings=("", "data", "InLobby", "map")))]
        for variant in variants:
            registry.records[identifier] = variant
            self.assertEqual(registry.catalogue(0, 20, 16, now), ())
        registry.records[identifier] = record
        self.assertEqual(registry.catalogue(0, 20, 16, record.owner.expires), ())

    def test_catalogue_malformed_or_unqualified_query_never_mutates_host(self):
        self.authenticate()
        identifier = self.host(self.clients[0])
        record = self.service.gatherings.records[identifier]
        guest = self.clients[1]
        for method, args in [(1, struct.pack("<III", offset, count, version))
                for offset, count, version in ((4097, 20, 16), (0, 21, 16), (0, 20, 17))] + [
                (1, b""), (1, struct.pack("<III", 0, 20, 16) + b"x"),
                (5, b"\4" + struct.pack("<I", 16)), (5, b"\5" + struct.pack("<I", 17)),
                (5, b"\5" + struct.pack("<I", 16) + b"x")]:
            self.assertEqual(guest.rpc(True, 121, method, args)[4:6], b"\x79\0")
            self.assertIs(self.service.gatherings.records[identifier], record)
        self.assertEqual(guest.rpc(True, 121, 1, struct.pack("<III", 0, 0, 16))[14:], b"\0" * 4)

    def test_native_endpoint_query_uses_observed_host_and_revokes_on_leave(self):
        self.authenticate()
        host, guest = self.clients
        identifier = self.host(host)
        record = self.service.gatherings.records[identifier]
        response = guest.rpc(True, 21, 16, struct.pack("<I", identifier))
        self.assertEqual(response[4:6], b"\x15\1")
        reader = gold.Reader(response[14:])
        self.assertEqual(reader.u32(), 1)
        url = reader.text(256).decode("ascii")
        reader.finish()
        self.assertEqual(url, record.owner.public_url)
        self.assertIn(f"port={host.sockets[1].getsockname()[1]};", url)
        self.assertIn(f"PID={host.principal};", url)
        self.assertIn(f"RVCID={self.service.adapters[0].connection_id};", url)
        self.assertNotIn(f"RVCID={self.service.adapters[1].connection_id};", url)
        self.assertNotIn("RVCID=0;", url)
        self.assertNotIn("port=50000;", url)  # Authored request's station is not authority.
        for args in (b"", struct.pack("<I", identifier) + b"x", struct.pack("<I", identifier + 1)):
            self.assertEqual(guest.rpc(True, 21, 16, args)[4:6], b"\x15\0")
            self.assertIs(self.service.gatherings.records[identifier], record)
        host.rpc(True, 21, 12, struct.pack("<I", identifier) + gold.text(""))
        self.assertEqual(guest.rpc(True, 21, 16, struct.pack("<I", identifier))[4:6], b"\x15\0")

    def test_independent_login_ticket_profile_and_shared_owner_authority(self):
        self.authenticate()
        first, second = self.clients
        self.assertNotEqual(first.principal, second.principal)
        self.assertNotEqual(first.session_key, second.session_key)
        adapters = self.service.adapters
        self.assertIsNot(adapters[0].profile, adapters[1].profile)
        self.assertIs(adapters[0].gatherings, adapters[1].gatherings)
        created = first.rpc(True, 21, 1, codec.encode(self.value))
        identifier, = struct.unpack("<I", created[14:])
        record = self.service.gatherings.records[identifier]
        self.assertEqual(record.principal, first.principal)
        # Another fully authenticated client is still not the owner.
        for method in (21, 40):
            denied = second.rpc(True, 21, method, struct.pack("<I", identifier))
            self.assertEqual(denied[4:6], b"\x15\0")
        self.assertEqual(second.rpc(True, 21, 2, struct.pack("<I", identifier))[14:], b"\0")
        self.assertIs(self.service.gatherings.records[identifier], record)
        other_created = second.rpc(True, 21, 1, codec.encode(self.value))
        other_id, = struct.unpack("<I", other_created[14:])
        self.assertGreater(other_id, identifier)
        self.assertEqual(len(self.service.gatherings.records), 2)
        first.exchange(True, fixture.incoming(3, 48, 17, first.servers[1], 4), 1)
        self.assertEqual(set(self.service.gatherings.records), {other_id})
        self.assertEqual(second.rpc(True, 21, 21, struct.pack("<I", other_id))[4:6], b"\x15\1")
        # Reports expose bounded state/schema, not launch data or principals.
        summary = json.dumps(self.service.summary())
        for peer in self.peers:
            self.assertNotIn(peer["name"], summary)
            self.assertNotIn(peer["secret"], summary)
        self.assertFalse(self.service.summary()["game_session_admission"])

    def test_other_launch_secret_and_ticket_are_rejected(self):
        first, second = self.clients
        self.assertFalse(first.login(self.peers[0]["name"], self.peers[1]["secret"]))
        self.assertFalse(self.service.adapters[0].registered)
        # Use fresh socket/connection after the rejected credential request.
        first.close()
        self.clients[0] = first = Client(self.service, 0)
        self.addCleanup(first.close)
        self.assertTrue(first.login(self.peers[0]["name"], self.peers[0]["secret"]))
        self.assertTrue(second.login(self.peers[1]["name"], self.peers[1]["secret"]))
        with self.assertRaises(ValueError):
            self.service.adapters[1].challenge(first.challenge)

    def test_each_native_configuration_points_to_its_own_auth_socket(self):
        ports = set()
        for endpoint in self.service.endpoints:
            ports.update(endpoint.values())
            request = http.client.HTTPConnection("127.0.0.1", endpoint["config_port"], timeout=1)
            try:
                request.request("GET", account_config.ROUTE)
                self.service.pump(.1)
                response = request.getresponse()
                self.assertEqual(response.status, 200)
                data = json.loads(response.read())
                self.assertIn(f"port={endpoint['port']};", data[0]["Values"][0])
            finally:
                request.close()
        self.assertEqual(len(ports), 6)

    def test_expiring_one_identity_preserves_other_owner(self):
        self.authenticate()
        identifiers = [int.from_bytes(client.rpc(True, 21, 1,
            codec.encode(self.value))[14:], "little") for client in self.clients]
        first, second = self.service.adapters
        self.assertEqual(len(self.service.gatherings.records), 2)
        first.retransmissions(max(c.expires for c in first.connections.values()) + 1)
        self.assertEqual(set(self.service.gatherings.records), {identifiers[1]})
        self.assertTrue(second.registered)

    def test_invalid_configuration_is_rejected_before_socket_allocation(self):
        invalid = [None, [], self.peers[:1], self.peers * 5,
            [self.peers[0], self.peers[0]],
            [self.peers[0], {"name": "FIXTURE-HOST", "secret": "b" * 64}],
            [self.peers[0], {"name": "guest", "secret": "a" * 64}],
            [self.peers[0], {"name": "guest\n", "secret": "b" * 64}],
            [self.peers[0], {"name": "guest", "secret": "b" * 63}],
            [self.peers[0], {"name": "guest", "secret": "b" * 64, "extra": True}]]
        with patch.object(peer_server.socket, "socket") as allocate:
            for value in invalid:
                with self.assertRaises(ValueError):
                    peer_server.PeerLoginService(value)
            allocate.assert_not_called()

    def test_shared_registry_requires_explicit_authenticated_session_mode(self):
        for account_mode, session_mode in ((False, False), (True, False), (False, True)):
            with self.assertRaises(ValueError):
                gold.GoldLoginAdapter("fixture", "c" * 64, 55123,
                    account_mode=account_mode, session_mode=session_mode,
                    gatherings=self.service.gatherings)


if __name__ == "__main__":
    unittest.main()
