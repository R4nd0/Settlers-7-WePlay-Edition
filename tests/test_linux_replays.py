"""Synthetic replay records only; no game data or identities."""
import gzip
from pathlib import Path
import struct
import sys
import tempfile
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tests/linux'))
import replay_checks


def record(value):
    return replay_checks.HEADER + replay_checks.FIELDS + struct.pack('<I', value) + b'coll'


class ReplayChecksTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory(); self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def write(self, name, data):
        path = self.root/name; path.write_bytes(data); return path

    def test_later_member_mismatch_cannot_be_hidden_by_first_member_success(self):
        left = self.write('a.rpl', gzip.compress(record(1)+record(2)) + gzip.compress(record(3)))
        right = self.write('b.rpl', gzip.compress(record(1)) + gzip.compress(record(2)+record(99)))
        result = replay_checks.compare(left, right)
        self.assertEqual((result['common'], result['mismatches'], result['first_mismatches']), (3, 1, [2]))
        self.assertTrue(result['all_gzip_members_read'])

    def test_record_split_between_members_and_unmatched_tail(self):
        raw = record(11)+record(22)
        left = self.write('a.rpl', gzip.compress(raw[:30]) + gzip.compress(raw[30:]))
        right = self.write('b.rpl', gzip.compress(raw+record(33)))
        result = replay_checks.compare(left, right)
        self.assertEqual((result['common'], result['mismatches']), (2, 0))
        self.assertEqual(result['unmatched_tail'], dict(left=0, right=1))

    def test_truncated_later_member_and_trailing_junk_are_rejected(self):
        first = gzip.compress(record(1)); second = gzip.compress(record(2))
        for name, data in (('truncated.rpl', first+second[:-3]), ('junk.rpl', first+b'junk')):
            with self.subTest(name=name), self.assertRaises(ValueError):
                replay_checks.read_checksums(self.write(name, data))

    def test_decompression_limit_and_missing_crc_records_are_rejected(self):
        path = self.write('large.rpl', gzip.compress(record(1)*10))
        with self.assertRaises(ValueError): replay_checks.read_checksums(path, max_raw=50)
        with self.assertRaises(ValueError):
            replay_checks.read_checksums(self.write('empty.rpl', gzip.compress(b'synthetic non-replay')))

    def test_changed_native_record_shape_is_rejected(self):
        data = bytearray(record(1)); data[25] = 9
        with self.assertRaises(ValueError):
            replay_checks.read_checksums(self.write('shape.rpl', gzip.compress(data)))


if __name__ == '__main__': unittest.main()
