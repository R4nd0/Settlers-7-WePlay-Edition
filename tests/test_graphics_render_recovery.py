"""Owned graphics preflights and last-working preference restoration."""
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/offline-ui'), str(ROOT / 'tools/lan')]
import desktop_backend as backend
import display_settings as display
import graphics_choice as choice
import graphics_profile as graphics
import quality_settings as quality
import reshade_runtime as reshade
import test_graphics_profile as fixtures


class GraphicsRecoveryTests(unittest.TestCase):
    snapshot = fixtures.GraphicsProfileTests.snapshot

    def setUp(self):
        fixtures.GraphicsProfileTests.setUp(self)
        graphics.install(self.root, self.source)

    def test_restoring_automatic_display_backup_keeps_game_settings(self):
        backup = {'renderer_policy': 'dxvk',
                  'choice': {'format': choice.FORMAT, 'choice': None, 'display': None},
                  'quality': quality.DEFAULTS}
        fallback = dict(device=r'\\.\DISPLAY1', width=1280, height=720, mode='windowed')
        desktop = Mock(displays=Mock(return_value=[]), supported_renderers=Mock(return_value=('dxvk',)),
                       recovery_display=Mock(return_value=fallback),
                       cached_graphics_probe=Mock(return_value={'devices': []}))
        with patch.object(backend.desktop_settings, 'graphics_backup', return_value=(True, backup)):
            backend.Desktop.restore_graphics(desktop, self.root)
        desktop.recovery_display.assert_not_called()
        self.assertIsNone(desktop.save_settings.call_args.args[0]['choice']['display'])

    @unittest.skipUnless(sys.platform == 'win32', 'Windows graphics dialog path')
    def test_disconnected_dialog_display_is_rejected_before_building_reshade_profile(self):
        selected = dict(format=choice.FORMAT, choice=None, enhanced_aa=True,
                        display=dict(device=r'\\.\DISPLAY2', width=1280, height=720, mode='windowed'))
        accepted = SimpleNamespace(returncode=0, stdout=graphics.encoded(selected))
        with patch.object(display, 'enumerate_displays', return_value=[]), \
                patch.object(subprocess, 'run', return_value=accepted), \
                patch.object(reshade, 'prepare') as prepare:
            with self.assertRaises(display.DisplayUnavailable):
                choice.choose_launch_environment(self.root, {})
        prepare.assert_not_called()
        self.assertFalse((self.root / choice.CHOICE_FILE).exists())

    def test_other_game_starting_during_options_read_prevents_graphics_write(self):
        path = self.parent / 'Options.ini'
        original = b'[Effects]\nAntialiasing = 1\n'
        path.write_bytes(original)
        self.closed.side_effect = [None, ValueError('another game started')]
        with patch.object(display, 'options_path', return_value=path):
            with self.assertRaisesRegex(ValueError, 'another game started'):
                quality.prepare(self.root, smaa=True)
        self.assertEqual(path.read_bytes(), original)

    def probe_desktop(self, previous):
        package = self.parent / 'probe-fixture'
        payload = package / 'Payload/Graphics'
        payload.mkdir(parents=True)
        program = payload / 'S7GraphicsProbe.exe'
        program.write_bytes(b'fixture probe executable')
        (payload / 'd3d9.dll').write_bytes(self.dll)
        manifest = {'files': {'payload/graphics/s7graphicsprobe.exe': graphics.content.digest(program)}}
        return Mock(state=self.root / 'LocalData', package=package,
                    package_manifest=Mock(return_value=manifest),
                    displays=Mock(return_value=[]), read=Mock(return_value=previous),
                    validate_probe=backend.Desktop.validate_probe)

    @unittest.skipUnless(sys.platform == 'win32', 'Windows GPU probe path')
    def test_corrupted_probe_cache_does_not_prevent_a_fresh_probe(self):
        desktop = self.probe_desktop(previous=['old invalid cache'])
        result = SimpleNamespace(returncode=0, stdout=b'{"format":"s7-render-probe-v1","devices":["Fixture GPU"]}')
        with patch.object(subprocess, 'run', return_value=result) as run:
            self.assertEqual(backend.Desktop.probe(desktop)['devices'], ['Fixture GPU'])
        run.assert_called_once()
        desktop.write.assert_called_once()

    @unittest.skipUnless(sys.platform == 'win32', 'Windows GPU probe path')
    def test_unnamed_adapter_does_not_discard_valid_probe_devices(self):
        desktop = self.probe_desktop(previous=None)
        result = SimpleNamespace(returncode=0, stdout=b'{"format":"s7-render-probe-v1","devices":["","Fixture GPU"]}')
        with patch.object(subprocess, 'run', return_value=result):
            record = backend.Desktop.probe(desktop)
        self.assertEqual(record['devices'], ['Fixture GPU'])
        self.assertIsNone(record['failure'])

    @unittest.skipUnless(sys.platform == 'win32', 'Windows GPU probe path')
    def test_probe_cache_from_before_renderer_fingerprinting_is_rebuilt(self):
        desktop = self.probe_desktop(previous=None)
        signature = desktop.package_manifest()['files']['payload/graphics/s7graphicsprobe.exe']
        desktop.read.return_value = dict(format='s7-desktop-probe-v1', probe_sha256=signature,
                                         adapters=[], devices=['Unverified GPU'], failure=None)
        self.assertEqual(backend.Desktop.cached_graphics_probe(desktop)['devices'], [])
        result = SimpleNamespace(returncode=0, stdout=b'{"format":"s7-render-probe-v1","devices":["Fixture GPU"]}')
        with patch.object(subprocess, 'run', return_value=result) as run:
            record = backend.Desktop.probe(desktop)
        run.assert_called_once()
        self.assertEqual(record['devices'], ['Fixture GPU'])
        self.assertEqual(record['renderer_sha256'], graphics.DXVK_SHA256)


if __name__ == '__main__':
    unittest.main()
