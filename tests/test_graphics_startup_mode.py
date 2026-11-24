import ctypes as C
import sys
from pathlib import Path
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/lan')]
import desktop_settings
import display_settings
import window_mode


@unittest.skipUnless(sys.platform == 'win32', 'Windows borderless readiness')
class GraphicsStartupModeTests(unittest.TestCase):
    def test_borderless_checkpoint_requires_the_requested_window_frame(self):
        screen = {
            'device': r'\\.\DISPLAY2', 'width': 1920, 'height': 1080, 'x': -1920, 'y': 0,
            'adapter': 'Fixture GPU', 'resolutions': [[1920, 1080]],
        }
        selection = {key: screen[key] for key in ('device', 'width', 'height')}
        selection['mode'] = 'borderless'
        preferences = {'renderer_policy': 'dxvk', 'choice': {'display': selection}, 'quality': {}}
        game = Mock(poll=Mock(return_value=None))
        for frame in ('windowed', 'wrong_monitor', 'requested'):
            with self.subTest(frame=frame):
                left = 0 if frame == 'wrong_monitor' else screen['x']
                def window_rect(_handle, pointer):
                    target = C.cast(pointer, C.POINTER(window_mode.W.RECT)).contents
                    target.left, target.top, target.right, target.bottom = left, 0, left + 1920, 1080
                    return True

                window = window_mode.Windows.__new__(window_mode.Windows)
                window.api = Mock(GetWindowRect=Mock(side_effect=window_rect))
                window.api.SetThreadDpiAwarenessContext.return_value = 2
                window.alive = Mock(return_value=True)
                window.owned = Mock(return_value=True)
                window.candidates = Mock(return_value=[77])
                window.responsive = Mock(return_value=True)
                window.get_style = Mock(side_effect=lambda _handle, slot:
                                        window_mode.OVERLAPPED if slot == -16 and frame == 'windowed' else 0)
                timer = Mock(monotonic=Mock(side_effect=(tick * .25 for tick in range(1000))))
                with patch.object(window_mode, 'owned_windows', return_value=window), \
                     patch.object(window_mode, 'time', timer), \
                     patch.object(desktop_settings, 'current_graphics', return_value=preferences), \
                     patch.object(desktop_settings, 'graphics_backup', return_value=None), \
                     patch.object(display_settings, 'enumerate_displays', return_value=[screen]), \
                     patch.object(desktop_settings, 'remember_graphics') as remember:
                    start = desktop_settings.GraphicsStartup(Mock(), ROOT)
                    start.observe(game)
                self.assertEqual(start.ready, frame == 'requested')
                self.assertEqual(start.observed, frame == 'requested')
                self.assertEqual(remember.call_count, int(frame == 'requested'))
