"""Regression cases for the renderer settings and owned-window paths."""
import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/offline-ui'), str(ROOT / 'tools/lan')]
import display_settings as display
import desktop_settings
import gold_graphics as gold
import graphics_choice as choice
import graphics_profile as graphics
import quality_settings as quality
import reshade_runtime as reshade
import test_graphics_profile as fixtures
import window_mode

SCREEN = dict(device=r'\\.\DISPLAY2', adapter='Display GPU', x=-1920, y=0,
              width=1920, height=1080, resolutions=[[1280, 720]])


class ChoiceChecks(unittest.TestCase):
    snapshot = fixtures.GraphicsProfileTests.snapshot

    def setUp(self):
        fixtures.GraphicsProfileTests.setUp(self)
        graphics.install(self.root, self.source)
        self.selected = dict(format=choice.FORMAT, choice=None)

    def test_gold_rejects_lod_setting_its_verified_adapter_cannot_apply(self):
        path = self.root / choice.CHOICE_FILE
        with patch.object(gold, 'checked_root', return_value=self.root), \
                patch.object(choice, 'visual_supported', return_value=True), \
                patch.object(choice, 'lod_maximum', return_value=4):
            with self.assertRaisesRegex(ValueError, 'update_required'):
                gold.save_choice(self.root, dict(self.selected, detail_level=5))
        self.assertFalse(path.exists())

    def test_supported_choice_can_replace_an_incompatible_old_lod_preference(self):
        path = self.root / choice.CHOICE_FILE
        with patch.object(choice, 'visual_supported', return_value=True), \
                patch.object(choice, 'lod_maximum', return_value=4):
            path.write_bytes(graphics.encoded(dict(self.selected, detail_level=5)))
            with self.assertRaisesRegex(ValueError, 'update_required'):
                choice.saved_launch_environment(self.root, {})
            desktop = Mock(installed=Mock(return_value={'renderer_policy': 'dxvk'}))
            self.assertEqual(desktop_settings.current_graphics(desktop, self.root)['choice']['detail_level'], 5)
            new = dict(self.selected, detail_level=4)
            choice.save_choice(self.root, new)
            self.assertEqual(choice._load_choice(self.root), new)

    def test_compatibility_smaa_falls_back_if_probed_gpu_is_on_other_display(self):
        requested = dict(self.selected, enhanced_aa=True,
                         display=dict(device=SCREEN['device'], width=1280, height=720, mode='windowed'))
        (self.root / choice.CHOICE_FILE).write_bytes(graphics.encoded(requested))
        with patch.object(display, 'enumerate_displays', return_value=[SCREEN]), \
                patch.object(reshade, 'probe_compatibility_smaa', return_value='Different GPU'), \
                patch.object(reshade, 'launch_environment', side_effect=lambda _, env, **_kw: env) as layer, \
                patch.object(quality, 'prepare') as settings:
            environment = choice.saved_launch_environment(self.root, {}, renderer='async',
                                                          renderer_package=self.parent)
        self.assertNotIn('DXVK_FILTER_DEVICE_NAME', environment)
        layer.assert_not_called()
        settings.assert_called_once_with(self.root, smaa=False, force_game_aa=True)

    def test_failed_reshade_preflight_does_not_disable_game_antialiasing(self):
        requested = dict(self.selected, enhanced_aa=True)
        (self.root / choice.CHOICE_FILE).write_bytes(graphics.encoded(requested))
        path = self.parent / 'Options.ini'
        before = b'[Effects]\nAntialiasing = 1\n'
        path.write_bytes(before)
        with patch.object(display, 'options_path', return_value=path), \
                patch.object(reshade, 'launch_environment', side_effect=ValueError('incomplete ReShade profile')):
            with self.assertRaisesRegex(ValueError, 'incomplete ReShade profile'):
                choice.saved_launch_environment(self.root, {})
        self.assertEqual(path.read_bytes(), before)


class DisplayChecks(unittest.TestCase):
    def test_current_borderless_mode_does_not_need_to_be_an_enumerated_mode(self):
        selection = dict(device=SCREEN['device'], width=1920, height=1080, mode='borderless')
        self.assertEqual(display.resolve(selection, [SCREEN]), (0, SCREEN))

    def test_legacy_ansi_options_preserve_original_text_bytes(self):
        before = b'; Qualit\xe4t\r\n[Quality]\r\nFullScreen = 1\r\nTextureQuality = 0\r\n'
        with self.subTest(writer='window'):
            self.assertEqual(display.windowed_options(before), before.replace(b'FullScreen = 1', b'FullScreen = 0'))
        with self.subTest(writer='quality'):
            changed = quality.replace(before, quality.DEFAULTS)
            self.assertTrue(changed.startswith(b'; Qualit\xe4t\r\n'))
            self.assertIn(b'TextureQuality = 1\r\n', changed)

    def test_missing_quality_sections_preserve_crlf_line_separators(self):
        result = quality.replace(b'[Other]\r\nName = unchanged\r\n', quality.DEFAULTS)
        self.assertNotIn(b'\n', result.replace(b'\r\n', b''))

    def test_reshade_preserves_windows_layer_aliases_without_duplicate_env_names(self):
        inherited = {'Vk_Layer_Path': 'another-layer-path', 'Vk_Instance_Layers': 'OtherLayer'}
        with patch.object(reshade, 'prepare', return_value='managed-layer'):
            child = reshade.launch_environment(Path('.'), inherited)
        self.assertEqual(child['VK_LAYER_PATH'], os.pathsep.join(('managed-layer', 'another-layer-path')))
        self.assertEqual(child['VK_INSTANCE_LAYERS'], os.pathsep.join(('OtherLayer', 'VK_LAYER_reshade')))
        self.assertEqual(len(child), 3)

    def test_borderless_controller_waits_out_multiple_transient_owned_windows(self):
        api = Mock()
        api.candidates.side_effect = [[37, 38], [38]]
        controller = window_mode.Controller(api, SCREEN)
        controller.tick([SCREEN])
        api.place.assert_not_called()
        controller.tick([SCREEN])
        api.place.assert_called_once_with(38, (-1920, 0, 1920, 1080))

    def test_failed_borderless_position_preserves_the_original_window_frame(self):
        api = window_mode.Windows.__new__(window_mode.Windows)
        api.api = Mock()
        api.owned = Mock(return_value=True)
        def rectangle(_window, pointer):
            value = C.cast(pointer, C.POINTER(W.RECT)).contents
            value.right, value.bottom = 1280, 720
            return True
        api.api.GetWindowRect.side_effect = rectangle
        api.api.SetWindowPos.return_value = False
        original = {-16: window_mode.OVERLAPPED | 0x10000000, -20: window_mode.EX_EDGES}
        styles = original.copy()
        api.get_style = lambda _window, slot: styles[slot]
        def set_style(_window, slot, value):
            previous = styles[slot]
            styles[slot] = value
            return previous
        api.set_style = set_style
        with self.assertRaises(OSError):
            api.place(37, (-1920, 0, 1920, 1080))
        self.assertEqual(styles, original)


if __name__ == '__main__':
    unittest.main()
