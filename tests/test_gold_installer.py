"""Gold package/publication boundaries and authored x86 bridge/CPU fixtures."""
import hashlib
import json
from pathlib import Path
import struct
import sys
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/gold'), str(ROOT / 'work/python-libs')]
import installer
import bootstrap
from entry_bridge import bridge


class GoldInstallerTests(unittest.TestCase):
    def setUp(self):
        (ROOT / 'work/gold-installer-tests').mkdir(parents=True, exist_ok=True)
        self.temp = tempfile.TemporaryDirectory(dir=ROOT / 'work/gold-installer-tests')
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.app, self.source = self.root / 'App', self.root / 'Original'
        self.payloads = {'Payload/weplay_gold_startup.dll': b'authored adapter',
                         'Payload/d3d9.dll': b'authored renderer', 'Definitions/Definitions.bin': b'authored definitions',
                         'Tools/dummy.py': b'authored tool', 'Runtime/python/python.exe': b'authored interpreter',
                         'Licenses/MIT.txt': b'authored license'}
        for name, value in self.payloads.items():
            path = self.app / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(value)
        self.exe, self.interface = b'authored prepared executable', b'authored cleaned interface'
        for obj, key, value in (
            (installer, 'ADAPTER_SHA256', self.payloads['Payload/weplay_gold_startup.dll']),
            (installer, 'EXE_SHA256', self.exe), (installer.gold, 'MENU_SHA256', self.interface),
            (installer.gold, 'DXVK_SHA256', self.payloads['Payload/d3d9.dll']),
            (installer.gold, 'DEFINITIONS_SHA256', self.payloads['Definitions/Definitions.bin'])):
            guard = patch.object(obj, key, hashlib.sha256(value).hexdigest())
            guard.start(); self.addCleanup(guard.stop)
        self.manifest = {'format': installer.FORMAT, 'menu_sha256': installer.gold.MENU_SHA256,
                         'multiplayer': False, 'files': installer.inventory(self.app,
                         ('Tools', 'Runtime', 'Payload', 'Definitions', 'Licenses'))}
        self.save_manifest()
        exe = self.source / installer.gold.EXE; exe.parent.mkdir(parents=True); exe.write_bytes(b'original')
        guard = patch.object(bootstrap, 'ensure_closed')
        guard.start(); self.addCleanup(guard.stop)
        guard = patch.object(installer.gold, 'inspect', return_value={})
        guard.start(); self.addCleanup(guard.stop)

    def save_manifest(self):
        (self.app / 'package.json').write_text(json.dumps(self.manifest))

    def prepared(self, source, target, **kwargs):
        for name, value in ((installer.gold.EXE, self.exe), (installer.gold.INTERFACE, self.interface),
                            (installer.gold.EXE.with_name('d3d9.dll'), self.payloads['Payload/d3d9.dll']),
                            (Path('Backup/Interface.original.bba'), b'original interface')):
            path = target / name; path.parent.mkdir(parents=True, exist_ok=True); path.write_bytes(value)
        return {'source_files': {'original': 'verified'}}

    def test_publishes_only_complete_verified_gold_game_and_keeps_original(self):
        with patch.object(installer.gold, 'prepare', side_effect=self.prepared):
            result = installer.install(self.app, self.source, emit=lambda text: None)
        self.assertEqual(installer.verify_game(self.app), result)
        self.assertEqual((self.source / installer.gold.EXE).read_bytes(), b'original')
        self.assertFalse(list(self.app.glob('GoldPreparation-*')))
        self.assertTrue(result['quiet_interface']); self.assertTrue(result['all_campaign_maps_selectable'])
        self.assertFalse(result['multiplayer'])

    def test_rejects_unknown_adapter_even_when_manifest_matches_its_bytes(self):
        target = self.app / 'Payload/weplay_gold_startup.dll'; target.write_bytes(b'foreign')
        self.manifest['files']['Payload/weplay_gold_startup.dll'] = installer.gold.digest(target)
        self.save_manifest()
        with self.assertRaisesRegex(ValueError, 'ungeprüften Laufzeitstand'):
            installer.verify_package(self.app)

    def test_existing_game_is_not_overwritten(self):
        target = self.app / 'Game'; target.mkdir(); (target / 'keep').write_bytes(b'keep')
        with patch.object(installer.gold, 'prepare') as prepare:
            with self.assertRaisesRegex(ValueError, 'existiert bereits'):
                installer.install(self.app, self.source, emit=lambda text: None)
            prepare.assert_not_called()
        self.assertEqual((target / 'keep').read_bytes(), b'keep')

    def test_failed_preparation_is_not_published(self):
        with patch.object(installer.gold, 'prepare', side_effect=OSError('copy failed')):
            with self.assertRaisesRegex(OSError, 'copy failed'):
                installer.install(self.app, self.source, emit=lambda text: None)
        self.assertFalse((self.app / 'Game').exists())

    def test_manifest_traversal_and_outside_roots_rejected_before_read(self):
        for name in ('../outside', '/absolute', 'C:/outside', 'Runtime/../../outside', 'LocalData/private.json'):
            with patch.object(installer.gold, 'digest') as digest:
                with self.assertRaises(ValueError):
                    installer.verified_files(self.app, {name: '0' * 64}, ('Tools',))
                digest.assert_not_called()

    def test_game_tampering_prevents_start(self):
        with patch.object(installer.gold, 'prepare', side_effect=self.prepared):
            installer.install(self.app, self.source, emit=lambda text: None)
        (self.app / 'Game' / installer.gold.INTERFACE).write_bytes(b'modified')
        with self.assertRaisesRegex(ValueError, 'verändert'):
            installer.verify_game(self.app)


class GoldBootstrapFixtures(unittest.TestCase):
    def test_preparation_only_blocks_games_in_its_own_app_but_start_blocks_all(self):
        from types import SimpleNamespace
        api = bootstrap.Windows.__new__(bootstrap.Windows)
        own = ROOT / 'work/gold-installer-tests/Owned'
        rows = [SimpleNamespace(exe='Settlers7R.exe', pid=12)]
        with patch.object(api, 'processes', return_value=iter(rows)), \
                patch.object(api, 'process_path', return_value=ROOT / 'work/OtherGame/Settlers7R.exe'):
            api.closed(own)
        with patch.object(api, 'processes', return_value=iter(rows)), \
                patch.object(api, 'process_path', return_value=own / 'Game/Settlers7R.exe'):
            with self.assertRaises(RuntimeError): api.closed(own)
        with patch.object(api, 'processes', return_value=iter(rows)):
            with self.assertRaises(RuntimeError): api.closed()

    @staticmethod
    def core(mask, group=0):
        row = bytearray(48)
        struct.pack_into('<II', row, 0, 0, 48)
        struct.pack_into('<H', row, 30, 1)
        struct.pack_into('<QH', row, 32, mask, group)
        return row

    def test_two_physical_cores_without_smt_and_process_restrictions(self):
        data = self.core(3) + self.core(12) + self.core(48)
        self.assertEqual(bootstrap.core_mask(data, 63), 5)
        self.assertEqual(bootstrap.core_mask(data, 42), 10)
        self.assertEqual(bootstrap.core_mask(data, 48), 16)

    def test_bad_topology_and_unavailable_group_fail_closed(self):
        for data in (b'bad', self.core(1)[:-1], self.core(1, group=1)):
            with self.assertRaises(ValueError): bootstrap.core_mask(data, 1)

    def test_bridge_replays_original_call_after_aslr_and_guards_all_failures(self):
        from capstone import Cs, CS_ARCH_X86, CS_MODE_32
        for image, remote in ((0x400000, 0x20000000), (0x900000, 0x30000000)):
            blob, length = bridge(remote, image, remote+4096, r'C:\Gold\weplay_gold_startup.dll')
            code = list(Cs(CS_ARCH_X86, CS_MODE_32).disasm(blob[:length], remote))
            self.assertEqual(sum(len(i.bytes) for i in code), length)
            self.assertEqual([int(i.op_str, 16) for i in code if i.mnemonic == 'call' and i.op_str.startswith('0x')], [image+0x7af83e])
            self.assertEqual([int(i.op_str, 16) for i in code if i.mnemonic == 'jmp'], [image+0x7aee60])
            branches = [int(i.op_str, 16) for i in code if i.mnemonic in ('je', 'jne')]
            self.assertEqual(len(branches), 5); self.assertEqual(len(set(branches)), 1)
            self.assertEqual([i.mnemonic for i in code[:2]], ['pushfd', 'pushal'])
            self.assertEqual(blob[branches[0]-remote:branches[0]-remote+2], b'\xc7\x05')


if __name__ == '__main__':
    unittest.main()
