import copy
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
import display_settings as display
import graphics_profile as graphics


class DisplaySettingsTests(unittest.TestCase):
    def setUp(self):
        self.screens = [dict(device=r'\\.\DISPLAY1',adapter='GPU A',primary=True,x=0,y=0,
            width=1920,height=1080,resolutions=[[1280,720],[1920,1080]]),
            dict(device=r'\\.\DISPLAY2',adapter='GPU B',primary=False,x=-1920,y=0,
            width=1920,height=1080,resolutions=[[1280,720],[1920,1080]])]
        self.value = dict(device=r'\\.\DISPLAY2',width=1280,height=720,mode='windowed')

    def test_negative_coordinates_and_borderless_never_enable_exclusive_fullscreen(self):
        self.assertEqual(display.game_arguments(self.value,self.screens),
            ('-width','1280','-height','720','-screenNumber','1','-x','-1600','-y','180'))
        self.value.update(mode='borderless',width=1920,height=1080)
        self.assertEqual(display.game_arguments(self.value,self.screens),
            ('-width','1920','-height','1080','-screenNumber','1','-x','-1920','-y','0'))
        with self.assertRaises(ValueError): display.resolve(dict(self.value,width=1280,height=720),self.screens)
        with self.assertRaises(ValueError): display.validate(dict(self.value,mode='fullscreen'))

    def test_unknown_modes_missing_displays_and_third_adapter_are_rejected(self):
        screens = copy.deepcopy(self.screens)
        screens.append(dict(screens[1],device=r'\\.\DISPLAY3'))
        for values in (dict(self.value,width=1600), dict(self.value,device=r'\\.\DISPLAY3'),
                       dict(self.value,device=r'\\.\DISPLAY4')):
            with self.assertRaises(ValueError): display.resolve(values,screens)

    def test_dxvk_gpu_matches_selected_display_without_guessing_physical_indices(self):
        self.assertEqual(display.resolve(self.value,self.screens,renderer='dxvk',gpu='gpu b')[0],1)
        self.assertEqual(display.resolve(self.value,self.screens,renderer='dxvk',gpu=None)[0],1)
        with self.assertRaises(ValueError): display.resolve(self.value,self.screens,renderer='dxvk',gpu='GPU A')

    def test_keep_game_settings_has_no_generated_arguments(self):
        self.assertIsNone(display.validate(None))
        self.assertEqual(display.game_arguments(None,[]),())

    def test_schema_bounds_and_untrusted_values(self):
        for changes in ({'width':True},{'height':720.0},{'height':None},{'mode':[]},
                        {'width':3840,'height':3840},{'width':799},{'height':599},
                        {'arguments':[]},{'device':r'\\.\DISPLAY0'},{'device':'x\n'}):
            with self.subTest(changes=changes), self.assertRaises(ValueError):
                display.validate(dict(self.value,**changes))

    def test_unbounded_desktop_position_is_rejected(self):
        self.screens[1]['x'] = -999999
        with self.assertRaises(ValueError): display.game_arguments(self.value,self.screens)

    def test_windowed_ini_preserves_encoding_comments_and_unrelated_options(self):
        text = '; note\r\n[Quality]\r\nScreenWidth = 1920\r\nFullScreen = 1 ; keep\r\n[Other]\r\nName = test\r\n'
        for bom, encoding in ((b'','utf-8'),(b'\xef\xbb\xbf','utf-8'),
                              (b'\xff\xfe','utf-16-le'),(b'\xfe\xff','utf-16-be')):
            raw = bom + text.encode(encoding)
            expected = bom + text.replace('FullScreen = 1','FullScreen = 0').encode(encoding)
            self.assertEqual(display.windowed_options(raw), expected)
            self.assertEqual(display.windowed_options(expected), expected)
        self.assertEqual(display.windowed_options(b'[Quality]\nOther = 2\n[Next]\nX = 3\n'),
                         b'[Quality]\nOther = 2\nFullScreen = 0\n[Next]\nX = 3\n')
        self.assertEqual(display.windowed_options(b''),b'[Quality]\nFullScreen = 0\n')

    def test_windowed_ini_rejects_ambiguous_or_unknown_values(self):
        for raw in (b'[Quality]\nFullScreen = 2\n',b'[Quality]\nFullScreen = false\n',
                    b'[Quality]\nFullScreen = 0\nfullscreen = 1\n',
                    b'[Quality]\n[quality]\n',b'x'*65537,b';'+b'x'*65535):
            with self.subTest(raw=raw[:80]), self.assertRaises(ValueError): display.windowed_options(raw)

    def test_windowed_preparation_refuses_running_game_changes_and_write_failure(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as folder:
            path = Path(folder) / 'Options.ini'
            original = b'[Quality]\nFullScreen = 1\nTextureQuality = 2\n'
            path.write_bytes(original)
            with patch.object(display,'options_path',return_value=path), \
                    patch.object(graphics,'ensure_closed',side_effect=ValueError('running game')):
                with self.assertRaises(ValueError): display.prepare_windowed()
            self.assertEqual(path.read_bytes(),original)
            changed = b'[Quality]\nFullScreen = 1\nTextureQuality = 0\n'
            with patch.object(display,'options_path',return_value=path), \
                    patch.object(graphics,'ensure_closed',side_effect=lambda:path.write_bytes(changed)):
                with self.assertRaises(ValueError): display.prepare_windowed()
            self.assertEqual(path.read_bytes(),changed)
            with patch.object(display,'options_path',return_value=path), \
                    patch.object(graphics,'ensure_closed'), \
                    patch.object(graphics,'atomic_write',side_effect=OSError('disk error')):
                with self.assertRaises(OSError): display.prepare_windowed()
            self.assertEqual(path.read_bytes(),changed)

    def test_windowed_preparation_changes_only_its_key_and_skips_writes_when_already_zero(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as folder:
            path = Path(folder) / 'Options.ini'
            original = b'[Quality]\nFullScreen = 1\nTextureQuality = 2\n'
            path.write_bytes(original)
            with patch.object(display,'options_path',return_value=path), patch.object(graphics,'ensure_closed'):
                display.prepare_windowed()
            self.assertEqual(path.read_bytes(),original.replace(b'FullScreen = 1',b'FullScreen = 0'))
            with patch.object(display,'options_path',return_value=path), \
                    patch.object(graphics,'atomic_write') as writer, patch.object(graphics,'ensure_closed') as closed:
                display.prepare_windowed()
            writer.assert_not_called(); closed.assert_not_called()
