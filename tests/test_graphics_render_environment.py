"""Graphics runtime regressions using only disposable settings and Win32 fixtures."""
import ctypes as C
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/lan')]
import display_settings as display
import graphics_profile as graphics
import quality_settings as quality
import test_graphics_profile as fixtures


class RenderProfileTests(unittest.TestCase):
    snapshot = fixtures.GraphicsProfileTests.snapshot

    def setUp(self):
        fixtures.GraphicsProfileTests.setUp(self)
        graphics.install(self.root, self.source)

    def test_deleted_dxvk_output_directories_do_not_prevent_a_verified_launch(self):
        state = self.root / graphics.STATE
        (state / 'logs').rmdir()
        (state / 'cache').rmdir()
        before = self.snapshot()
        environment = graphics.launch_environment(self.root, {})
        self.assertTrue(Path(environment['DXVK_LOG_PATH']).is_dir())
        self.assertTrue(Path(environment['DXVK_SHADER_CACHE_PATH']).is_dir())
        self.assertEqual(self.snapshot(), before)

    def test_renderer_swap_requires_working_restore_source_before_it_mutates(self):
        executable = self.root / graphics.EXE
        renderer = executable.with_name('d3d9.dll')
        journal = self.root / graphics.RENDERER_JOURNAL
        package = self.parent / 'package'
        before = renderer.read_bytes()
        with self.assertRaises(ValueError):
            graphics.apply_launch_renderer(self.root, executable, package, 'native')
        self.assertEqual(renderer.read_bytes(), before)
        self.assertFalse(journal.exists())

    def test_options_are_recreated_when_an_owned_portable_profile_directory_is_missing(self):
        path = self.root / 'LocalData/ProfileRoot/Profiles/Options.ini'
        with patch.object(display, 'uses_portable_profile', return_value=True), \
                patch.object(display, 'options_path', return_value=path):
            quality.prepare(self.root, smaa=True)
        self.assertIn(b'Antialiasing = 0', path.read_bytes())


@unittest.skipUnless(os.name == 'nt', 'Windows display enumeration layouts')
class DisplayInventoryTests(unittest.TestCase):
    def screens(self, devices, unavailable=(), virtual_size=None):
        api = Mock()
        def device(_unused, index, pointer, _flags):
            if index >= len(devices):
                return False
            record = C.cast(pointer, C.POINTER(display.Device)).contents
            record.name, record.description, record.flags = devices[index]
            return True
        def mode(name, number, pointer):
            if name in unavailable or (number not in (0, 0xffffffff) and not (number == 1 and virtual_size)):
                return False
            value = C.cast(pointer, C.POINTER(display.Mode)).contents
            value.bpp = 32
            value.width, value.height = virtual_size if number == 1 else (1920, 1080)
            return True
        api.EnumDisplayDevicesW.side_effect = device
        api.EnumDisplaySettingsW.side_effect = mode
        with patch.object(C, 'WinDLL', return_value=api):
            return display.enumerate_displays(), api.EnumDisplaySettingsW

    def test_windows_mirroring_drivers_are_not_independent_game_outputs(self):
        desktop = r'\\.\DISPLAY1'
        mirror = r'\\.\DISPLAYV1'
        screens, modes = self.screens([(desktop, 'GPU', 0x5), (mirror, 'Mirror', 0x9)])
        self.assertEqual([item['device'] for item in screens], [desktop])
        self.assertTrue(all(call.args[0] != mirror for call in modes.call_args_list))

    def test_detaching_a_secondary_monitor_does_not_hide_a_working_primary(self):
        desktop, removed = r'\\.\DISPLAY1', r'\\.\DISPLAY2'
        screens, _ = self.screens([(desktop, 'GPU', 0x5), (removed, 'GPU', 0x1)], unavailable=[removed])
        self.assertEqual([item['device'] for item in screens], [desktop])

    def test_virtual_super_resolution_is_not_offered_as_a_window_larger_than_the_desktop(self):
        screens, _ = self.screens([(r'\\.\DISPLAY1', 'GPU', 0x5)], virtual_size=(3840, 2160))
        self.assertEqual(screens[0]['resolutions'], [[1920, 1080]])

    def test_previously_selected_oversized_window_requires_a_matching_desktop_size(self):
        screens, _ = self.screens([(r'\\.\DISPLAY1', 'GPU', 0x5)])
        current = screens[0]
        current.update(width=2560, height=1440, resolutions=[[2560, 1440], [3840, 2160]])
        choice = dict(device=current['device'], width=3840, height=2160, mode='windowed')
        with self.assertRaises(display.DisplayUnavailable):
            display.resolve(choice, screens)
        current.update(width=3840, height=2160)
        self.assertEqual(display.resolve(choice, screens), (0, current))


if __name__ == '__main__':
    unittest.main()
