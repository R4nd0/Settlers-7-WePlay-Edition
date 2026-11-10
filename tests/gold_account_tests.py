"""Owned synthetic profile/transport fixtures, including loss and identity attacks."""
import http.client
from pathlib import Path
import struct
import sys
import threading
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/gold"))
import account_config
import account_profile
import login_server as gold
import gold_login_tests as fixture

request = fixture.request


def incoming(kind, flags=32, session=0, signature=0, sequence=0, payload=b"", connection=0, fragment=0):
    data = gold.encode(kind, flags, session, signature, sequence, payload, connection, fragment)
    data = b"\x3f\x31" + data[2:-1]
    return data + bytes((gold.checksum(data),))


class AccountTests(unittest.TestCase):
    connect = fixture.GoldTests.connect
    login = fixture.GoldTests.login

    def setUp(self):
        self.adapter = gold.GoldLoginAdapter("local-fixture", "a" * 64, 55123, account_mode=True)
        self.auth = "127.0.0.1", 51000
        self.backend = "127.0.0.1", 51001
        self.now = 10.

    def register(self):
        auth_server, _, _ = self.login()
        self.adapter.handle(False, self.auth, incoming(2, 8, 17, auth_server, 1), self.now)
        server = self.connect(True)
        urls = struct.pack("<I", 1) + gold.text("prudps:/address=127.0.0.1;port=51001;sid=15;stream=3")
        self.send(server, 2, 11, 1, urls)
        return server

    def send(self, server, sequence, protocol, method, args=b"", now=None):
        return self.adapter.handle(True, self.backend,
            incoming(2, 48, 17, server, sequence, request(protocol, sequence, method, args)),
            self.now if now is None else now)

    def arguments(self, replies, protocol, method):
        plain = b"".join(gold.unwrap(wire[13:-1]) for wire in replies[1:])
        self.assertEqual(int.from_bytes(plain[:4], "little"), len(plain) - 4)
        self.assertEqual(plain[4:6], bytes((protocol, 1)))
        self.assertEqual(int.from_bytes(plain[10:14], "little"), method | 0x8000)
        return plain[14:]

    def fragmented_upload(self, server, start, args):
        plain = gold.unwrap(request(111, start, 5, args))
        chunks = [plain[i:i + 700] for i in range(0, len(plain), 700)]
        frames = []
        replies = []
        for index, part in enumerate(chunks):
            frame = incoming(2, 48, 17, server, start + index, gold.wrap(part),
                             fragment=index + 1 if index < len(chunks) - 1 else 0)
            frames.append(frame)
            replies = self.adapter.handle(True, self.backend, frame, self.now)
            self.assertEqual(len(replies), 1 if index < len(chunks) - 1 else 2)
            self.assertEqual(replies[0][10], frame[10])
        return start + len(chunks), frames, replies

    def test_unregistered_and_default_sdk_mode_do_not_read_profile(self):
        self.login()
        server = self.connect(True)
        denied = self.send(server, 2, 111, 2, b"\0" * 12)
        self.assertEqual(gold.unwrap(denied[1][13:-1])[4:6], b"\x6f\0")
        self.assertNotIn("profile_read", self.adapter.events)
        self.assertIsNone(gold.GoldLoginAdapter("local-fixture", "a" * 64, 55123).profile)

    def test_fragmented_profile_round_trip_duplicate_reordering_and_acks(self):
        server = self.register()
        base = self.adapter.profile.base
        castle = bytes(range(256)) * 35
        args = base + struct.pack("<I", len(castle)) + castle
        next_sequence, frames, stored = self.fragmented_upload(server, 3, args)
        self.assertEqual(self.arguments(stored, 111, 5), base)
        self.assertEqual(self.adapter.profile.castle, castle)
        self.assertEqual(self.adapter.events["profile_stored"], 1)
        # Old fragment retry is ACKed without executing the update again.
        retry = self.adapter.handle(True, self.backend, frames[0], self.now)
        self.assertEqual(len(retry), 1)
        self.assertEqual(self.adapter.events["profile_stored"], 1)
        bad = incoming(2, 48, 17, server, 3, gold.wrap(b"changed"), fragment=1)
        self.assertEqual(self.adapter.handle(True, self.backend, bad, self.now), [])
        read = self.send(server, next_sequence, 111, 2, b"\0" * 12)
        self.assertGreater(len(read), 3)
        self.assertEqual(self.arguments(read, 111, 2), args)
        # Clear earlier registration/update responses; then lose one real ACK.
        state = self.adapter.connections[True, self.backend]
        for sequence, (wire, _, _) in list(state.pending.items()):
            if wire in read[1:]:
                continue
            self.adapter.handle(True, self.backend, incoming(2, 8, 17, server, sequence, fragment=wire[10]), self.now)
        lost = read[2]
        for wire in read[1:]:
            if wire is lost:
                continue
            sequence = int.from_bytes(wire[8:10], "little")
            # A mismatched fragment ACK cannot cancel a pending packet.
            wrong = incoming(2, 8, 17, server, sequence, fragment=wire[10] ^ 64)
            self.adapter.handle(True, self.backend, wrong, self.now)
            self.assertIn(sequence, state.pending)
            self.adapter.handle(True, self.backend, incoming(2, 8, 17, server, sequence, fragment=wire[10]), self.now)
        self.assertEqual(self.adapter.retransmissions(10.25), [(True, self.backend, lost)])
        sequence = int.from_bytes(lost[8:10], "little")
        self.adapter.handle(True, self.backend, incoming(2, 8, 17, server, sequence, fragment=lost[10]), 10.3)
        self.assertEqual(self.adapter.retransmissions(11), [])

    def test_missing_fragment_and_expired_assembly_never_store_profile(self):
        server = self.register()
        second = incoming(2, 48, 17, server, 4, gold.wrap(b"later"), fragment=2)
        self.assertEqual(self.adapter.handle(True, self.backend, second, 10), [])
        first = incoming(2, 48, 17, server, 3, gold.wrap(b"first"), fragment=1)
        self.assertEqual(len(self.adapter.handle(True, self.backend, first, 10)), 1)
        self.assertEqual(self.adapter.handle(True, self.backend, second, 21), [])
        self.assertTrue(self.adapter.connections[True, self.backend].closed)
        self.assertNotIn("profile_stored", self.adapter.events)

    def test_registration_on_another_connection_does_not_authorize_profile(self):
        self.register()
        self.backend = "127.0.0.1", 51002
        server = self.connect(True)  # Same owned ticket; no registration here.
        denied = self.send(server, 2, 111, 2, b"\0" * 12)
        self.assertEqual(gold.unwrap(denied[1][13:-1])[4:6], b"\x6f\0")
        self.assertNotIn("profile_read", self.adapter.events)

    def test_sequence_exhaustion_does_not_commit_profile_update(self):
        server = self.register()
        state = self.adapter.connections[True, self.backend]
        state.next_out = 0x10000 - 63
        original = self.adapter.profile.base, self.adapter.profile.castle
        args = self.adapter.profile.base + struct.pack("<I", 4) + b"test"
        self.assertEqual(self.send(server, 3, 111, 5, args), [])
        self.assertTrue(state.closed)
        self.assertEqual((self.adapter.profile.base, self.adapter.profile.castle), original)

    def test_identity_truncation_and_length_attacks_are_atomic(self):
        local = self.adapter.profile
        other = account_profile.LocalProfile("another-local-identity")
        old = local.base, local.castle, local.progress
        for bad in (other.base + b"\0" * 4, local.base[:-1],
                    local.base + struct.pack("<I", 32769), local.base + b"\0" * 4 + b"trailing"):
            with self.assertRaises((ValueError, UnicodeError)):
                local.response(111, 5, bad, gold.Reader)
            self.assertEqual((local.base, local.castle, local.progress), old)
        with self.assertRaises(ValueError):
            local.response(111, 7, struct.pack("<I", 32769), gold.Reader)
        self.assertEqual(local.progress, b"")

    def test_progress_and_title_options_are_owned_and_exact(self):
        local = self.adapter.profile
        content = b"owned-synthetic-progress"
        data = struct.pack("<I", len(content)) + content
        self.assertEqual(local.response(111, 7, data, gold.Reader)[0], b"")
        self.assertEqual(local.response(111, 8, b"", gold.Reader)[0], data)
        self.assertEqual(local.response(111, 9, b"", gold.Reader)[0], b"\0" * 20)
        local.response(117, 3, b"\1", gold.Reader)
        self.assertEqual(local.response(117, 1, b"", gold.Reader)[0], b"\1" + b"\0" * 4)
        for bad in (b"", b"\2", b"\1trailing"):
            with self.assertRaises(ValueError):
                local.response(117, 3, bad, gold.Reader)
        self.assertTrue(local.title_option)
        self.assertIsNone(local.response(120, 1, b"", gold.Reader))


class ConfigTests(unittest.TestCase):
    def test_native_route_host_and_body_bounds(self):
        with account_config.ConfigService(55123) as server:
            def fetch(path, host=None, body=None):
                worker = threading.Thread(target=server.handle_request)
                worker.start()
                try:
                    connection = http.client.HTTPConnection("127.0.0.1", server.server_port, timeout=2)
                    headers = {} if host is None else {"Host": host}
                    connection.request("GET", path, body=body, headers=headers)
                    response = connection.getresponse()
                    result = response.status, response.read()
                    connection.close()
                    return result
                finally:
                    worker.join(2)
                    self.assertFalse(worker.is_alive())
            status, body = fetch(account_config.ROUTE)
            self.assertEqual(status, 200)
            self.assertIn(b'"Name":"SandboxUrl"', body)
            self.assertIn(b"address=127.0.0.1;port=55123", body)
            for path, host, body in (("/elsewhere", None, None),
                                     (account_config.ROUTE, "external.invalid", None),
                                     (account_config.ROUTE, None, b"body")):
                self.assertEqual(fetch(path, host, body)[0], 400)
            self.assertEqual(server.responses, 1)


if __name__ == "__main__":
    unittest.main()
