"""Authored Gold-decoder fixtures; no game scripts or archives distributed."""
import importlib.util
from pathlib import Path
import unittest
from unittest import mock
from test_gold_profile_interface import script

ROOT = Path(__file__).resolve().parents[1]
spec = importlib.util.spec_from_file_location('gold_graphics_assets', ROOT / 'tools/gold/graphics_assets.py')
graphics = importlib.util.module_from_spec(spec)
spec.loader.exec_module(graphics)
runtime_spec = importlib.util.spec_from_file_location('gold_visual_runtime', ROOT / 'tools/gold/visual_runtime.py')
visual = importlib.util.module_from_spec(runtime_spec)
runtime_spec.loader.exec_module(visual)


class GoldGraphicsTests(unittest.TestCase):
    def test_gold_runtime_does_not_change_history_pins_or_profile_paths(self):
        import reshade_runtime as history
        self.assertIsNot(visual.runtime, history)
        self.assertNotEqual(visual.runtime.CLOCK_SHA256, history.CLOCK_SHA256)
        self.assertNotEqual(visual.runtime.CLOCK_FILE, history.CLOCK_FILE)
        self.assertNotEqual(visual.runtime.FOLDER, history.FOLDER)
        self.assertEqual(history.CLOCK_SHA256, 'bc397467e09ffdad3c00c3f4654aa54766bf97a8cc7c5d26a22e30fee3aaa2e4')
        # Exercise the shared integrity imports as a real caller does, rather
        # than only mocking the Gold executable digest.
        self.assertEqual(visual.runtime._plain(ROOT / 'work'), (ROOT / 'work').resolve())
        self.assertEqual(visual.gold.EXE.as_posix(), 'Data/Base/_Dbg/Bin/Release/Settlers7R.exe')

    def test_gold_runtime_rejects_changed_exe_and_renderer_before_profile_creation(self):
        with mock.patch.object(visual.gold, 'digest', return_value='0' * 64):
            with self.assertRaisesRegex(ValueError, 'exact prepared Gold'):
                visual.checked_root(ROOT / 'work/invented-gold')
        with mock.patch.object(visual.gold, 'digest', side_effect=[visual.EXE_SHA256, '0' * 64]):
            with self.assertRaisesRegex(ValueError, 'pinned Modern'):
                visual.checked_root(ROOT / 'work/invented-gold')

    def test_unknown_archives_never_reach_decoders(self):
        for function in (graphics.interface, graphics.shared):
            with self.assertRaisesRegex(ValueError, 'exact original Gold'):
                function(b'not an owned game archive')

    def test_each_camera_transition_is_redirected_and_other_code_preserved(self):
        call = 'e["GFX"]["ToggleDepthOfField"]('
        body = ('local e={}\n' + call + 'true);\n') * 6 + 'return n(1)'
        _, patched, _ = graphics.gold.decoded_view(graphics.camera_script(script(body)))
        self.assertEqual(patched.count('weplayCameraDepthOfField(true)'), 6)
        self.assertEqual(patched.count('local weplayCameraDepthOfField='), 1)
        self.assertIn('return "caption"', patched)
        self.assertIn('G.UI.GfxGetWriteOptionValue(option)', patched)
        self.assertIn('G.UI.GfxSetOption(option, selected, false)', patched)

    def test_missing_or_extra_camera_transitions_fail(self):
        for count in (0, 5, 7):
            with self.assertRaisesRegex(ValueError, 'six original'):
                graphics.camera_script(script('e["GFX"]["ToggleDepthOfField"](true);' * count))

    def test_weather_menu_keeps_stock_values_and_adds_local_choice(self):
        body = ('r["weatherModes"]={["dayNightCycle"]=4,};'
                'l["weatherNumberDropdownList"]["setSelectedIndex"](o["gameplayOptions"][i["id"]["weather"]]);'
                'return n(1)')
        _, patched, _ = graphics.gold.decoded_view(graphics.settings_script(script(body)))
        self.assertIn('["dayNightCycle"]=4,["midsummer"]=5,}', patched)
        self.assertIn('Mittsommer / Midsummer', patched)
        self.assertIn('return "caption"', patched)
        with self.assertRaisesRegex(ValueError, 'exactly once'):
            graphics.settings_script(script(body + body))

    def test_weather_hook_is_local_and_has_no_simulation_or_random_calls(self):
        raw = graphics.tutorial_script(script('e["weather"]["create"]()e["head"]={}'))
        _, patched, _ = graphics.gold.decoded_view(raw)
        self.assertIn('end)(e,o,r);e["head"]={}', patched)
        self.assertIn('G.GFX.EnvSetBlend(handle,5000)', patched)
        self.assertNotIn('math.random', patched)
        self.assertNotIn('ExecuteCommand', patched)
        with self.assertRaisesRegex(ValueError, 'exactly once'):
            graphics.tutorial_script(script('return false'))


if __name__ == '__main__':
    unittest.main()
