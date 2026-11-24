import sys
from pathlib import Path
import unittest

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
import native_graphics_settings as native
import graphics_choice


class NativeGraphicsSettingsTests(unittest.TestCase):
    def test_legacy_choices_remain_identical_and_inactive(self):
        old = {'format': graphics_choice.FORMAT, 'choice': None}
        self.assertEqual(graphics_choice.validate_choice(old), old)
        self.assertEqual(native.requested_features(None), ())
        self.assertIsNone(native.render_size(None, None))

    def test_optional_preferences_round_trip_without_mutating_input(self):
        value = dict(native.DEFAULTS, render_scale=150)
        old = {'format': graphics_choice.FORMAT, 'choice': None, 'native_graphics': value}
        result = graphics_choice.validate_choice(old)
        self.assertEqual(result, old)
        result['native_graphics']['improved_water'] = True
        self.assertFalse(value['improved_water'])

    def test_1440p_to_4k_is_exact_and_no_5k_clamping(self):
        display = dict(width=2560, height=1440, mode='borderless')
        plan = native.render_size(dict(native.DEFAULTS, render_scale=150), display)
        self.assertEqual((plan['render_width'], plan['render_height']), (3840, 2160))
        self.assertEqual(plan['pixel_multiplier'], 2.25)
        with self.assertRaisesRegex(ValueError, 'resolution_limit'):
            native.render_size(dict(native.DEFAULTS, render_scale=200), display)

    def test_no_implicit_display_change_or_fractional_aspect_drift(self):
        value = dict(native.DEFAULTS, render_scale=150)
        with self.assertRaisesRegex(ValueError, 'display_required'):
            native.render_size(value, None)
        with self.assertRaises(ValueError):
            native.render_size(value, dict(width=1365, height=768, mode='windowed'))

    def test_feature_activation_needs_each_actual_capability(self):
        value = dict(native.DEFAULTS, render_scale=200, improved_water=True)
        with self.assertRaisesRegex(ValueError, 'unavailable'):
            native.require_features(value, ['supersampling'])
        self.assertEqual(native.require_features(value, ['supersampling', 'improved_water']),
                         ('supersampling', 'improved_water'))

    def test_rejects_booleans_as_scales_unknown_fields_and_non_boolean_effects(self):
        for value in (dict(native.DEFAULTS, render_scale=True), dict(native.DEFAULTS, render_scale=125),
                      dict(native.DEFAULTS, improved_water=1), dict(native.DEFAULTS, shader_path='external')):
            with self.subTest(value=value), self.assertRaises(ValueError):
                native.validate(value)


if __name__ == '__main__':
    unittest.main()
