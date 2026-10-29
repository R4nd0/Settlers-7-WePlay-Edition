import base64
from dataclasses import replace
import json
from pathlib import Path
import sys
import unittest
from unittest.mock import patch

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / 'tools/installer')]
import internet_room as rooms
from lobby_directory import LanInterface


def room(**kw):
    return rooms.Room(**dict(dict(room_id='a' * 32, secret='b' * 64,
                                 subnet='10.203.77.0/24'), **kw))


class InvitationTests(unittest.TestCase):
    def test_canonical_round_trip_and_secret_is_not_in_repr(self):
        original = room()
        self.assertEqual(rooms.parse(original.token()), original)
        self.assertNotIn(original.secret, repr(original))

    def test_rejects_malformed_noncanonical_and_oversized_invitations(self):
        for text in ('', 'weplay2.a', room().token() + '=', 'weplay1.' + 'a' * 2049,
                     room().token() + '\n', None, 3):
            with self.subTest(text=type(text).__name__), self.assertRaises(rooms.InternetError):
                rooms.parse(text)

    def test_rejects_duplicate_fields_unknown_versions_and_url_injection(self):
        original = room().token()
        raw = base64.urlsafe_b64decode(original.split('.', 1)[1] + '==')
        payloads = [raw.replace(b'"v":1', b'"v":1,"v":1'),
                    raw.replace(b'"v":1', b'"v":true'),
                    raw.replace(b'"v":1', b'"v":2'),
                    raw.replace(b'public.easytier.cn', b'other.example')]
        for data in payloads:
            token = rooms.PREFIX + base64.urlsafe_b64encode(data).decode().rstrip('=')
            with self.assertRaises(rooms.InternetError): rooms.parse(token)

    def test_only_supported_private_subnets_and_modes(self):
        for changes in (dict(subnet='26.1.2.0/24'), dict(subnet='10.2.0.0/16'),
                        dict(subnet='10.2.3.4/24'), dict(subnet='::/24'),
                        dict(mode='auto'), dict(secret='weak'), dict(room_id=None),
                        dict(mode='direct'), dict(bootstrap=('tcp://example.com:11010',))):
            with self.assertRaises(rooms.InternetError): room(**changes)

    def test_direct_endpoints_are_numeric_and_cannot_enable_other_protocols(self):
        for bad in ('http://1.2.3.4:80', 'udp://127.0.0.1:1', 'udp://0.0.0.0:11010',
                    'udp://224.0.0.1:11010', 'udp://1.2.3.4:0', 'tcp://user@1.2.3.4:1',
                    'udp://1.2.3.4:11010/path', 'udp://example.com:11010', 'udp://1.2.3.4:11010?q=x'):
            with self.assertRaises(rooms.InternetError): rooms.endpoint(bad)
        self.assertEqual(rooms.endpoint('udp://[2001:db8::1]:11010'), 'udp://[2001:db8::1]:11010')
        direct = room(mode='direct', bootstrap=('udp://1.2.3.4:11010',))
        self.assertEqual(rooms.parse(direct.token()), direct)

    def test_generated_room_avoids_physical_and_vpn_networks(self):
        physical = [LanInterface('10.32.0.7', 16)]
        with patch.object(rooms.secrets, 'randbelow', side_effect=[0, 0, 1, 0]):
            actual = rooms.create(physical)
        self.assertEqual(actual.subnet, '10.33.0.0/24')
        self.assertNotEqual(actual.secret, rooms.create([]).secret)
        self.assertNotIn(actual.secret, repr(actual))

    def test_overlay_match_requires_exact_core_ip_prefix_and_device(self):
        r = room(); overlay = LanInterface('10.203.77.2', 24, 'WePlay-test')
        physical = LanInterface('192.168.1.20', 24, 'Ethernet', True)
        self.assertEqual(rooms.select_interface(r, overlay.address, overlay.name, [physical, overlay]), overlay)
        for interfaces in ([physical], [replace(overlay, address='10.203.77.3')],
                           [replace(overlay, prefix_length=16)], [overlay, overlay],
                           [overlay, LanInterface('10.203.1.9', 16, 'Other VPN')]):
            with self.assertRaises(rooms.InternetError):
                rooms.select_interface(r, overlay.address, overlay.name, interfaces)


if __name__ == '__main__': unittest.main()
