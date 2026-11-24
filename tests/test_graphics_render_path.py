import ctypes as C
from ctypes import wintypes as W
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch


ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/lan')]
import display_settings as display
import gold_graphics
import graphics_choice as choice
import graphics_profile as graphics
import quality_settings as quality
import reshade_runtime


class ReShadePathTests(unittest.TestCase):
    def test_saved_smaa_choice_uses_game_aa_when_reshade_cannot_load(self):
        for edition, launch in (('history', choice.saved_launch_environment),
                                ('gold', gold_graphics.launch_environment)):
            with self.subTest(edition=edition), tempfile.TemporaryDirectory() as folder:
                root = Path(folder) / ('WePlay' + os.pathsep + 'Games')
                (root / 'LocalData').mkdir(parents=True)
                values = {**quality.DEFAULTS, 'Antialiasing': 0}
                (root / quality.FILE).write_bytes(graphics.encoded(values))
                options = root / 'Options.ini'
                original = b'[Effects]\nAntialiasing = 0\n'
                options.write_bytes(original)
                selected = {'format': choice.FORMAT, 'choice': None, 'enhanced_aa': True}
                with patch.object(graphics, '_preflight', return_value=root), \
                     patch.object(graphics, 'ensure_closed'), \
                     patch.object(graphics, 'launch_environment', return_value={}), \
                     patch.object(choice, '_renderer', return_value=('dxvk', {})), \
                     patch.object(choice, '_load_choice', return_value=selected), \
                     patch.object(gold_graphics, 'is_gold', return_value=edition == 'gold'), \
                     patch.object(gold_graphics, 'checked_root', return_value=root), \
                     patch.object(gold_graphics, 'runtime', return_value=reshade_runtime), \
                     patch.object(display, 'uses_portable_profile', return_value=True), \
                     patch.object(display, 'options_path', return_value=options), \
                     patch.object(reshade_runtime, 'prepare') as prepare:
                    env = launch(root, {})
                prepare.assert_not_called()
                self.assertNotIn('VK_INSTANCE_LAYERS', env)
                self.assertIn(b'Antialiasing = 1', options.read_bytes())
                self.assertEqual(quality.load(root), values)

    def test_high_integrity_launch_keeps_game_aa_when_loader_ignores_the_managed_layer(self):
        root = ROOT / 'work' / 'qualified-dxvk-fixture'
        selected = {'enhanced_aa': True}
        parent = {'PATH': 'fixture'}
        with patch.object(reshade_runtime, 'high_integrity', return_value=True, create=True), \
                patch.object(reshade_runtime, 'launch_environment',
                             return_value={**parent, 'VK_INSTANCE_LAYERS': 'VK_LAYER_reshade'}) as activate:
            child, smaa, fallback = choice.launch_managed_effects(root, selected, parent, reshade_runtime)
        self.assertEqual(child, parent)
        self.assertFalse(smaa)
        self.assertTrue(fallback)
        activate.assert_not_called()

        with patch.object(reshade_runtime, 'high_integrity', return_value=True), \
                patch.object(reshade_runtime, 'prepare') as prepare:
            with self.assertRaisesRegex(ValueError, 'reshade_elevation_unavailable'):
                reshade_runtime.launch_environment(root, parent, selected)
            prepare.assert_not_called()

    @unittest.skipUnless(sys.platform == 'win32', 'Windows token ABI')
    def test_elevated_token_detection_matches_the_loader_integrity_threshold(self):
        def open_token(_process, _access, token):
            C.cast(token, C.POINTER(W.HANDLE)).contents.value = 7
            return True

        def token_information(_token, _class, result, _length, _required):
            C.cast(result, C.POINTER(W.LPVOID)).contents.value = 13
            return True

        kernel = Mock(GetCurrentProcess=Mock(return_value=-1))
        security = Mock(OpenProcessToken=Mock(side_effect=open_token),
                        GetTokenInformation=Mock(side_effect=token_information),
                        GetSidSubAuthorityCount=Mock(return_value=C.pointer(C.c_ubyte(1))))
        libraries = {'kernel32': kernel, 'advapi32': security}
        with patch.object(C, 'WinDLL', side_effect=lambda name, **_: libraries[name]):
            for level, elevated in ((0x2000, False), (0x3000, True), (0x4000, True)):
                security.GetSidSubAuthority.return_value = C.pointer(W.DWORD(level))
                self.assertIs(reshade_runtime.high_integrity(), elevated)
        self.assertEqual(kernel.CloseHandle.call_count, 3)
