import struct
import unittest
from .draw_trace import configuration


class DrawTraceTests(unittest.TestCase):
    def test_legacy_abi_cannot_be_confused_with_trace(self):
        self.assertEqual(struct.unpack('<11I', configuration(1)), (44, 2, 1, 0, 0, 0, 0, 0, 0, 0, 0))

    def test_edge_roi_and_last_consecutive_draw_are_accepted(self):
        self.assertEqual(len(configuration(100, first_draw=32761, draw_count=8,
                                          target=(3840, 2160), roi=(3584, 1904, 256, 256))), 44)

    def test_requests_cannot_escape_pixel_or_work_bounds(self):
        base = dict(first_draw=20, draw_count=2, target=(1920, 1080), roi=(1900, 1000, 20, 80))
        for patch in (dict(first_draw=32768), dict(draw_count=9), dict(roi=(1901,1000,20,80)),
                      dict(roi=(0,0,257,2)), dict(roi=(-1,0,2,2)), dict(target=(3841,1080)),
                      dict(first_draw=True), dict(draw_count=0)):
            with self.subTest(patch=patch), self.assertRaises(ValueError):
                configuration(1, **{**base, **patch})
        for sequence in (0, True, -1, 1000001, 2**32):
            with self.subTest(sequence=sequence), self.assertRaises(ValueError):
                configuration(sequence)


if __name__ == '__main__':
    unittest.main()
