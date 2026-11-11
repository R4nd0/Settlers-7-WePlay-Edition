"""Owned synthetic registrations through the real authenticated transport."""
from dataclasses import replace
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / "tools/gold"))
import gathering_codec as codec
import login_server as gold
from local_gatherings import LocalGatherings
import gold_account_tests as account
import gold_login_tests as fixture


class RegistrationTests(unittest.TestCase):
    connect = fixture.GoldTests.connect
    login = fixture.GoldTests.login
    register = account.AccountTests.register
    send = account.AccountTests.send
    arguments = account.AccountTests.arguments

    def setUp(self):
        self.adapter = gold.GoldLoginAdapter("local-fixture", "a" * 64, 55123,
            account_mode=True, session_mode=True)
        self.auth = "127.0.0.1", 51000
        self.backend = "127.0.0.1", 51001
        self.now = 10.
        self.value = codec.SessionGathering(codec.Gathering((0,) * 7, (0, 4), None, b""),
            ("map", "hash", "template", "host"), (0,) * 6, (False,) * 4)
        self.data = codec.encode(self.value)

    def test_registration_retry_owned_delete_and_recreate(self):
        server = self.register()
        created = self.send(server, 3, 21, 1, self.data)
        identifier, = struct.unpack("<I", self.arguments(created, 21, 1))
        records = self.adapter.gatherings.records
        self.assertEqual(len(records), 1)
        record = records[identifier]
        self.assertIs(record.owner, self.adapter.connections[True, self.backend])
        self.assertEqual(record.principal, self.adapter.principal)
        self.assertEqual(record.value.base.words[0], identifier)
        self.assertEqual(self.send(server, 3, 21, 1, self.data), created)
        self.assertEqual(self.adapter.events["gathering_registered"], 1)
        denied = self.send(server, 4, 21, 1, self.data)
        self.assertEqual(gold.unwrap(denied[1][13:-1])[4:6], b"\x15\0")
        self.assertEqual(len(records), 1)
        self.assertEqual(self.arguments(self.send(server, 5, 21, 2,
            struct.pack("<I", identifier)), 21, 2), b"\x01")
        self.assertFalse(records)
        recreated = self.arguments(self.send(server, 6, 21, 1, self.data), 21, 1)
        self.assertGreater(int.from_bytes(recreated, "little"), identifier)

    def test_unregistered_or_different_connection_cannot_claim_ownership(self):
        server = self.register()
        identifier = int.from_bytes(self.arguments(self.send(server, 3, 21, 1, self.data), 21, 1), "little")
        owner = self.adapter.connections[True, self.backend]
        self.backend = "127.0.0.1", 51002
        other_server = self.connect(True)
        denied = self.send(other_server, 2, 21, 2, struct.pack("<I", identifier))
        self.assertEqual(gold.unwrap(denied[1][13:-1])[4:6], b"\x15\0")
        other = self.adapter.connections[True, self.backend]
        other.registered = True  # Same authenticated principal still isn't owner.
        reply = self.send(other_server, 3, 21, 2, struct.pack("<I", identifier))
        self.assertEqual(self.arguments(reply, 21, 2), b"\0")
        self.assertIs(self.adapter.gatherings.records[identifier].owner, owner)

    def test_disconnect_and_expiry_remove_registrations(self):
        for through_packet in (False, True):
            self.setUp()
            server = self.register()
            self.send(server, 3, 21, 1, self.data)
            if through_packet:
                self.adapter.handle(True, self.backend,
                    account.incoming(3, 48, 17, server, 4), self.now)
            else:
                self.adapter.retransmissions(self.now + 121)
            self.assertFalse(self.adapter.gatherings.records)
        self.setUp()
        server = self.register()
        self.send(server, 3, 21, 1, self.data)
        self.adapter.handle(False, self.auth, account.incoming(0), self.now + 121)
        self.assertFalse(self.adapter.gatherings.records)

    def test_malformed_and_exhausted_requests_do_not_mutate_registry(self):
        server = self.register()
        for sequence, data in enumerate((self.data[:-1], self.data + b"x",
                codec.encode(replace(self.value, matchmaking_word=1)),
                codec.encode(replace(self.value, base=replace(self.value.base, words=(1,) * 7)))), 3):
            self.send(server, sequence, 21, 1, data)
            self.assertFalse(self.adapter.gatherings.records)
        state = self.adapter.connections[True, self.backend]
        state.next_out = 0x10000
        self.send(server, 7, 21, 1, self.data)
        self.assertFalse(self.adapter.gatherings.records)

    def test_registry_capacity_ids_and_owner_lifetime(self):
        registry = LocalGatherings()
        owners = [object() for _ in range(9)]
        for owner in owners[:8]:
            registry.create(owner, 1, self.data, gold.Reader)
        with self.assertRaises(ValueError):
            registry.create(owners[8], 1, self.data, gold.Reader)
        registry.remove_owner(owners[0])
        registry.next_identifier = 0xffffffff
        self.assertEqual(registry.create(owners[8], 1, self.data, gold.Reader), 0xffffffff)
        registry.remove_owner(owners[8])
        with self.assertRaises(ValueError):
            registry.create(owners[8], 1, self.data, gold.Reader)

    def test_only_owned_participating_registration_can_claim_host(self):
        server = self.register()
        self.data = codec.encode(replace(self.value, base=replace(self.value.base,
            words=(0, 0, 0x12345678, 0, 0, 0, 0))))
        identifier = int.from_bytes(self.arguments(self.send(server, 3, 21, 1, self.data), 21, 1), "little")
        self.assertEqual(self.adapter.gatherings.get_owned(
            self.adapter.connections[True, self.backend], identifier).base.words[2], 0)
        control = struct.pack("<I", identifier)
        participation = control + gold.text("")
        denied = self.send(server, 4, 21, 40, control)
        self.assertEqual(gold.unwrap(denied[1][13:-1])[4:6], b"\x15\0")
        self.assertEqual(self.arguments(self.send(server, 5, 21, 11, participation), 21, 11), b"\1")
        self.assertEqual(self.arguments(self.send(server, 6, 21, 40, control), 21, 40), b"")
        self.assertTrue(self.adapter.gatherings.records[identifier].host_claimed)
        record = self.adapter.gatherings.records[identifier]
        self.assertEqual(record.value.base.words[2], self.adapter.principal)
        wire = codec.decode(codec.encode(record.value), gold.Reader)
        self.assertEqual(wire.base.words[2], self.adapter.principal)
        self.send(server, 7, 21, 12, participation + b"trailing")
        self.assertIs(self.adapter.gatherings.records[identifier], record)
        self.assertEqual(self.arguments(self.send(server, 8, 21, 12, participation), 21, 12), b"\1")
        self.assertFalse(self.adapter.gatherings.records[identifier].host_claimed)
        self.assertEqual(self.adapter.gatherings.records[identifier].value.base.words[2], 0)
        self.assertFalse(self.adapter.gatherings.participate(object(), identifier, True))
        with self.assertRaises(ValueError):
            self.adapter.gatherings.claim_host(object(), identifier)

    def test_absent_predefined_groups_do_not_publish_session_records(self):
        server = self.register()
        self.send(server, 3, 21, 1, self.data)
        snapshot = dict(self.adapter.gatherings.records)
        query = gold.text("S7Multiplayer") + struct.pack("<II", 0, 20)
        self.assertEqual(self.arguments(self.send(server, 4, 21, 17, query), 21, 17), b"\0" * 4)
        for sequence, args in enumerate((query + b"x", query[:-1],
                gold.text("S7Session") + struct.pack("<II", 0, 20),
                gold.text("S7Multiplayer") + struct.pack("<II", 0, 65)), 5):
            denied = self.send(server, sequence, 21, 17, args)
            self.assertEqual(gold.unwrap(denied[1][13:-1])[4:6], b"\x15\0")
        self.assertEqual(self.adapter.gatherings.records, snapshot)

    def test_title_update_requires_current_owned_host_and_preserves_authority(self):
        server = self.register()
        identifier = int.from_bytes(self.arguments(self.send(server, 3, 21, 1, self.data), 21, 1), "little")
        owner = self.adapter.connections[True, self.backend]
        control = struct.pack("<I", identifier)
        value = self.adapter.gatherings.get_owned(owner, identifier)
        denied = self.send(server, 4, 21, 4, codec.encode(value))
        self.assertEqual(gold.unwrap(denied[1][13:-1])[4:6], b"\x15\0")
        self.send(server, 5, 21, 11, control + gold.text(""))
        self.send(server, 6, 21, 40, control)
        value = self.adapter.gatherings.get_owned(owner, identifier)
        updated = replace(value, words=(4, 2, 1, 0, 1, 0), flags=(True, False, False, False))
        self.assertEqual(self.arguments(self.send(server, 7, 21, 4,
            codec.encode(updated)), 21, 4), b"\1")
        snapshot = self.adapter.gatherings.records[identifier]
        self.assertEqual(snapshot.value, updated)
        for sequence, data in enumerate((codec.encode(updated)[:-1], codec.encode(updated) + b"x",
                codec.encode(replace(updated, base=replace(updated.base,
                    words=(identifier, 0, 1, 0, 0, 0, 0))))), 8):
            self.send(server, sequence, 21, 4, data)
            self.assertIs(self.adapter.gatherings.records[identifier], snapshot)
        with self.assertRaises(ValueError):
            self.adapter.gatherings.update(object(), codec.encode(updated), gold.Reader)

    def test_owned_record_read_roundtrips_authoritative_id_without_publication(self):
        server = self.register()
        identifier = int.from_bytes(self.arguments(self.send(server, 3, 21, 1, self.data), 21, 1), "little")
        reply = self.arguments(self.send(server, 4, 21, 21, struct.pack("<I", identifier)), 21, 21)
        self.assertEqual(reply[0], 1)
        decoded = codec.decode(reply[1:], gold.Reader)
        self.assertEqual(decoded.base.words[0], identifier)
        self.assertEqual(decoded.strings, self.value.strings)
        with self.assertRaises(ValueError):
            self.adapter.gatherings.get_owned(object(), identifier)
        denied = self.send(server, 5, 21, 21, struct.pack("<I", identifier + 1))
        self.assertEqual(gold.unwrap(denied[1][13:-1])[4:6], b"\x15\0")


if __name__ == "__main__":
    unittest.main()
