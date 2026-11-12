"""Owned synthetic archives only; no games, sockets or process creation."""
import copy
import hashlib
from pathlib import Path
import sys
import struct
import tempfile
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / 'tools/gold'))
sys.path.insert(0, str(ROOT / 'tests'))
import content_audit as audit
from test_lan_tools import synthetic_archive
import bba


class ContentAuditTests(unittest.TestCase):
    def setUp(self):
        self.temp = tempfile.TemporaryDirectory()
        self.addCleanup(self.temp.cleanup)
        self.path = Path(self.temp.name) / 'invented.bba'
        self.raw = synthetic_archive()
        self.path.write_bytes(self.raw)
        self.pin = hashlib.sha256(self.raw).hexdigest()

    def test_actual_entry_digest_agrees_despite_container_compression_and_replacement_history(self):
        first = audit.inventory(self.path, self.pin)
        rewritten = bba.Archive(self.raw).replace({'Script/example.lua': b'return false\n'})
        self.path.write_bytes(rewritten)
        second = audit.inventory(self.path, hashlib.sha256(rewritten).hexdigest())
        self.assertNotEqual(first['archive_sha256'], second['archive_sha256'])
        self.assertEqual(first['entry_digest'], second['entry_digest'])
        self.assertTrue(audit.compare({'gold': {'scripts': first}, 'history': {'scripts': second}})['scripts']['entries_equal'])

    def test_changed_missing_and_extra_entries_are_compared_over_the_union(self):
        first = audit.inventory(self.path, self.pin)
        for operation in ('change', 'remove', 'extra'):
            second = copy.deepcopy(first)
            if operation == 'change': second['files']['Script/example.lua']['sha256'] = 'f' * 64
            elif operation == 'remove': del second['files']['Script/example.lua']
            else: second['files']['Script/extra.lua'] = {'size': 1, 'sha256': '1' * 64}
            with self.subTest(operation=operation):
                compared = audit.compare({'gold': {'scripts': first}, 'history': {'scripts': second}})['scripts']
                self.assertFalse(compared['entries_equal'])
                self.assertEqual(len(compared['different_entries']), 1)

    def test_fingerprint_is_required_before_archive_parsing(self):
        with patch.object(audit, 'Archive') as parser:
            for pin in ('x', 'f' * 64, None):
                with self.subTest(pin=pin), self.assertRaises(ValueError):
                    audit.inventory(self.path, pin)
            parser.assert_not_called()

    def test_production_root_directory_metadata_is_accepted_but_dot_file_is_rejected(self):
        original = bba.Archive(self.raw)
        for kind in (256, 0):
            directory = bytearray(original.directory)
            at = original.hash_table_offset
            values = [0] * 16
            values[4], values[12] = kind, 1
            directory[at:at] = struct.pack('<16I', *values) + b'.\0\0\0'
            struct.pack_into('<I', directory, 8, at + 68)
            struct.pack_into('<I', directory, 272, 3)
            encrypted = bba.crypt(directory, bba.FILE_KEYS, 1532281822, 8, True)
            offset = struct.unpack_from('<I', original.header)[0]
            raw = bytearray(self.raw[:offset]) + encrypted
            header = struct.pack('<4I', offset, 0, len(encrypted), bba.crc32(encrypted))
            raw[16:32] = bba.crypt(header, bba.HEAD_KEYS, 1231853475, 7, True)
            self.path.write_bytes(raw)
            pin = hashlib.sha256(raw).hexdigest()
            if kind == 256:
                self.assertEqual(len(audit.inventory(self.path, pin)['files']), 2)
            else:
                with self.assertRaisesRegex(ValueError, 'entry path'):
                    audit.inventory(self.path, pin)

    def test_owned_archive_corruption_is_still_rejected_with_matching_outer_hash(self):
        raw = bytearray(self.raw)
        entry = bba.Archive(raw).entries['Script/example.lua']
        raw[entry.values[6]] ^= 1
        self.path.write_bytes(raw)
        with self.assertRaises((ValueError, OSError)):
            audit.inventory(self.path, hashlib.sha256(raw).hexdigest())

    def test_scope_prefix_requires_exact_directory_boundary_and_nonempty_selection(self):
        selected = audit.inventory(self.path, self.pin, 'Script/')
        self.assertEqual(len(selected['files']), 2)
        for prefix in ('Script', '../', '/Script/', 'Script\\', 'Missing/', 'Script/../'):
            with self.subTest(prefix=prefix), self.assertRaises(ValueError):
                audit.inventory(self.path, self.pin, prefix)
        second = copy.deepcopy(selected)
        second['prefix'] = 'Other/'
        with self.assertRaises(ValueError):
            audit.compare({'gold': {'scripts': selected}, 'history': {'scripts': second}})

    def test_run_reports_no_world_permission_and_omits_input_paths(self):
        entry = {'path': str(self.path), 'sha256': self.pin}
        result = audit.run({'profiles': {'gold': {'scripts': entry}, 'history': {'scripts': entry}}})
        self.assertFalse(result['world_permission'])
        self.assertFalse(result['complete_dependency_closure'])
        self.assertNotIn('path', result['profiles']['gold']['scripts'])
        self.assertEqual(self.path.read_bytes(), self.raw)

    def test_invalid_profile_structure_opens_no_game_file(self):
        for profiles in ({'gold': {}}, {'gold': {'a': {}}, 'history': {'b': {}}},
                         {'Gold': {'a': {}}, 'history': {'a': {}}}):
            with self.subTest(profiles=profiles), patch.object(audit, 'inventory') as read:
                with self.assertRaises(ValueError): audit.run({'profiles': profiles})
                read.assert_not_called()


if __name__ == '__main__':
    unittest.main()
