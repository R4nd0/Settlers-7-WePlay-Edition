"""Owned graphics preflights and last-working preference restoration."""
from contextlib import contextmanager
from pathlib import Path
import os
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
import memory_guard
import platform_standalone as platform
import quality_settings as quality
import reshade_runtime as reshade
import test_graphics_profile as fixtures
import window_mode


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
        manifest = {
            'format': backend.install.FORMAT,
            'capabilities': {'local_test': True, 'lan_host': False, 'lan_join': False},
            'files': {'payload/graphics/s7graphicsprobe.exe': graphics.content.digest(program)},
        }
        (package / 'package.json').write_bytes(graphics.encoded(manifest))
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

    def test_cached_probe_ignores_reports_from_the_previous_probe_executable(self):
        desktop = self.probe_desktop(previous=None)
        record = dict(format='s7-desktop-probe-v1', probe_sha256='0' * 64,
                      renderer_sha256=graphics.DXVK_SHA256, adapters=[],
                      devices=['Outdated GPU Name'], failure=None)
        desktop.read.return_value = record
        self.assertEqual(backend.Desktop.cached_graphics_probe(desktop)['devices'], [])
        record['probe_sha256'] = desktop.package_manifest()['files']['payload/graphics/s7graphicsprobe.exe']
        self.assertEqual(backend.Desktop.cached_graphics_probe(desktop), record)

    @unittest.skipUnless(sys.platform == 'win32', 'Windows GPU probe path')
    def test_gpu_probe_ignores_an_inherited_dxvk_device_uuid_filter(self):
        desktop = self.probe_desktop(previous=None)
        result = SimpleNamespace(returncode=0, stdout=b'{"format":"s7-render-probe-v1","devices":["Fixture GPU"]}')
        with patch.dict(os.environ, {'DXVK_FILTER_DEVICE_UUID': '0' * 32}), \
                patch.object(subprocess, 'run', return_value=result) as child:
            self.assertEqual(backend.Desktop.probe(desktop)['devices'], ['Fixture GPU'])
        self.assertFalse(any(name.upper() == 'DXVK_FILTER_DEVICE_UUID' for name in child.call_args.kwargs['env']))

    def test_missing_display_inventory_uses_automatic_output_for_a_stale_choice(self):
        desktop = Mock(displays=Mock(return_value=[]))
        preferred = dict(format=choice.FORMAT, choice=None,
                         display=dict(device=r'\\.\DISPLAY2', width=1920, height=1080, mode='borderless'))
        selected = backend.Desktop.default_choice(desktop, self.root, 'dxvk', {'devices': []}, preferred)
        self.assertIsNone(selected['display'])

    def test_late_game_exit_after_an_inconclusive_window_probe_keeps_graphics_choices(self):
        desktop = Mock()
        backup = (True, {'old': 'confirmed'})
        game = Mock(poll=Mock(return_value=None))
        pending = {'choice': {'format': choice.FORMAT, 'choice': None}}
        with patch.object(backend.desktop_settings, 'graphics_backup', return_value=backup), \
                patch.object(backend.desktop_settings, 'current_graphics', return_value=pending), \
                patch.object(window_mode, 'wait_until_ready', return_value=False):
            with self.assertRaisesRegex(RuntimeError, 'game_failed'):
                with backend.Desktop.graphics_startup(desktop, self.root) as startup:
                    startup.observe(game)
                    game.poll.return_value = 1
                    raise RuntimeError('game_failed')
        desktop.restore_graphics.assert_not_called()

    def gold_launch(self, *, normalize=False, monitor_error=None):
        before = dict(format=choice.FORMAT, choice=None,
                      display=dict(device=r'\\.\DISPLAY2', width=1920, height=1080, mode='windowed'))
        after = dict(before, display=None) if normalize else before
        renderer = 'native', self.parent / 'package'
        desktop = Mock(game=Mock(return_value=self.root), launch_renderer=Mock(return_value=renderer),
                       installed=Mock(return_value={'package_id': 'fixture'}),
                       package_id=Mock(return_value='fixture'),
                       default_choice=Mock(return_value=after),
                       cached_graphics_probe=Mock(return_value={'devices': []}))
        state = {'held': False}
        closed_under_lock = []
        @contextmanager
        def lock(root):
            self.assertEqual(root, self.root)
            state['held'] = True
            try:
                yield
            finally:
                state['held'] = False
        game = Mock(pid=123, wait=Mock(return_value=0))
        game.close.side_effect = lambda: closed_under_lock.append(state['held'])
        observer = Mock()
        observer.close.side_effect = monitor_error
        with patch.object(choice, '_load_choice', return_value=before), \
                patch.object(choice, 'save_choice') as save, \
                patch.object(choice, 'game_arguments', return_value=()), \
                patch.object(choice, 'saved_launch_environment', return_value={}), \
                patch.object(graphics, 'session_lock', side_effect=lock), \
                patch.object(platform, 'launch', return_value=game), \
                patch.object(backend.launch_local, 'try_window_mode', return_value=None), \
                patch.object(memory_guard, 'Probe'), patch.object(memory_guard, 'Sentinel', return_value=observer):
            if monitor_error is None:
                self.assertEqual(backend.Desktop.play_gold(desktop), {'exit_code': 0})
            else:
                with self.assertRaises(RuntimeError) as raised:
                    backend.Desktop.play_gold(desktop)
                self.assertIs(raised.exception, monitor_error)
        return save, after, closed_under_lock

    def test_gold_launch_reconciles_unavailable_output_before_start(self):
        save, expected, _ = self.gold_launch(normalize=True)
        save.assert_called_once_with(self.root, expected, renderer='native')

    def test_gold_launch_restores_temporary_renderer_before_releasing_copy_lock(self):
        _, _, closed_under_lock = self.gold_launch()
        self.assertEqual(closed_under_lock, [True])

    def test_gold_restores_renderer_even_if_memory_monitor_cleanup_fails(self):
        _, _, closed_under_lock = self.gold_launch(monitor_error=RuntimeError('memory monitor cleanup'))
        self.assertEqual(closed_under_lock, [True])


if __name__ == '__main__':
    unittest.main()
