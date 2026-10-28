import base64
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/offline-ui')]
import lan_control
from lan_hub import HubLifetime, status_counts
from lobby_directory import CONTROL_FORMAT
import launch_local
import player_settings

EXPECTED = 'a' * 64
LOCAL, COORDINATOR = '192.168.1.20', '192.168.1.21'


def response(peers=None):
    return {'format': CONTROL_FORMAT, 'content_id': EXPECTED, 'revision': 1, 'lease_seconds': 15,
            'peers': peers or [LOCAL, COORDINATOR, '192.168.1.22', '192.168.1.23']}


class MembershipTests(unittest.TestCase):
    def setUp(self):
        self.temporary = tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT'))
        self.addCleanup(self.temporary.cleanup)
        self.root = Path(self.temporary.name)

    def test_four_members_allow_only_exact_other_addresses(self):
        peers = lan_control.validate_peers(response(), LOCAL, COORDINATOR, EXPECTED)
        path = lan_control.write_peers(self.root, LOCAL, peers, clock=lambda: 123000)
        self.assertEqual(path.read_text(), 'format=s7-lan-peers-v1\nlocal=192.168.1.20\n'
            'expires_tick_ms=138000\npeer_1=192.168.1.21\npeer_2=192.168.1.22\npeer_3=192.168.1.23\n')

    def test_bad_membership_never_replaces_existing_file(self):
        original = lan_control.write_peers(self.root, LOCAL, (COORDINATOR,), clock=lambda: 0).read_bytes()
        for peers in ([LOCAL], [LOCAL, LOCAL, COORDINATOR], [LOCAL, COORDINATOR, '8.8.8.8'],
                      [COORDINATOR], [LOCAL, COORDINATOR] + [f'10.0.0.{i}' for i in range(1, 8)]):
            with self.subTest(peers=peers), self.assertRaises(ValueError):
                lan_control.validate_peers(response(peers), LOCAL, COORDINATOR, EXPECTED)
        self.assertEqual((self.root / 'LocalData/lan-peers.ini').read_bytes(), original)

    def test_failure_to_renew_does_not_extend_native_deadline(self):
        transport = Mock(side_effect=[response(), OSError('unavailable'), None])
        membership = lan_control.Membership(self.root, LOCAL, COORDINATOR, EXPECTED, transport=transport, clock=lambda: 10)
        membership.renew()
        original = membership.path.read_bytes()
        with self.assertRaises(OSError): membership.renew()
        self.assertEqual(membership.path.read_bytes(), original)
        membership.close()
        self.assertFalse((self.root / 'LocalData/lan-peers.ini').exists())
        self.assertEqual(transport.call_args.args[2], 'leave')

    def test_coordinator_admits_its_own_game(self):
        transport = Mock(return_value=response([LOCAL]))
        membership = lan_control.Membership(self.root, LOCAL, LOCAL, EXPECTED, transport=transport, clock=lambda: 1)
        membership.start(); membership.close()
        self.assertEqual(transport.call_args_list[0].args[2], 'admit')
        self.assertEqual(transport.call_args_list[-1].args[2], 'leave')

    def test_hub_stays_for_remote_players_and_has_bounded_idle_lifetime(self):
        now = [0]
        life = HubLifetime(clock=lambda: now[0])
        now[0] = 50; self.assertTrue(life.keep(2))
        now[0] = 100; self.assertTrue(life.keep(1))
        now[0] = 159; self.assertTrue(life.keep(0))
        now[0] = 160; self.assertFalse(life.keep(0))
        now[0] = 86400; self.assertFalse(life.keep(1))

    def test_player_name_is_local_and_does_not_change_identity(self):
        data = self.root / 'LocalData'; data.mkdir()
        identity = data / 'local-profile-id.txt'; identity.write_text('unchanged-fixture')
        self.assertEqual(player_settings.load(self.root), 'LocalPlayer')
        player_settings.save(self.root, 'Fränzi')
        self.assertEqual(player_settings.load(self.root), 'Fränzi')
        self.assertEqual(identity.read_text(), 'unchanged-fixture')
        self.assertFalse((self.root / 'compatibility.json').exists())

    def test_player_names_respect_native_utf8_byte_limit_and_line_safety(self):
        self.assertEqual(player_settings.player_name('ä' * 12), 'ä' * 12)
        self.assertEqual(player_settings.player_name('A=B'), 'A=B')
        for name in ('ä' * 13, '', ' A', 'x\ny', 'x\0y', 'x\u2028y', 'x\u0085y'):
            with self.subTest(name=repr(name)), self.assertRaises(ValueError): player_settings.player_name(name)

    def test_runtime_group_and_name_are_written_without_developer_flag(self):
        data = self.root / 'LocalData'; data.mkdir()
        public = base64.b64encode(b'ECS1\x20\0\0\0' + b'0' * 64).decode()
        configuration = {'public_key': public, 'prudp_urls': [f'prudp:/address={COORDINATOR};port=9111']}
        path = launch_local.write_configuration(data, configuration, 9111, True,
            address=COORDINATOR, lan_group=LOCAL, display_name='Fränzi')
        result = path.read_text(encoding='utf-8')
        self.assertIn('display_name=Fränzi\n', result)
        self.assertIn('lan_group=1\nlan_local=' + LOCAL, result)
        self.assertNotIn('lan_peer', result)
        self.assertNotIn('developer', result)
        with self.assertRaises(ValueError):
            launch_local.write_configuration(data, configuration, 9111, True,
                address=COORDINATOR, lan_group=LOCAL, lan=(LOCAL, COORDINATOR))

    def test_http_is_direct_numeric_source_bound_and_bounded(self):
        connection = Mock()
        answer = Mock(status=200); answer.read.return_value = json.dumps(response()).encode()
        connection.getresponse.return_value = answer
        with patch.object(lan_control.http.client, 'HTTPConnection', return_value=connection) as factory:
            lan_control.request(COORDINATOR, LOCAL, 'admit', expected=EXPECTED)
        self.assertEqual(factory.call_args.kwargs['source_address'], (LOCAL, 0))
        self.assertEqual(connection.request.call_args.args, ('POST', '/public/lobby/v1/admit'))
        answer.read.assert_called_once_with(4097)
        connection.close.assert_called_once()


if __name__ == '__main__': unittest.main()
