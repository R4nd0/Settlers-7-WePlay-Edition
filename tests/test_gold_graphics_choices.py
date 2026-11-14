"""Shared choices, independent Gold pins and bounded child-only application."""
from pathlib import Path
import importlib.util
import sys
import unittest
from unittest.mock import patch

ROOT=Path(__file__).resolve().parents[1]
sys.path.insert(0,str(ROOT/'tools/installer'))
import gold_graphics as gold
import graphics_choice as choice
import platform_standalone as platform
import reshade_runtime as history


class GoldChoicesTests(unittest.TestCase):
    def setUp(self):
        self.root=ROOT/'work/graphics-choice-fixture'
        self.selected=dict(format=choice.FORMAT,choice=None,simple_menu=True)

    def test_defective_maximum_is_rejected_at_every_output_before_runtime_access(self):
        for display in (None,dict(device=r'\\.\DISPLAY1',width=2560,height=1440,mode='windowed'),
                        dict(device=r'\\.\DISPLAY1',width=1920,height=1080,mode='windowed')):
            with patch.object(gold.graphics,'_read',side_effect=AssertionError('premature runtime access')):
                with self.assertRaisesRegex(ValueError,'gold_maximum_unavailable'):
                    gold.validate(self.root,dict(self.selected,maximum_fx=True,display=display))

    def test_defective_maximum_cannot_be_enabled_with_a_known_old_runtime(self):
        selected=dict(self.selected,maximum_fx=True,display=dict(device=r'\\.\DISPLAY1',width=1920,height=1080,mode='windowed'))
        with patch.object(gold.graphics,'_read',return_value=b'changed'):
            with self.assertRaisesRegex(ValueError,'gold_maximum_unavailable'):gold.validate(self.root,selected)

    def test_gold_reshade_never_changes_history_clock_or_profile(self):
        original=(history.CLOCK_FILE,history.CLOCK_SHA256,history.FOLDER,history.ADDON_DLL_SHA256)
        module=gold.runtime(self.root,self.selected)
        self.assertNotEqual(module.CLOCK_FILE,history.CLOCK_FILE)
        self.assertNotEqual(module.CLOCK_SHA256,history.CLOCK_SHA256)
        self.assertNotEqual(module.FOLDER,history.FOLDER)
        self.assertEqual(original,(history.CLOCK_FILE,history.CLOCK_SHA256,history.FOLDER,history.ADDON_DLL_SHA256))
        for flags in (dict(enhanced_aa=True),dict(midsummer_tone=True,contact_shadows=True)):
            values=dict(self.selected,**flags)
            self.assertEqual(module._preset(values),history._preset(values))

    def test_gold_renderer_is_fixed_but_uses_shared_choice_format(self):
        with patch.object(gold,'is_gold',return_value=True),patch.object(gold,'checked_root',return_value=self.root):
            self.assertEqual(choice._renderer(self.root,{'kept':'value'}),('dxvk',{'kept':'value'}))
        self.assertEqual(choice.validate_choice(dict(self.selected,extended_detail=True))['extended_detail'],True)

    def test_child_config_overrides_old_fixed_anisotropy_without_rewriting_it(self):
        exe=self.root/gold.EXE
        inherited={'dxvk_config':'unsafe','DxVk_Config_File':'foreign','dxvk_filter_device_name':'other','kept':'value'}
        for filtering in (False,True):
            options=dict(device_name='Test GPU',anisotropic_filtering=filtering)
            result=platform.launch_environment({'edition':'gold'},exe,inherited,graphics_options=options)
            self.assertEqual(inherited['dxvk_config'],'unsafe')
            self.assertEqual(result['DXVK_FILTER_DEVICE_NAME'],'Test GPU')
            self.assertIn('Strict',result['DXVK_CONFIG'])
            self.assertIn('16' if filtering else '-1',result['DXVK_CONFIG'])
            self.assertEqual(result['DXVK_CONFIG_FILE'],str(exe.with_name('dxvk.conf').resolve()))
            self.assertEqual(result['kept'],'value')

    def test_inline_options_reject_strings_controls_unknown_fields_and_wrong_edition(self):
        valid=dict(device_name=None,anisotropic_filtering=False)
        for options in (dict(valid,anisotropic_filtering='false'),dict(valid,device_name='GPU\n'),dict(valid,arbitrary=1)):
            with self.assertRaisesRegex(ValueError,'gold_graphics_invalid'):
                platform.launch_environment({'edition':'gold'},self.root/gold.EXE,{},graphics_options=options)
        with self.assertRaisesRegex(ValueError,'gold_graphics_invalid'):
            platform.launch_environment({'edition':'history'},self.root/gold.EXE,{},graphics_options=valid)

    def test_gold_start_reads_saved_choice_instead_of_history_layout(self):
        with patch.object(gold,'is_gold',return_value=True),patch.object(gold,'launch_environment',return_value={'applied':'yes'}) as launch:
            self.assertEqual(choice.saved_launch_environment(self.root,{}),{'applied':'yes'})
            launch.assert_called_once_with(self.root,{})

    def test_black_menu_and_background_choice_produce_real_gold_flags(self):
        for simple in (True,False):
            with patch.object(gold,'is_gold',return_value=True),patch.object(choice,'_load_choice',return_value=dict(self.selected,simple_menu=simple)):
                self.assertEqual(choice.game_arguments(self.root),('-vSynch','-noMapInMainMenu' if simple else '-mapInMainMenu'))


if __name__=='__main__':unittest.main()
