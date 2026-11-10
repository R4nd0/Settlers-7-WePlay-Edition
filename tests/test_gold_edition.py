"""Gold preparation safety tests; invented executable, no game/process/profile."""
import importlib.util
import json
import os
from pathlib import Path
import struct
import tempfile
import unittest
from unittest.mock import patch

REPO = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gold_prepare', REPO / 'tools/gold/prepare.py')
gold = importlib.util.module_from_spec(spec)
spec.loader.exec_module(gold)


class GoldTests(unittest.TestCase):
    @classmethod
    def setUpClass(cls):
        raw = bytearray(gold.RELOCATION_APPEND + 128)
        raw[:2] = b'MZ'
        struct.pack_into('<I', raw, 0x3c, 0x80)
        raw[0x80:0x86] = b'PE\0\0\x4c\x01'
        raw[0x98:0x9a] = b'\x0b\x01'
        struct.pack_into('<II', raw, 0x120, 0xd2c000, 0x9577c)
        struct.pack_into('<H', raw, 0xde, 0x8140)
        for offset, value in gold.OLD_RELOCATIONS:
            struct.pack_into('<H', raw, offset, value)
        for offset, before in gold.GUARDS.items():
            raw[offset:offset + len(bytes.fromhex(before))] = bytes.fromhex(before)
        cls.raw = bytes(raw)
        cls.definitions = gold.DEFINITIONS.read_bytes()

    def setUp(self):
        self.temp = tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT'))
        self.addCleanup(self.temp.cleanup)
        self.root = Path(self.temp.name)
        self.source = self.root / 'S7Gold/Settlers 7'
        exe = self.source / gold.EXE
        exe.parent.mkdir(parents=True)
        exe.write_bytes(self.raw)
        archive = self.source / 'Data/Base/BBA/Interface.bba'
        archive.parent.mkdir()
        archive.write_bytes(b'invented Gold archive, not History')
        (self.source / 'uplay_install.state').write_bytes(b'must not be copied')
        self.hash_guard = patch.object(gold, 'GOLD_SHA256', gold.sha256(self.raw))
        self.hash_guard.start()
        self.addCleanup(self.hash_guard.stop)

    def test_unlock_is_exact_and_keeps_all_other_bytes(self):
        result = gold.unlock_bytes(self.raw, self.definitions)
        allowed = set(range(0xd8, 0xdc))
        allowed.update(range(0x124, 0x128))
        allowed.update(range(gold.RELOCATION_APPEND, gold.RELOCATION_APPEND + len(gold.RELOCATION_BLOCK)))
        for offset, _ in gold.OLD_RELOCATIONS:
            allowed.update(range(offset, offset + 2))
        for offset, after in gold.gold_edits(self.definitions).items():
            self.assertEqual(result[offset:offset + len(after)], after)
            allowed.update(range(offset, offset + len(after)))
        self.assertEqual(len(result), len(self.raw))
        self.assertTrue(all(a == b or index in allowed for index, (a, b) in enumerate(zip(self.raw, result))))
        self.assertNotEqual(result, self.raw)
        self.assertNotEqual(struct.unpack_from('<I', result, 0xd8)[0], 0)
        self.assertEqual(result[0xde:0xe0], self.raw[0xde:0xe0])
        self.assertEqual(struct.unpack_from('<II', result, 0x120), (0xd2c000, 0x95788))
        for offset, _ in gold.OLD_RELOCATIONS:
            self.assertEqual(struct.unpack_from('<H', result, offset)[0], 0)
        self.assertEqual(result[gold.RELOCATION_APPEND:gold.RELOCATION_APPEND + 12], gold.RELOCATION_BLOCK)

    def test_relocation_guards_reject_independent_mutations(self):
        for offset in (0x120, 0x124, *[entry[0] for entry in gold.OLD_RELOCATIONS], gold.RELOCATION_APPEND):
            raw = bytearray(self.raw)
            raw[offset] ^= 1
            with patch.object(gold, 'GOLD_SHA256', gold.sha256(raw)):
                with self.assertRaisesRegex(ValueError, 'relocation'):
                    gold.unlock_bytes(raw, self.definitions)

    def test_truncated_relocation_data_is_rejected(self):
        with self.assertRaisesRegex(ValueError, 'Truncated Gold relocation'):
            gold.repair_relocations(bytearray(64))

    def test_unknown_executable_fails_before_output(self):
        (self.source / gold.EXE).write_bytes(self.raw + b'foreign')
        target = self.root / 'copy'
        with self.assertRaisesRegex(ValueError, 'Unknown or modified'):
            gold.prepare(self.source, target)
        self.assertFalse(target.exists())

    def test_instruction_guards_remain_independent_of_hash(self):
        raw = bytearray(self.raw)
        for offset in gold.GUARDS:
            changed = bytearray(raw)
            changed[offset] ^= 1
            with patch.object(gold, 'GOLD_SHA256', gold.sha256(changed)):
                with self.assertRaisesRegex(ValueError, 'original-byte guard'):
                    gold.unlock_bytes(changed, self.definitions)

    def test_definitions_are_pinned(self):
        with self.assertRaisesRegex(ValueError, 'definitions fingerprint'):
            gold.unlock_bytes(self.raw, self.definitions + b'changed')

    def test_wrong_pe_type_rejected(self):
        raw = bytearray(self.raw)
        raw[0x84] = 0x64
        with patch.object(gold, 'GOLD_SHA256', gold.sha256(raw)):
            with self.assertRaisesRegex(ValueError, 'x86'):
                gold.unlock_bytes(raw, self.definitions)

    def test_inspection_finds_nested_ubisoft_layout_without_writes(self):
        before = sorted(p.relative_to(self.source).as_posix() for p in self.source.rglob('*'))
        report = gold.inspect(self.source.parent)
        self.assertEqual(report['unlock_edits'], 16)
        self.assertFalse(report['crossplay_verified'])
        self.assertFalse(report['weplay_adapter_available'])
        self.assertFalse(report['game_launched'])
        self.assertFalse(report['profile_changes'])
        self.assertEqual(before, sorted(p.relative_to(self.source).as_posix() for p in self.source.rglob('*')))
        self.assertEqual((self.source / gold.EXE).read_bytes(), self.raw)

    def test_ambiguous_gold_source_is_rejected(self):
        nested = self.source / 'Settlers 7' / gold.EXE
        nested.parent.mkdir(parents=True)
        nested.write_bytes(self.raw)
        with self.assertRaisesRegex(ValueError, 'Ambiguous'):
            gold.source_root(self.source)

    def test_existing_destination_is_never_overwritten(self):
        target = self.root / 'copy'
        target.mkdir()
        sentinel = target / 'keep.txt'
        sentinel.write_bytes(b'keep')
        with self.assertRaisesRegex(ValueError, 'already exists'):
            gold.prepare(self.source, target)
        self.assertEqual(sentinel.read_bytes(), b'keep')

    def test_nested_and_parent_destinations_are_rejected(self):
        for target in (self.source, self.source / 'copy', self.source.parent):
            with self.assertRaisesRegex(ValueError, 'separate'):
                gold.prepare(self.source, target)

    def test_prepares_only_game_data_and_preserves_original(self):
        target = self.root / 'copy'
        report = gold.prepare(self.source.parent, target)
        self.assertEqual((self.source / gold.EXE).read_bytes(), self.raw)
        self.assertEqual((target / 'Backup/Settlers7R.original.exe').read_bytes(), self.raw)
        self.assertEqual(gold.digest(target / gold.EXE), report['unlocked_exe_sha256'])
        self.assertFalse((target / 'uplay_install.state').exists())
        self.assertFalse((target / 'PREPARATION-INCOMPLETE.txt').exists())
        self.assertFalse(report['starter_created'])
        self.assertFalse(report['shared_profile_isolated'])
        self.assertFalse(report['crossplay_verified'])
        self.assertFalse(report['graphics_verified'])
        self.assertFalse(report['dxvk_staged'])
        self.assertFalse(list(target.glob('*.cmd')))
        saved = json.loads((target / 'gold-development.json').read_text())
        self.assertEqual(saved['source_files'][gold.EXE.as_posix()], gold.sha256(self.raw))

    def test_failed_copy_remains_marked_incomplete(self):
        target = self.root / 'copy'
        with patch.object(gold.shutil, 'copy2', side_effect=OSError('disk failure')):
            with self.assertRaisesRegex(OSError, 'disk failure'):
                gold.prepare(self.source, target)
        self.assertTrue((target / 'PREPARATION-INCOMPLETE.txt').is_file())
        self.assertFalse((target / 'gold-development.json').exists())

    def test_foreign_graphics_dll_rejected_before_output(self):
        dll = self.root / 'foreign.dll'
        dll.write_bytes(b'foreign DLL')
        target = self.root / 'copy'
        with self.assertRaisesRegex(ValueError, 'DXVK DLL fingerprint'):
            gold.prepare(self.source, target, dxvk=dll)
        self.assertFalse(target.exists())

    def test_dxvk_staging_is_pinned_and_does_not_claim_rendering(self):
        dll = self.root / 'd3d9.dll'
        dll.write_bytes(b'invented x86 renderer')
        target = self.root / 'copy'
        with patch.object(gold, 'DXVK_SHA256', gold.digest(dll)):
            report = gold.prepare(self.source, target, dxvk=dll)
        self.assertEqual((target / gold.EXE).with_name('d3d9.dll').read_bytes(), dll.read_bytes())
        self.assertEqual((target / gold.EXE).with_name('dxvk.conf').read_bytes(), gold.DXVK_CONFIG)
        # Gold requests display adapters beyond index zero on this setup.
        # Physical-only enumeration can pass a factory probe yet fail CreateDevice.
        self.assertIn(b'd3d9.enumerateByDisplays = True\n', gold.DXVK_CONFIG)
        self.assertFalse(report['graphics_verified'])

    def test_existing_source_proxy_is_not_replaced(self):
        (self.source / gold.EXE).with_name('d3d9.dll').write_bytes(b'existing proxy')
        dll = self.root / 'new.dll'
        dll.write_bytes(b'new renderer')
        target = self.root / 'copy'
        with patch.object(gold, 'DXVK_SHA256', gold.digest(dll)):
            with self.assertRaisesRegex(ValueError, 'must not be overwritten'):
                gold.prepare(self.source, target, dxvk=dll)
        self.assertEqual((target / gold.EXE).with_name('d3d9.dll').read_bytes(), b'existing proxy')
        self.assertTrue((target / 'PREPARATION-INCOMPLETE.txt').exists())

    def test_symlink_is_rejected(self):
        link = self.root / 'linked-source'
        try:
            link.symlink_to(self.source, target_is_directory=True)
        except OSError:
            self.skipTest('Symlink creation unavailable')
        with self.assertRaisesRegex(ValueError, 'Links/reparse'):
            gold.source_root(link)


if __name__ == '__main__':
    unittest.main()
