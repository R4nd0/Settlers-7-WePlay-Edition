import json
from itertools import chain, repeat
from pathlib import Path
import socket
import sys
import threading
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
from lobby_directory import *
from lan_discovery import LanBeacon, LanDiscovery, query_message, answer_message, read_answer
from lan_interfaces import linux_interfaces, parse_interfaces
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

    def test_custom_port_matches_beacon_bind_queries_and_reply_source(self):
        port = 19112
        beacon_socket = Mock()
        beacon = LanBeacon(self.room, port=port, socket_factory=Mock(return_value=beacon_socket),
                           route_socket=Mock())
        self.addCleanup(beacon.close)
        beacon_socket.bind.assert_called_once_with(('0.0.0.0', port))
        packet = answer_message(query_message(NONCE), self.room)
        client_socket = Mock()
        client_socket.recvfrom.side_effect = [(packet, (self.room.endpoint.host, port)), OSError()]
        directory = LanDiscovery([self.interface], port=port, socket_factory=Mock(return_value=client_socket),
                                 route_socket=Mock(), clock=Mock(side_effect=chain(repeat(0, 6), repeat(.2))))
        with patch('lan_discovery.secrets.token_hex', return_value=NONCE), \
                patch('select.select', return_value=([client_socket], [], [])):
            self.assertEqual(directory.list_rooms(timeout=.1)[0].room_id, self.room.room_id)
        self.assertEqual({call.args[1][1] for call in client_socket.sendto.call_args_list}, {port})
        with self.assertRaises(ValueError):
            read_answer(packet, (self.room.endpoint.host, port), self.interface, NONCE, 0)

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

    def test_linux_ignores_many_unusable_addresses_before_selecting_private_adapter(self):
        entries = [{'ifname': f'veth{index}', 'flags': ['UP'],
                    'addr_info': [{'family': 'inet', 'local': f'169.254.1.{index}', 'prefixlen': 16}]}
                   for index in range(1, 71)]
        entries.append({'ifname': 'eth0', 'flags': ['UP'],
                        'addr_info': [{'family': 'inet', 'local': '192.168.1.7', 'prefixlen': 24}]})
        result = [Mock(returncode=0, stdout=json.dumps(value).encode()) for value in ([{'dev': 'eth0'}], entries)]
        with patch('lan_interfaces.subprocess.run', side_effect=result):
            self.assertEqual(linux_interfaces(), [LanInterface('192.168.1.7', 24, 'eth0', True)])

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
        sock = Mock(); sock.bind.side_effect = OSError()
        directory = LanDiscovery([self.interface], socket_factory=Mock(return_value=sock))
        self.assertEqual(directory.list_rooms(timeout=.1), [])
        sock.close.assert_called_once()

    def test_failed_broadcast_can_still_find_own_coordinator(self):
        packet = answer_message(query_message(NONCE), self.room)
        client = Mock()
        def send(_packet, target):
            if target[0] != self.interface.address:
                raise OSError('broadcast unavailable')
        client.sendto.side_effect = send
        client.recvfrom.side_effect = [(packet, (self.room.endpoint.host, DISCOVERY_PORT)), socket.timeout()]
        directory = LanDiscovery([self.interface], socket_factory=Mock(return_value=client),
                                 clock=Mock(side_effect=chain(repeat(0, 6), repeat(.2))), route_socket=Mock())
        with patch('lan_discovery.secrets.token_hex', return_value=NONCE), \
                patch('select.select', return_value=([client], [], [])):
            self.assertEqual([room.room_id for room in directory.list_rooms(timeout=.1)], [self.room.room_id])
        self.assertEqual(client.sendto.call_args.args[1], (self.interface.address, DISCOVERY_PORT))

    def test_discovery_retries_after_first_query_is_lost(self):
        packet = answer_message(query_message(NONCE), self.room)
        client = Mock()
        now = [0.]
        def ready(read, _write, _except, delay):
            if client.sendto.call_count < 4:
                now[0] += delay
                return [], [], []
            return read, [], []
        def receive(_size):
            room_reply = client.sendto.call_count >= 4
            now[0] += .03
            if room_reply:
                return packet, (self.room.endpoint.host, DISCOVERY_PORT)
            raise socket.timeout()
        client.recvfrom.side_effect = receive
        directory = LanDiscovery([self.interface], socket_factory=Mock(return_value=client),
                                 clock=lambda: now[0], route_socket=Mock())
        with patch('lan_discovery.secrets.token_hex', return_value=NONCE), patch('select.select', side_effect=ready):
            self.assertEqual([room.room_id for room in directory.list_rooms(timeout=.4)], [self.room.room_id])
        self.assertEqual(client.sendto.call_count, 4)

    def test_discovery_retries_when_first_send_temporarily_fails(self):
        packet = answer_message(query_message(NONCE), self.room)
        client = Mock()
        client.sendto.side_effect = [OSError('interface still coming up'), OSError('interface still coming up'),
                                     len(packet), len(packet)]
        client.recvfrom.return_value = packet, (self.room.endpoint.host, DISCOVERY_PORT)
        now = [0.]
        def ready(read, _write, _except, wait):
            if client.sendto.call_count == 4 and not client.recvfrom.called:
                return read, [], []
            now[0] += wait
            return [], [], []
        directory = LanDiscovery([self.interface], socket_factory=Mock(return_value=client),
                                 clock=lambda: now[0], route_socket=Mock())
        with patch('lan_discovery.secrets.token_hex', return_value=NONCE), patch('select.select', side_effect=ready):
            self.assertEqual([room.room_id for room in directory.list_rooms(timeout=.4)], [self.room.room_id])
        self.assertEqual(client.sendto.call_count, 4)
        client.close.assert_called_once()

    def test_idle_adapters_do_not_use_up_deadline_before_ready_adapter(self):
        packet = answer_message(query_message(NONCE), self.room)
        sockets = [Mock() for _ in range(6)]
        now = [0.]
        def idle(_size):
            now[0] += .03
            raise socket.timeout()
        for sock in sockets[:-1]:
            sock.recvfrom.side_effect = idle
        def receive(_size):
            now[0] += .02
            return packet, (self.room.endpoint.host, DISCOVERY_PORT)
        sockets[-1].recvfrom.side_effect = receive
        directory = LanDiscovery([self.interface] * len(sockets), socket_factory=Mock(side_effect=sockets),
                                 clock=lambda: now[0], route_socket=Mock())
        with patch('lan_discovery.secrets.token_hex', return_value=NONCE), \
                patch('select.select', return_value=([sockets[-1]], [], [])):
            self.assertEqual([room.room_id for room in directory.list_rooms(timeout=.1)], [self.room.room_id])
        for sock in sockets[:-1]:
            sock.recvfrom.assert_not_called()

    def test_loopback_scan_receives_reply_to_retransmission(self):
        # Only this fixture permits loopback as a LAN address. Its beacon
        # listens on an ephemeral loopback port and ignores the first query.
        with patch('lobby_directory.private_address', side_effect=lambda address: address), \
                socket.socket(socket.AF_INET, socket.SOCK_DGRAM) as beacon:
            beacon.bind(('127.0.0.1', 0))
            beacon.settimeout(2)
            interface = LanInterface('127.0.0.1', 24)
            room = LobbyRoom(self.room.room_id, self.room.name, EXPECTED,
                             LobbyEndpoint('lan-direct', interface.address), interface)
            received = []
            def serve():
                try:
                    received.append(beacon.recvfrom(MAX_DATAGRAM))
                    query, address = beacon.recvfrom(MAX_DATAGRAM)
                    received.append((query, address))
                    beacon.sendto(answer_message(query, room), address)
                except OSError:
                    pass

            worker = threading.Thread(target=serve)
            worker.start()
            try:
                directory = LanDiscovery([interface], port=beacon.getsockname()[1], route_socket=Mock())
                result = directory.list_rooms(timeout=.6)
                self.assertEqual([item.room_id for item in result], [room.room_id])
                self.assertEqual(len(received), 2)
                self.assertEqual(received[0], received[1])
            finally:
                worker.join(timeout=3)
            self.assertFalse(worker.is_alive())


if __name__ == '__main__': unittest.main()
