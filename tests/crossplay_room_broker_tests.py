"""Actual three-socket loopback transport tests; no original game is started."""
import hmac
from pathlib import Path
import secrets
import socket
import sys
import time
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1]/'tools/installer'))
from crossplay_broker import CrossplayBroker, HEADER, Launch, MAX_PAYLOAD, PINS
from crossplay_room_broker import CrossplayRoomBroker


class RoomBrokerTests(unittest.TestCase):
    def setUp(self):
        self.launches = tuple(Launch(i, secrets.token_bytes(32), bytes((42,))+bytes(15) if i == 1 else bytes((i+20,))*16, bytes((0x51,))*32) for i in (1, 2, 3))
        self.broker = CrossplayRoomBroker(self.launches, host_edition=3, generation=7)
        self.sockets = []

    def tearDown(self):
        self.broker.close()
        for sock in self.sockets:
            sock.close()

    def client(self, slot):
        sock = socket.socket();sock.bind(('127.0.0.'+str(slot+1), 0));sock.connect(('127.0.0.1', self.broker.port));sock.setblocking(False)
        self.sockets.append(sock);self.broker.pump(0);return sock

    def frame(self, slot, kind=1, sequence=1, body=None, target=0, generation=7, magic=b'WPX2', version=2):
        launch = self.launches[slot-1]
        if body is None:
            body = bytes((slot, 3, self.broker.host_slot, 0))+generation.to_bytes(4, 'little')+PINS[slot]+launch.content+launch.native_identity
        value = HEADER.pack(magic, version, kind, self.broker.session, sequence, slot, target, 0, len(body))+body
        return value+hmac.digest(launch.key, value, 'sha256')

    def pump(self, condition):
        deadline = time.monotonic()+2
        while not condition() and time.monotonic() < deadline:
            self.broker.pump(.001)
        self.assertTrue(condition())

    def receive(self, sock, slot, count=1):
        incoming = bytearray();frames = [];deadline = time.monotonic()+2
        while len(frames) < count and time.monotonic() < deadline:
            self.broker.pump(.001)
            try:
                data = sock.recv(65536)
            except BlockingIOError:
                continue
            self.assertTrue(data);incoming.extend(data)
            while len(incoming) >= HEADER.size:
                header = HEADER.unpack_from(incoming);size = HEADER.size+header[-1]+32
                if len(incoming) < size:
                    break
                frame = bytes(incoming[:size]);del incoming[:size]
                self.assertTrue(hmac.compare_digest(hmac.digest(self.launches[slot-1].key, frame[:-32], 'sha256'), frame[-32:]))
                self.assertEqual((header[0], header[1], header[6]), (b'WPX2', 2, slot))
                frames.append((header, frame[HEADER.size:-32]))
        self.assertEqual(len(frames), count);self.assertFalse(incoming);return frames

    def connected(self):
        sockets = [self.client(i) for i in (1, 2, 3)]
        for i, sock in enumerate(sockets, 1):
            sock.sendall(self.frame(i))
        self.pump(lambda: len(self.broker.members) == 3)
        for i, sock in enumerate(sockets, 1):
            header, body = self.receive(sock, i)[0]
            self.assertEqual((header[2], header[5]), (2, 0));self.assertEqual(len(body), 196)
            self.assertEqual(body[:4], bytes((3, self.broker.host_slot, i, 0)));self.assertEqual(int.from_bytes(body[4:8], 'little'), 7)
            self.assertEqual(body[8:40], self.launches[0].content)
            for member in range(1, 4):
                at = 40+(member-1)*52
                self.assertEqual(body[at:at+4], bytes((member, member, self.broker.logical_slots[member-1], 0)))
                self.assertEqual(body[at+4:at+20], self.broker.identities[member-1])
                self.assertEqual(body[at+36:at+52], self.broker.projections[i-1][member-1])
        return sockets

    def rejected(self, sock, frame):
        previous = self.broker.rejections;sock.sendall(frame);self.pump(lambda: self.broker.rejections > previous)

    def test_two_authenticated_clients_do_not_create_a_connected_three_member_room(self):
        for slot in (1, 2):
            self.client(slot).sendall(self.frame(slot))
        self.pump(lambda: len(self.broker.members) == 2)
        self.assertTrue(all(not member.output and member.sequence_out == 0 for member in self.broker.members.values()))

    def test_all_three_receive_one_common_room_and_each_native_namespace(self):
        self.connected();self.assertEqual(self.broker.logical_slots, (2, 3, 1));self.assertEqual(self.broker.rejections, 0)
        for view in self.broker.projections:
            self.assertEqual(len(set(view)), 3)

    def test_gold_game_host_has_the_same_three_member_transport(self):
        self.broker.close();self.broker = CrossplayRoomBroker(self.launches, host_edition=1, generation=7)
        self.connected();self.assertEqual(self.broker.logical_slots, (1, 2, 3));self.assertEqual(self.broker.rejections, 0)

    def test_broadcast_from_each_origin_reaches_both_other_members_without_echo(self):
        sockets = self.connected()
        for origin in (1, 2, 3):
            payload = b'authored-event-origin-'+bytes((origin,));sockets[origin-1].sendall(self.frame(origin, 3, 2, payload))
            for target in (1, 2, 3):
                if target == origin:
                    continue
                header, body = self.receive(sockets[target-1], target)[0]
                self.assertEqual((header[2], header[5]), (3, origin));self.assertEqual(body, payload)
        self.assertEqual(self.broker.relayed, 6)

    def test_targeted_event_does_not_fanout_to_unaddressed_member(self):
        sockets = self.connected();sockets[0].sendall(self.frame(1, 3, 2, b'only-steam', target=3))
        header, body = self.receive(sockets[2], 3)[0];self.assertEqual(header[5], 1);self.assertEqual(body, b'only-steam')
        self.assertEqual(self.broker.relayed, 1);self.assertEqual(self.broker.members[2].sequence_out, 1)

    def test_pair_protocol_and_wrong_generation_are_rejected(self):
        self.rejected(self.client(1), self.frame(1, magic=b'WPX1', version=1))
        self.rejected(self.client(2), self.frame(2, generation=6));self.assertFalse(self.broker.members)

    def test_tampered_authenticator_and_source_alias_are_rejected(self):
        forged = self.frame(1)[:-1]+bytes((self.frame(1)[-1] ^ 1,));self.rejected(self.client(1), forged)
        self.rejected(self.client(2), self.frame(1));self.assertFalse(self.broker.members)

    def test_event_before_third_member_retires_room(self):
        sock = self.client(1);sock.sendall(self.frame(1));self.pump(lambda: 1 in self.broker.members)
        self.rejected(sock, self.frame(1, 3, 2, b'not-a-native-result'));self.assertTrue(self.broker.retired);self.assertEqual(self.broker.relayed, 0)

    def test_duplicate_connection_does_not_steal_authenticated_member(self):
        sock = self.client(2);sock.sendall(self.frame(2));self.pump(lambda: 2 in self.broker.members);owner = self.broker.members[2]
        self.rejected(self.client(2), self.frame(2));self.assertIs(self.broker.members[2], owner);self.assertFalse(self.broker.retired)

    def test_replayed_event_cannot_relay(self):
        sockets = self.connected();self.rejected(sockets[0], self.frame(1, 3, 1, b'replayed'));self.assertTrue(self.broker.retired)

    def test_room_cannot_be_rejoined_after_one_member_leaves(self):
        sockets = self.connected();sockets[1].close();self.pump(lambda: self.broker.retired)
        previous = self.broker.rejections;self.client(2);self.pump(lambda: self.broker.rejections > previous)

    def test_queued_fanout_is_drained_before_peer_eof(self):
        sockets = self.connected()
        sockets[0].sendall(b''.join(self.frame(1, 3, i+2, bytes((i,))) for i in range(64)))
        sockets[0].shutdown(socket.SHUT_WR);self.pump(lambda: self.broker.retired)
        self.assertEqual(self.broker.relayed, 128);self.assertEqual(self.broker.rejections, 0)
        for target in (2, 3):
            messages = self.receive(sockets[target-1], target, count=64)
            self.assertEqual([body for _, body in messages], [bytes((i,)) for i in range(64)])

    def test_self_target_is_not_an_echo_or_broadcast(self):
        sockets = self.connected();self.rejected(sockets[0], self.frame(1, 3, 2, b'bad-self-target', target=1))
        self.assertEqual(self.broker.relayed, 0);self.assertTrue(self.broker.retired)

    def test_invalid_roster_or_host_is_rejected_without_opening_socket(self):
        for launches, host in ((self.launches[:2], 1), (self.launches, 2), (self.launches[::-1], 1)):
            with self.assertRaises(ValueError):
                CrossplayRoomBroker(launches, host_edition=host)
        with self.assertRaises(ValueError):
            CrossplayBroker(self.launches)

    def test_oversized_frame_rejected_before_body(self):
        raw = HEADER.pack(b'WPX2', 2, 1, self.broker.session, 1, 1, 0, 0, MAX_PAYLOAD+1)
        self.rejected(self.client(1), raw);self.assertFalse(self.broker.members)


if __name__ == '__main__':
    unittest.main()
