import hashlib
import json
import os
from pathlib import Path
import subprocess
import sys
import tempfile
import tomllib
import unittest
from unittest.mock import Mock, patch

sys.path[:0] = [str(Path(__file__).resolve().parents[1] / 'tools/installer')]
import easytier_runtime as runtime
from internet_room import Room, InternetError

ROOM = Room('a' * 32, 'b' * 64, '10.203.77.0/24')


def row(ip, cost='p2p', name='Guest'):
    return dict(ipv4=ip, cidr=ip + '/24', cost=cost, hostname=name)


class RuntimeTests(unittest.TestCase):
    def test_config_is_private_encrypted_and_has_no_exit_routes_or_broadcast_capture(self):
        text = runtime.configuration(ROOM, ROOM.coordinator, 'id', 'WePlay-id')
        config = tomllib.loads(text)
        self.assertEqual(config['network_identity']['network_secret'], ROOM.secret)
        self.assertFalse(config['dhcp'])
        self.assertEqual(config['routes'], [ROOM.subnet])
        for field in ('enable_exit_node', 'accept_dns', 'enable_udp_broadcast_relay'):
            self.assertFalse(config['flags'][field])
        self.assertTrue(config['flags']['enable_encryption'])
        self.assertFalse(config['flags']['p2p_only'])
        self.assertNotIn('proxy_network', config)
        self.assertNotIn('vpn_portal_config', config)
        direct = Room(ROOM.room_id, ROOM.secret, ROOM.subnet, 'direct', ('udp://1.2.3.4:11010',))
        config = tomllib.loads(runtime.configuration(direct, ROOM.coordinator, 'id', 'WePlay-id'))
        self.assertTrue(config['flags']['p2p_only'])
        self.assertEqual(config['stun_servers'], [])
        self.assertEqual(config['stun_servers_v6'], [])

    def test_environment_cannot_override_config_or_dll_search(self):
        with patch.dict(os.environ, {'ET_CONFIG_SERVER': 'untrusted', 'ET_NETWORK_SECRET': 'no',
                                     'RUST_LOG': 'trace', 'PYTHONPATH': 'untrusted', 'PATH': 'untrusted'}):
            actual = runtime.clean_environment()
        self.assertFalse(any(k.upper().startswith(('ET_', 'RUST_', 'PYTHON')) for k in actual))
        self.assertNotIn('untrusted', actual['PATH'])

    def test_cli_snapshot_accepts_local_direct_and_relay_but_excludes_bootstrap(self):
        rows = [row('10.203.77.2', 'Local', 'WePlay-id'), row(ROOM.coordinator),
                row('10.203.77.3', 'relay(2)'), dict(ipv4='', cost='p2p')]
        address, peers = runtime.peer_snapshot(json.dumps(rows).encode(), ROOM, 'WePlay-id')
        self.assertEqual(address, '10.203.77.2')
        self.assertEqual(peers, {ROOM.coordinator: 'p2p', '10.203.77.3': 'relay'})

    def test_snapshot_rejects_wrong_instance_other_subnets_and_duplicate_addresses(self):
        local = row('10.203.77.2', 'Local', 'WePlay-id')
        for rows in ([], [local, local], [local, row('192.168.1.20')],
                     [row('10.203.77.2', 'Local', 'SomeoneElse')],
                     [local, dict(row(ROOM.coordinator), cidr='10.203.77.1/16')],
                     [local, row(ROOM.coordinator, 'Local')]):
            with self.assertRaises(InternetError): runtime.peer_snapshot(json.dumps(rows).encode(), ROOM, 'WePlay-id')
        for rows in ([local, row(local['ipv4'])], [local, row(ROOM.coordinator), row(ROOM.coordinator)]):
            with self.assertRaises(InternetError) as error:
                runtime.peer_snapshot(json.dumps(rows).encode(), ROOM, 'WePlay-id')
            self.assertEqual(error.exception.code, 'internet_address_conflict')
        with self.assertRaises(InternetError): runtime.peer_snapshot(b'x' * 131073, ROOM, 'WePlay-id')

    def test_runtime_integrity_includes_dlls_and_rejects_extra_packet_library(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); directory = root / 'Runtime/easytier'; directory.mkdir(parents=True)
            files = {}
            for name in ('easytier-core.exe', 'easytier-cli.exe', 'wintun.dll'):
                data = name.encode(); (directory / name).write_bytes(data)
                files[name] = hashlib.sha256(data).hexdigest()
            lock = root / 'lock.json'; lock.write_text(json.dumps(dict(format='weplay-easytier-runtime-v1', files=files)))
            with patch.object(runtime, 'LOCK', lock):
                self.assertEqual(runtime.verify_runtime(root), directory)
                (directory / 'Packet.dll').write_bytes(b'extra')
                with self.assertRaises(InternetError): runtime.verify_runtime(root)
                (directory / 'Packet.dll').unlink()
                (directory / 'wintun.dll').write_bytes(b'modified')
                with self.assertRaises(InternetError): runtime.verify_runtime(root)

    def test_child_start_failure_closes_job_and_removes_own_secret_config(self):
        with tempfile.TemporaryDirectory() as folder:
            root = Path(folder); directory = root / 'Runtime/easytier'; directory.mkdir(parents=True)
            job = Mock(); job.start.side_effect = OSError('fixture failure')
            overlay = runtime.EasyTier(root, ROOM, host=True, job_factory=lambda: job)
            def spawn(arguments, **options):
                self.assertNotIn(ROOM.secret, repr(arguments))
                self.assertNotIn(ROOM.secret, repr(options))
                config = Path(arguments[arguments.index('-c') + 1])
                self.assertEqual(tomllib.loads(config.read_text())['network_identity']['network_secret'], ROOM.secret)
                raise OSError('fixture failure')
            job.start.side_effect = spawn
            with patch.object(runtime, 'verify_runtime', return_value=directory), \
                    patch('ctypes.windll.shell32.IsUserAnAdmin', return_value=1):
                with self.assertRaises(OSError): overlay.start()
            job.close.assert_called_once()
            self.assertEqual(list((root / 'LocalData').iterdir()), [])
            overlay.close()  # Repeated cleanup does not touch another process.

    def test_readiness_cancellation_and_early_process_exit_do_not_continue(self):
        overlay = runtime.EasyTier(Path('.'), ROOM, host=True, cancel_check=Mock(side_effect=RuntimeError('cancelled')))
        with self.assertRaisesRegex(RuntimeError, 'cancelled'): overlay.wait_ready()
        overlay.cancel_check = None
        with self.assertRaises(InternetError) as error: overlay.snapshot()
        self.assertEqual(error.exception.code, 'internet_disconnected')

    def test_close_removes_only_owned_adapter_after_child_exit_and_preserves_other_files(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);data=root/'LocalData';data.mkdir();other=data/'keep.txt';other.write_bytes(b'personal fixture')
            overlay=runtime.EasyTier(root,ROOM,host=True);overlay.private=data/'easytier-fixture';overlay.private.mkdir()
            (overlay.private/'network.toml').write_text('private fixture',encoding='utf-8')
            child=Mock();child.poll.return_value=None;overlay.child=child;overlay.job=Mock()
            def remove(bundle,name,address):
                child.wait.assert_called_once_with(timeout=10)
                self.assertEqual((bundle,name,address),(root,overlay.device_name,overlay.address))
                self.assertNotIn(ROOM.secret,repr((bundle,name,address)))
            with patch('windows_system.remove_adapter',side_effect=remove) as cleanup:
                overlay.close();overlay.close();cleanup.assert_called_once()
            self.assertEqual(other.read_bytes(),b'personal fixture');self.assertIsNone(overlay.private)

    def test_failed_adapter_cleanup_still_reclaims_private_room_config(self):
        with tempfile.TemporaryDirectory() as folder:
            root=Path(folder);data=root/'LocalData';data.mkdir()
            overlay=runtime.EasyTier(root,ROOM,host=True);overlay.private=data/'easytier-fixture';overlay.private.mkdir()
            config=overlay.private/'network.toml';config.write_text('private fixture',encoding='utf-8')
            overlay.child=Mock();overlay.child.poll.return_value=0
            with patch('windows_system.remove_adapter',side_effect=ValueError('helper failed')):
                with self.assertRaisesRegex(InternetError,'cleanup_failed'):overlay.close()
            self.assertFalse(config.exists());self.assertIsNone(overlay.private)


if __name__ == '__main__': unittest.main()
