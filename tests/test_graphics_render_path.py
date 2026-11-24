import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import patch


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
