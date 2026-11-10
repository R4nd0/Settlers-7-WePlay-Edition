"""Synthetic protocol/state tests; no game files, recorded packets or accounts."""
from pathlib import Path
import hashlib
import hmac
import random
import struct
import sys
import unittest
import zlib

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/gold"))
import login_server as gold


def incoming(kind, flags=32, session=0, signature=0, sequence=0, payload=b"", connection=0):
    data = gold.encode(kind, flags, session, signature, sequence, payload, connection)
    data = b"\x3f\x31" + data[2:-1]
    return data + bytes((gold.checksum(data),))


def request(protocol, call, method, args):
    body = bytes((protocol | 128,)) + struct.pack("<II", call, method) + args
    return gold.wrap(struct.pack("<I", len(body)) + body)


def login_args(name="local-fixture", password="a" * 64):
    custom = gold.text(name) + gold.text("") + gold.text(password)
    return (gold.text("fixture-ticket-name") + gold.text(gold.CLASS_NAME.decode())
            + struct.pack("<II", len(custom) + 4, len(custom)) + custom)


class GoldTests(unittest.TestCase):
    def setUp(self):
        self.adapter = gold.GoldLoginAdapter("local-fixture", "a" * 64, 55123)
        self.auth = "127.0.0.1", 51000
        self.backend = "127.0.0.1", 51001
        self.now = 10.

    def connect(self, backend=False):
        peer = self.backend if backend else self.auth
        replies = self.adapter.handle(backend, peer, incoming(0), self.now)
        self.assertEqual(len(replies), 1)
        server = int.from_bytes(replies[0][10:14], "little")
        payload = b""
        if backend:
            plain = struct.pack("<III", self.adapter.principal, 1, 0xffffffff)
            encrypted = gold.rc4(plain, self.adapter.session_key)
            encrypted += hmac.digest(self.adapter.session_key, encrypted, "md5")
            opaque = self.adapter.opaque_ticket
            payload = gold.wrap(struct.pack("<I", len(opaque)) + opaque + struct.pack("<I", len(encrypted)) + encrypted)
        data = incoming(1, 48, 17, server, 1, payload, 0x12345678)
        replies = self.adapter.handle(backend, peer, data, self.now)
        self.assertEqual(len(replies), 1)
        if backend:
            self.assertEqual(gold.unwrap(replies[0][16:-1]), struct.pack("<II", 4, 0))
        return server

    def login(self):
        server = self.connect()
        data = incoming(2, 48, 17, server, 2, request(10, 1, 2, login_args()))
        replies = self.adapter.handle(False, self.auth, data, self.now)
        self.assertEqual(len(replies), 2)
        self.assertEqual(gold.unwrap(replies[1][13:-1])[4:6], b"\x0a\x01")
        return server, data, replies

    def test_compatibility_cipher_independent_vector(self):
        self.assertEqual(gold.rc4(b"Plaintext", b"Key").hex(), "bbf316e8d940af0ad3")
        self.assertEqual(gold.checksum(bytes.fromhex("01020304"), 0), 10)
        self.assertEqual(gold.checksum(b"\xff" * 8, 0), 251)  # 32-bit word carry wraps.

    def test_envelope_limits(self):
        message = b"x" * gold.MAX_MESSAGE
        self.assertEqual(gold.unwrap(gold.rc4(b"\x01" + zlib.compress(message), gold.TRANSPORT_KEY)), message)
        for value in (b"", b"\x01" + zlib.compress(message + b"x"), b"\x01" + zlib.compress(b"ok") + b"trailing", b"\x01" + zlib.compress(b"ok")[:-1]):
            with self.assertRaises((ValueError, zlib.error)):
                gold.unwrap(gold.rc4(value, gold.TRANSPORT_KEY))

    def test_exact_credential_shape(self):
        self.assertTrue(self.adapter.credentials_match(login_args()))
        self.assertFalse(self.adapter.credentials_match(login_args(password="b" * 64)))
        self.assertFalse(self.adapter.credentials_match(login_args(name="other")))
        for args in (login_args() + b"x", login_args()[:-1], gold.text("local-fixture") * 5):
            try:
                self.assertFalse(self.adapter.credentials_match(args))
            except ValueError:
                pass

    def test_ticket_is_bound_to_principal_and_ephemeral_session(self):
        ticket = self.adapter.ticket()
        self.assertEqual(ticket[-16:], hmac.digest(self.adapter.ticket_key, ticket[:-16], "md5"))
        plain = gold.rc4(ticket[:-16], self.adapter.ticket_key)
        self.assertEqual(plain[:16], self.adapter.session_key)
        self.assertEqual(struct.unpack_from("<II", plain, 16), (1, 32))
        self.assertEqual(plain[24:], self.adapter.opaque_ticket)
        self.assertNotEqual(ticket, gold.GoldLoginAdapter("local-fixture", "a" * 64, 55123).ticket())

    def test_full_state_path_and_retransmission(self):
        server, data, replies = self.login()
        self.assertEqual(self.adapter.handle(False, self.auth, data, self.now), replies)
        self.assertEqual(self.adapter.events["login_answered"], 1)
        changed = incoming(2, 48, 17, server, 2, request(10, 9, 2, login_args()))
        self.assertEqual(self.adapter.handle(False, self.auth, changed, self.now), [])
        args = struct.pack("<II", self.adapter.principal, 1)
        ticket = self.adapter.handle(False, self.auth, incoming(2, 48, 17, server, 3, request(10, 2, 3, args)), self.now)
        self.assertEqual(len(ticket), 2)
        backend = self.connect(True)
        urls = struct.pack("<I", 1) + gold.text("prudps:/address=127.0.0.1;port=51001;sid=15;stream=3")
        registration = incoming(2, 48, 17, backend, 2, request(11, 3, 1, urls))
        replies = self.adapter.handle(True, self.backend, registration, self.now)
        self.assertEqual(len(replies), 2)
        self.assertTrue(self.adapter.registered)
        self.assertEqual(self.adapter.events["registered"], 1)
        self.assertEqual(self.adapter.handle(True, self.backend, registration, self.now), replies)
        self.assertEqual(self.adapter.events["registered"], 1)

    def test_rejected_login_does_not_admit_backend(self):
        server = self.connect()
        data = incoming(2, 48, 17, server, 2, request(10, 1, 2, login_args(password="b" * 64)))
        replies = self.adapter.handle(False, self.auth, data, self.now)
        self.assertEqual(gold.unwrap(replies[1][13:-1])[4:6], b"\x0a\0")
        self.assertIsNone(self.adapter.admitted_peer)
        self.assertEqual(self.adapter.handle(True, self.backend, incoming(0), self.now), [])

    def test_lost_response_retries_are_bounded_and_real_ack_stops_them(self):
        server, _, replies = self.login()
        self.assertEqual(self.adapter.retransmissions(10.2), [])
        self.assertEqual(self.adapter.retransmissions(10.25), [(False, self.auth, replies[1])])
        ack = incoming(2, 8, 17, server, 1)
        self.adapter.handle(False, self.auth, ack, 10.3)
        self.assertEqual(self.adapter.retransmissions(11), [])
        # A second instance receives no ACK: finite retries, no unbounded queue.
        self.setUp()
        self.login()
        count = 0
        for tick in range(1, 30):
            count += len(self.adapter.retransmissions(10 + tick * 2))
        self.assertEqual(count, 7)
        self.assertEqual(self.adapter.events["response_timeout"], 1)

    def test_disconnect_is_idempotent_and_revokes_transport(self):
        server, data, _ = self.login()
        bye = incoming(3, 48, 17, server, 3)
        response = self.adapter.handle(False, self.auth, bye, 11)
        self.assertEqual(len(response), 1)
        self.assertEqual(self.adapter.handle(False, self.auth, bye, 12), response)
        self.assertEqual(self.adapter.handle(False, self.auth, data, 12), [])
        self.assertEqual(self.adapter.retransmissions(13), [])

    def test_reject_changed_session_signature_order_and_expiry(self):
        server, _, _ = self.login()
        payload = request(10, 2, 3, struct.pack("<II", self.adapter.principal, 1))
        for session, signature, sequence in ((18, server, 3), (17, server ^ 1, 3), (17, server, 5)):
            self.assertEqual(self.adapter.handle(False, self.auth, incoming(2, 48, session, signature, sequence, payload), self.now), [])
        self.assertEqual(self.adapter.handle(False, self.auth, incoming(2, 48, 17, server, 3, payload), 200), [])
        self.assertEqual(self.adapter.handle(False, ("192.0.2.1", 51000), incoming(0), self.now), [])

    def test_challenge_tampering_and_cross_instance_replay(self):
        self.login()
        plain = struct.pack("<III", self.adapter.principal, 1, 5)
        encrypted = gold.rc4(plain, self.adapter.session_key)
        encrypted += hmac.digest(self.adapter.session_key, encrypted, "md5")
        message = struct.pack("<I", 32) + self.adapter.opaque_ticket + struct.pack("<I", 28) + encrypted
        self.assertEqual(gold.unwrap(self.adapter.challenge(gold.wrap(message))), struct.pack("<II", 4, 6))
        for index in (4, len(message) - 1):
            altered = bytearray(message)
            altered[index] ^= 1
            with self.assertRaises(ValueError):
                self.adapter.challenge(gold.wrap(bytes(altered)))
        with self.assertRaises(ValueError):
            gold.GoldLoginAdapter("local-fixture", "a" * 64, 55123).challenge(gold.wrap(message))

    def test_bounded_malformed_input(self):
        rng = random.Random(1122)
        for _ in range(10000):
            data = rng.randbytes(rng.randrange(0, 2500))
            self.assertEqual(self.adapter.handle(bool(rng.getrandbits(1)), self.auth, data, self.now), [])
        self.assertLessEqual(len(self.adapter.connections), 8)


if __name__ == "__main__":
    unittest.main()
