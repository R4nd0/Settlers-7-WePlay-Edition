"""Four physical cores by default; explicit diagnostic pairs avoid SMT siblings."""
from pathlib import Path
import struct
import sys
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
from isolated_process import core_mask


def core(mask, group=0, efficiency=0):
    row = bytearray(48)
    struct.pack_into('<II', row, 0, 0, 48)
    row[9] = efficiency
    struct.pack_into('<H', row, 30, 1)
    struct.pack_into('<QH', row, 32, mask, group)
    return row


class CpuAssignmentTests(unittest.TestCase):
    def test_four_core_default_excludes_smt_on_larger_cpus(self):
        topology = b''.join(core(3 << (2 * n)) for n in range(8))
        self.assertEqual(core_mask(topology, 0xffff), 0x55)
        self.assertEqual(core_mask(topology, 0xaaaa), 0xaa)
        self.assertEqual(core_mask(topology, 0xffff, core_offset=4), 0x5500)

    def test_fewer_physical_cores_use_only_the_available_count(self):
        for count in (1, 2, 3, 4):
            topology = b''.join(core(3 << (2 * n)) for n in range(count))
            self.assertEqual(core_mask(topology, 0xffff), sum(1 << (2*n) for n in range(count)))

    def test_hybrid_prefers_faster_allowed_physical_cores(self):
        topology = core(3) + b''.join(core(3 << (2*n), efficiency=1) for n in range(1, 5))
        self.assertEqual(core_mask(topology, 0xffff), 0x154)
        self.assertEqual(core_mask(topology, 0x3f), 0x15)

    def test_pair_has_disjoint_physical_cores_and_smt_siblings(self):
        topology = b''.join(core(3 << (2 * n)) for n in range(6))
        self.assertEqual(core_mask(topology, 4095, maximum=2), 5)
        self.assertEqual(core_mask(topology, 4095, maximum=2, core_offset=2), 80)
        self.assertEqual(core_mask(topology, 4095, maximum=2, core_offset=4), 1280)

    def test_offsets_apply_to_available_physical_cores(self):
        topology = b''.join(core(3 << (2 * n)) for n in range(5))
        self.assertEqual(core_mask(topology, 0b1010101010, maximum=2, core_offset=2), 160)
        self.assertEqual(core_mask(topology, 0b1111110000, maximum=2, core_offset=0), 80)
        with self.assertRaises(ValueError):
            core_mask(topology, 0b1111110000, maximum=2, core_offset=2)

    def test_no_silent_overlap_or_fallback_for_second_client(self):
        topology = core(3) + core(12) + core(48)
        for offset in (2, 3, 100, -1, True, 1.5):
            with self.assertRaises(ValueError):
                core_mask(topology, 63, maximum=2, core_offset=offset)
        self.assertEqual(core_mask(topology, 48), 16)

    def test_malformed_and_unavailable_group_remain_rejected(self):
        for topology in (b'bad', core(1)[:-1], core(1, group=1), core(0), core(3)+core(2)):
            with self.assertRaises(ValueError):
                core_mask(topology, 1)

    def test_invalid_limits_fail_instead_of_selecting_siblings(self):
        for maximum in (0, 1, 3, 5, True, 4.0, '4'):
            with self.assertRaises(ValueError):
                core_mask(core(3), 3, maximum=maximum)


if __name__ == '__main__':
    unittest.main()
