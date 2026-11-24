"""Typed graphics failures must retain actionable, shareable launcher codes."""
from pathlib import Path
import sys
import unittest

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/offline-ui'), str(ROOT / 'tools/lan')]
import desktop_backend as backend
import display_settings as display


class GraphicsDiagnosticsTests(unittest.TestCase):
    def test_invalid_graphics_output_keeps_the_specific_launcher_error(self):
        value = dict(device=r'\\.\DISPLAY1', width=1920, height=1080, mode='borderless')
        screen = dict(device=value['device'], width=1920, height=1080,
                      resolutions=[[1920, 1080]], adapter='Display GPU')
        for screens, gpu, expected in (([], None, 'graphicsDisplayUnavailable'),
                                       ([screen], 'Different GPU', 'graphicsDisplayGpuMismatch')):
            with self.subTest(expected=expected):
                with self.assertRaises(display.DisplayUnavailable) as failure:
                    display.resolve(value, screens, renderer='dxvk', gpu=gpu)
                self.assertEqual(backend.error_code(failure.exception), expected)


if __name__ == '__main__':
    unittest.main()
