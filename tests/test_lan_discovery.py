import json
from pathlib import Path
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
from lobby_directory import *
from lan_discovery import LanBeacon, LanDiscovery, query_message, answer_message, read_answer
from lan_interfaces import parse_interfaces
from lobby_picker import read_choice, snapshot

EXPECTED = 'a' * 64
NONCE = 'b' * 32


class DirectoryTests(unittest.TestCase):
    def setUp(self):
        self.interface = LanInterface('192.168.178.125', 24, 'Heimnetz')
        self.room = LobbyRoom('c' * 32, 'Spieleabend', EXPECTED,
            LobbyEndpoint('lan-direct', '192.168.178.85'), self.interface)

    def test_nonce_source_and_fixed_ports_are_authoritative(self):
        packet = answer_message(query_message(NONCE), self.room)
        result = read_answer(packet, ('192.168.178.85', DISCOVERY_PORT), self.interface, NONCE, 10)
        self.assertEqual(result.endpoint.host, '192.168.178.85')
        self.assertEqual(result.expires_at, 19)
        for source, nonce in [(('192.168.179.85', DISCOVERY_PORT), NONCE),
                              (('192.168.178.85', 12), NONCE),
                              (('192.168.178.85', DISCOVERY_PORT), '0' * 32)]:
            with self.assertRaises(ValueError):
                read_answer(packet, source, self.interface, nonce, 10)

    def test_unknown_fields_ttl_and_capacity_are_rejected(self):
        original = json.loads(answer_message(query_message(NONCE), self.room))
        for key, value in [('host', '8.8.8.8'), ('http_port', 80), ('udp_port', True),
                            ('ttl', 10), ('ttl', True), ('members', 9), ('capacity', 9), ('members', False)]:
            changed = dict(original); changed[key] = value
            with self.subTest(key=key, value=value), self.assertRaises(ValueError):
                read_answer(encode(changed), ('192.168.178.85', DISCOVERY_PORT), self.interface, NONCE, 10)

    def test_duplicate_fields_nonfinite_and_large_records_fail(self):
        for raw in (b'{"x":1,"x":2}', b'{"x":NaN}', b'[]', b' ' * 1025, b'\xff', b'{"x":Infinity}'):
            with self.subTest(raw=raw[:30]), self.assertRaises(ValueError):
                json_record(raw)

    def test_public_names_and_addresses_reject_controls_wan_and_noncanonical(self):
        for value in ('example.com', '8.8.8.8', '127.0.0.1', '192.168.001.2', '10.0.0.255', '0.0.0.0'):
            with self.subTest(value=value), self.assertRaises(ValueError): private_address(value)
        for value in ('', ' X', 'A\nB', 'A\u2028B', '\ud800', 'x' * 65):
            with self.subTest(value=repr(value)), self.assertRaises(ValueError): public_name(value)
        self.assertEqual(public_name('Für alle'), 'Für alle')

    def test_cache_expiry_ambiguity_and_limit(self):
        now = [0]
        cache = RoomCache(lambda: now[0])
        from dataclasses import replace
        cache.add(replace(self.room, expires_at=9))
        cache.add(replace(self.room, endpoint=LobbyEndpoint('lan-direct', '192.168.178.86'), expires_at=9))
        with self.assertRaises(ValueError): cache.resolve(self.room.room_id)
        for index in range(64):
            cache.add(replace(self.room, room_id=f'{index:032x}', expires_at=9))
        self.assertEqual(len(cache.list_rooms()), MAX_ROOMS)
        now[0] = 9
        self.assertEqual(cache.list_rooms(), [])

    def test_beacon_never_replies_outside_selected_network_or_to_bad_query(self):
        beacon = LanBeacon.__new__(LanBeacon)
        from collections import OrderedDict
        beacon.room, beacon.clock, beacon.rates = self.room, lambda: 0, OrderedDict()
        packet = query_message(NONCE)
        self.assertIsNone(beacon._answer(packet, ('192.168.179.2', 1234)))
        self.assertIsNotNone(beacon._answer(packet, ('192.168.178.2', 1234)))
        self.assertIsNone(beacon._answer(packet, ('192.168.178.2', 1234)))
        self.assertIsNone(beacon._answer(b'{"kind":"other"}', ('192.168.178.3', 1234)))

    def test_interfaces_ignore_loopback_public_duplicate_and_malformed(self):
        records = [dict(address=address, prefix_length=24, name='LAN', default_route=False)
                   for address in ('127.0.0.1', '8.8.8.8', '192.168.1.20', '192.168.1.20')]
        records.append({'other': True})
        result = parse_interfaces(json.dumps(records).encode())
        self.assertEqual([x.address for x in result], ['192.168.1.20'])

    def test_picker_data_has_no_addresses_or_native_ids(self):
        value = snapshot([self.interface], [self.room], EXPECTED, 'Anna', 3)
        self.assertNotIn('192.168', json.dumps(value))
        self.assertEqual(value['rooms'][0]['network'], 0)
        self.assertTrue(value['rooms'][0]['compatible'])

    def test_picker_rejects_unlisted_actions_and_networks(self):
        valid = {'format': 's7-lobby-choice-v1', 'action': 'join', 'network': 0,
                 'room': self.room.room_id, 'display_name': 'Anna'}
        self.assertEqual(read_choice(encode(valid), [self.interface]), valid)
        for key, value in [('network', True), ('network', 1), ('action', 'command'), ('display_name', 'x\n')]:
            changed = dict(valid); changed[key] = value
            with self.subTest(key=key), self.assertRaises(ValueError): read_choice(encode(changed), [self.interface])

    def test_discovery_closes_failed_sockets(self):
        from unittest.mock import Mock
        sock = Mock(); sock.bind.side_effect = OSError()
        directory = LanDiscovery([self.interface], socket_factory=Mock(return_value=sock))
        self.assertEqual(directory.list_rooms(timeout=.1), [])
        sock.close.assert_called_once()


if __name__ == '__main__': unittest.main()
