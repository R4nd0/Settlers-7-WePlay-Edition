"""Adversarial loopback tests for bridge ownership, bounds and retired epochs."""
import hmac
from pathlib import Path
import secrets
import socket
import sys
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/gold'))
from crossplay_server import CrossplayBroker, HEADER, Launch, MAX_PAYLOAD, PINS


class BrokerTests(unittest.TestCase):
    def setUp(self):
        self.launches = (Launch(1, secrets.token_bytes(32), bytes((42,))+bytes(15), bytes((0x51,))*32),
                         Launch(2, secrets.token_bytes(32), bytes((0x22,))*16, bytes((0x51,))*32))
        self.broker = CrossplayBroker(self.launches)
        self.sockets = []

    def tearDown(self):
        self.broker.close()
        for sock in self.sockets:
            sock.close()

    def client(self, slot):
        sock = socket.socket(socket.AF_INET, socket.SOCK_STREAM)
        sock.bind(('127.0.0.'+str(slot+1), 0)); sock.connect(('127.0.0.1', self.broker.port))
        self.sockets.append(sock)
        self.broker.pump(0)
        return sock

    def hello(self, slot):
        launch, peer = self.launches[slot-1], self.launches[2-slot]
        return bytes((launch.edition, peer.edition, 0, 0))+PINS[launch.edition]+launch.content+launch.native_identity

    def frame(self, slot, kind=1, sequence=1, body=None, session=None, length=None):
        body = self.hello(slot) if body is None else body
        header = HEADER.pack(b'WPX1', 1, kind, self.broker.session if session is None else session,
                             sequence, slot, 0 if kind == 1 else 3-slot, 0, len(body) if length is None else length)
        value = header+body
        return value+hmac.digest(self.launches[slot-1].key, value, 'sha256')

    def pump(self, predicate):
        deadline = time.monotonic()+2
        while not predicate() and time.monotonic() < deadline:
            self.broker.pump(.001)
        self.assertTrue(predicate())

    def rejected(self, sock, frame):
        prior = self.broker.rejections
        sock.sendall(frame)
        self.pump(lambda: self.broker.rejections > prior)

    def test_old_session_proof_rejected(self):
        session = bytes((self.broker.session[0] ^ 128,))+self.broker.session[1:]
        self.rejected(self.client(1), self.frame(1, session=session))
        self.assertFalse(self.broker.members)
        self.assertFalse(self.broker.retired)

    def test_accepted_stream_disables_tcp_batching_without_authenticating_peer(self):
        self.client(1)
        self.assertEqual(len(self.broker.connections),1)
        connection=next(iter(self.broker.connections.values()))
        self.assertEqual(connection.sock.getsockopt(socket.IPPROTO_TCP,socket.TCP_NODELAY),1)
        self.assertFalse(connection.authenticated);self.assertFalse(self.broker.members)

    def test_authenticated_profile_cannot_be_changed(self):
        body = self.hello(1)
        self.rejected(self.client(1), self.frame(1, body=body[:-16]+bytes((43,))+bytes(15)))
        self.assertFalse(self.broker.members)

    def test_duplicate_slot_keeps_original_owner(self):
        first = self.client(1); first.sendall(self.frame(1))
        self.pump(lambda: 1 in self.broker.members)
        owner = self.broker.members[1]
        self.rejected(self.client(1), self.frame(1))
        self.assertIs(self.broker.members[1], owner)
        self.assertFalse(self.broker.retired)

    def test_source_address_cannot_claim_other_slot(self):
        self.rejected(self.client(2), self.frame(1))
        self.assertFalse(self.broker.members)

    def test_oversized_frame_rejected_before_payload(self):
        self.rejected(self.client(1), self.frame(1, body=b'', length=MAX_PAYLOAD+1)[:HEADER.size])
        self.assertFalse(self.broker.members)

    def test_event_before_peer_authentication_retires_owner(self):
        sock = self.client(1); sock.sendall(self.frame(1))
        self.pump(lambda: 1 in self.broker.members)
        self.rejected(sock, self.frame(1, 3, 2, b'synthetic'))
        self.assertTrue(self.broker.retired)
        self.assertEqual(self.broker.relayed, 0)

    def test_replayed_sequence_cannot_relay(self):
        first, second = self.client(1), self.client(2)
        first.sendall(self.frame(1)); second.sendall(self.frame(2))
        self.pump(lambda: len(self.broker.members) == 2)
        self.rejected(first, self.frame(1, 3, 1, b'synthetic'))
        self.assertEqual(self.broker.relayed, 0)
        self.assertTrue(self.broker.retired)

    def test_retired_session_cannot_be_rejoined(self):
        first = self.client(1); first.sendall(self.frame(1))
        self.pump(lambda: 1 in self.broker.members)
        first.close(); self.pump(lambda: self.broker.retired)
        prior = self.broker.rejections
        self.client(1)
        self.pump(lambda: self.broker.rejections > prior)
        self.assertFalse(self.broker.members)

    def test_content_disagreement_rejected_at_creation(self):
        other = self.launches[1]
        with self.assertRaises(ValueError):
            CrossplayBroker((self.launches[0], Launch(other.edition, other.key, other.native_identity, bytes((2,))*32)))

    def test_fragmented_header_is_not_an_authentication(self):
        sock = self.client(1); value = self.frame(1)
        sock.sendall(value[:39]); self.broker.pump(.01)
        self.assertFalse(self.broker.members)
        sock.sendall(value[39:-1]); self.broker.pump(.01)
        self.assertFalse(self.broker.members)
        sock.sendall(value[-1:]); self.pump(lambda: 1 in self.broker.members)

    def test_tiny_event_backlog_drains_before_eof(self):
        first, second = self.client(1), self.client(2)
        first.sendall(self.frame(1)); second.sendall(self.frame(2))
        self.pump(lambda: len(self.broker.members) == 2)
        first.sendall(b''.join(self.frame(1, 3, i+2, bytes((i & 255,))) for i in range(512)))
        first.shutdown(socket.SHUT_WR)
        self.pump(lambda: self.broker.retired)
        self.assertEqual(self.broker.relayed, 512)
        self.assertEqual(self.broker.rejections, 0)

    def test_truncated_event_at_eof_rejected(self):
        first = self.client(1); first.sendall(self.frame(1))
        self.pump(lambda: 1 in self.broker.members)
        first.sendall(self.frame(1, 3, 2, b'pending')[:15]); first.shutdown(socket.SHUT_WR)
        self.pump(lambda: self.broker.retired)
        self.assertEqual(self.broker.rejections, 1)


if __name__ == '__main__':
    unittest.main()
