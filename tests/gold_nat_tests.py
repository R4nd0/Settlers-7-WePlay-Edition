"""Real loopback RPC/ACK tests; native ping and gameplay remain separate checks."""
import struct
import time
import unittest

import gold_peer_tests as fixture

gold = fixture.gold


class NatTests(unittest.TestCase):
    peers = fixture.PeerTests.peers
    setUp = fixture.PeerTests.setUp
    authenticate = fixture.PeerTests.authenticate
    host = fixture.PeerTests.host

    def prepare(self):
        self.authenticate()
        identifier = self.host(self.clients[0])
        target = self.service.gatherings.records[identifier].owner
        source = next(state for (backend, _), state in self.service.adapters[1].connections.items() if backend)
        return identifier, target, source

    def request(self, urls):
        return self.clients[1].rpc(True, 3, 1,
            struct.pack("<I", len(urls)) + b"".join(gold.text(value) for value in urls))

    def receive(self):
        wire = self.clients[0].sockets[True].recv(4096)
        protocol, call, method, args = gold.rpc_request(wire[13:-1])
        self.assertEqual((protocol, method), (3, 2))
        return wire, call, args

    def test_native_probe_notification_uses_authenticated_source_and_real_ack(self):
        _, target, source = self.prepare()
        prefix, tail = target.public_url.split(":/", 1)
        reordered = prefix + ":/" + ";".join(reversed(tail.split(";"))) + ";"
        self.assertEqual(self.request((reordered,))[4:6], b"\3\1")
        wire, call, args = self.receive()
        self.assertEqual(args, gold.text(source.public_url))
        output_sequence = int.from_bytes(wire[8:10], "little")
        self.assertIn(output_sequence, target.pending)
        client = self.clients[0]
        client.exchange(True, fixture.fixture.incoming(2, 8, 17,
            client.servers[True], output_sequence), 0)
        self.assertNotIn(output_sequence, target.pending)
        sequence = client.sequences[True]
        response = fixture.fixture.incoming(2, 48, 17, client.servers[True], sequence,
            gold.rpc_response(3, call, 2, b""))
        client.sequences[True] += 1
        ack, = client.exchange(True, response, 1)
        self.assertEqual(ack[2] & 7, 2)
        self.assertFalse(self.service.probes.jobs)
        self.assertEqual(self.service.adapters[0].events["native_probe_notification_answered"], 1)
        # A repeated transport packet gets only its original ACK.
        self.assertEqual(client.exchange(True, response, 1), [ack])
        self.assertEqual(self.service.adapters[0].events["native_probe_notification_answered"], 1)

    def test_probe_targets_must_be_live_published_foreign_stations(self):
        identifier, target, source = self.prepare()
        for urls in ((), (source.public_url,), ("prudp:/address=192.0.2.1;port=1",),
                (target.public_url, target.public_url), (target.public_url + ";PID=1",)):
            with self.subTest(count=len(urls)):
                self.assertEqual(self.request(urls)[4:6], b"\3\0")
                self.assertFalse(self.service.probes.jobs)
        self.service.gatherings.remove_owner(target)
        self.assertEqual(self.request((target.public_url,))[4:6], b"\3\0")
        self.assertNotIn(identifier, self.service.gatherings.records)

    def test_native_probe_request_bounds_and_inactive_requesters(self):
        _, target, source = self.prepare()
        relay = self.service.probes
        arguments = struct.pack("<I", 1) + gold.text(target.public_url)
        for value in (b"", struct.pack("<I", 9), arguments[:-1], arguments + b"tail",
                struct.pack("<IH", 1, 258) + b"x" * 257 + b"\0"):
            with self.assertRaises(ValueError):
                relay.request(self.service.adapters[1], source, value, time.monotonic())
            self.assertFalse(relay.jobs)
        for attribute, value in (("connected", False), ("registered", False), ("closed", True), ("expires", 0)):
            old = getattr(source, attribute)
            setattr(source, attribute, value)
            with self.assertRaises(ValueError):
                relay.request(self.service.adapters[1], source, arguments, time.monotonic())
            setattr(source, attribute, old)
            self.assertFalse(relay.jobs)

    def test_probe_queue_backpressure_and_revocation(self):
        _, target, source = self.prepare()
        now = time.monotonic()
        target.pending = {index: (b"fixture", now + 100, 1) for index in range(20, 29)}
        args = struct.pack("<I", 1) + gold.text(target.public_url)
        relay = self.service.probes
        for _ in range(40):
            relay.request(self.service.adapters[1], source, args, now)
        self.assertEqual(len(relay.jobs), 1)
        sequence = target.next_out
        relay.pump(now)
        self.assertEqual(target.next_out, sequence)
        self.service.gatherings.remove_owner(target)
        relay.pump(now)
        self.assertFalse(relay.jobs)
        self.assertEqual(target.next_out, sequence)

    def test_probe_response_correlation_cannot_be_claimed_by_requester(self):
        _, target, source = self.prepare()
        self.request((target.public_url,))
        _, call, _ = self.receive()
        relay = self.service.probes
        for adapter, state, response in (
                (self.service.adapters[1], source, gold.rpc_response(3, call, 2, b"")),
                (self.service.adapters[0], target, gold.rpc_response(3, call + 1, 2, b"")),
                (self.service.adapters[0], target, gold.rpc_response(3, call, 1, b"")),
                (self.service.adapters[0], target, gold.rpc_response(3, call, 2, b"extra"))):
            with self.assertRaises(ValueError):
                relay.response(adapter, state, gold.unwrap(response))
            self.assertEqual(len(relay.jobs), 1)
        relay.response(self.service.adapters[0], target, gold.unwrap(gold.rpc_rejection(3, call)))
        self.assertFalse(relay.jobs)

    def test_expiry_cancels_only_own_notification(self):
        _, target, source = self.prepare()
        relay = self.service.probes
        now = time.monotonic()
        relay.request(self.service.adapters[1], source,
            struct.pack("<I", 1) + gold.text(target.public_url), now)
        relay.pump(now)
        sequence = next(iter(relay.jobs.values())).sequence
        self.assertIn(sequence, target.pending)
        target.pending[sequence + 1] = (b"unrelated", now + 100, 1)
        source.closed = True
        relay.pump(now)
        self.assertNotIn(sequence, target.pending)
        self.assertIn(sequence + 1, target.pending)
        self.assertFalse(relay.jobs)


if __name__ == "__main__":
    unittest.main()
