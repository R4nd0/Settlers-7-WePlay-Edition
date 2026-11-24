import base64
import json
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/offline-ui'))
sys.path.insert(0, str(ROOT / 'tools/installer'))
import launch_local
import launch_lan

class LanLauncherTests(unittest.TestCase):
    def config(self, address='192.168.10.10'):
        return {'public_key': base64.b64encode(b'ECS1' + bytes([32,0,0,0]) + bytes(64)).decode(),
                'prudp_urls': [f'prudp:/address={address};port=9111'], 'content_id': 'a'*64}

    def test_address_policy(self):
        for bad in ('localhost','8.8.8.8','192.168.10.0','192.168.10.255','192.168.010.20','172.32.1.1','::1','192.168.10.10'):
            with self.subTest(bad=bad), self.assertRaises(ValueError):
                launch_local.lan_pair('192.168.10.10', bad)
        self.assertEqual(launch_local.lan_pair('10.1.2.3','172.16.1.1'), ('10.1.2.3','172.16.1.1'))

    def test_both_roles_write_exact_addresses(self):
        for server in ('192.168.10.10','192.168.10.20'):
            with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temporary:
                path=launch_local.write_configuration(Path(temporary), self.config(server), 9111, True,
                    address=server, lan=('192.168.10.10','192.168.10.20'))
                text=path.read_text()
                self.assertIn('address='+server+'\n', text)
                self.assertIn('lan_local=192.168.10.10\nlan_peer=192.168.10.20\n', text)
                self.assertIn('connect_probe=1\n', text)

    def test_unrelated_server_rejected_without_output(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temporary:
            with self.assertRaises(ValueError):
                launch_local.write_configuration(Path(temporary), self.config(), 9111, True,
                    address='192.168.10.30', lan=('192.168.10.10','192.168.10.20'))
            self.assertEqual(list(Path(temporary).iterdir()), [])

    def test_config_cannot_redirect_to_other_endpoint(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temporary:
            with self.assertRaises(ValueError):
                launch_local.write_configuration(Path(temporary), self.config('192.168.10.30'), 9111, True,
                    address='192.168.10.10', lan=('192.168.10.10','192.168.10.20'))
            self.assertEqual(list(Path(temporary).iterdir()), [])

    def test_loopback_default_preserved(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temporary:
            path=launch_local.write_configuration(Path(temporary), self.config('127.0.0.1'), 9111, True)
            self.assertNotIn('lan_', path.read_text())

    def test_lan_requires_explicit_pair(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temporary, self.assertRaises(ValueError):
            launch_local.write_configuration(Path(temporary), self.config(), 9111, True, address='192.168.10.10')

    def test_content_mismatch_and_missing_id_rejected(self):
        for config in ({}, {'content_id':'b'*64}):
            with self.subTest(config=config), self.assertRaises(ValueError):
                launch_lan.check_content(config, 'a'*64)
        launch_lan.check_content({'content_id':'a'*64}, 'a'*64)

    def test_remote_configuration_rejects_response_completed_after_deadline(self):
        now = [0.]
        payload = json.dumps(self.config()).encode('ascii')
        def finish(_limit):
            now[0] = 5.1
            return payload
        response = Mock(status=200, read=Mock(side_effect=finish))
        connection = Mock(getresponse=Mock(return_value=response))
        with patch.object(launch_lan.time, 'monotonic', side_effect=lambda: now[0]), \
                patch.object(launch_lan.http.client, 'HTTPConnection', return_value=connection), \
                self.assertRaises(TimeoutError):
            launch_lan.remote_configuration('192.168.10.10', '192.168.10.20')
        response.close.assert_called_once()
        connection.close.assert_called_once()

if __name__ == '__main__': unittest.main()
