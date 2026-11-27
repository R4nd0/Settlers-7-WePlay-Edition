"""Renderer staging/recovery failures only; no real game, profile or GPU access."""
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import Mock,patch
from types import SimpleNamespace

from . import dxwrapper as d, game, recover


class Tests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.bundle = self.root / 'work/game'
        self.case = self.root / 'work/case'
        self.bin = self.bundle / 'Base/Bin'
        self.bin.mkdir(parents=True)
        self.exe = self.bin / 'Settlers7R.exe'
        self.exe.write_bytes(b'synthetic original exe')
        self.original = b'synthetic original renderer'
        (self.bin / 'd3d9.dll').write_bytes(self.original)
        self.package = self.root / 'work/package'
        (self.package / 'Stub').mkdir(parents=True)
        (self.package / 'dxwrapper.dll').write_bytes(b'synthetic wrapper')
        (self.package / 'Stub/d3d9.dll').write_bytes(b'synthetic stub')
        for obj, name, value in (
                (recover, 'ROOT', self.root),
                (recover, 'DXVK', d.digest(self.original)),
                (d, 'DLL_SHA256', d.digest(b'synthetic wrapper')),
                (d, 'STUB_SHA256', d.digest(b'synthetic stub'))):
            mocked = patch.object(obj, name, value)
            mocked.start(); self.addCleanup(mocked.stop)
        mocked = patch.object(game.platform, 'load', return_value=(
            {'edition': 'history'}, self.exe, self.bin / 'uplay_r1_loader.dll'))
        mocked.start(); self.addCleanup(mocked.stop)
        mocked = patch.object(recover, '_no_games')
        self.no_games = mocked.start(); self.addCleanup(mocked.stop)

    def session(self, mode='dxwrapper-d3d9on12'):
        session = d.Session(self.bundle, self.case, mode,
                            None if mode == 'native' else self.package)
        self.case.mkdir()
        return session

    def test_worker_priority_is_automatic_only_for_corrected_on12_and_can_be_disabled(self):
        with patch('tools.graphics_lab.water_on12_probe.Probe'):
            automatic=d.Session(self.bundle,self.case,'dxwrapper-d3d9on12',self.package,water_probe=self.root/'build',water_pow_cap=True)
            self.assertIsNotNone(automatic.worker_policy)
            baseline=d.Session(self.bundle,self.case,'dxwrapper-d3d9on12',self.package,water_probe=self.root/'build',water_pow_cap=True,water_worker_priority=False)
            self.assertIsNone(baseline.worker_policy)
            benchmark=d.Session(self.bundle,self.case,'dxwrapper-d3d9on12',self.package,water_probe=self.root/'build',water_pow_cap=True,water_benchmark=True)
            self.assertIsNone(benchmark.worker_policy)

    def test_policy_cleanup_error_does_not_prevent_renderer_restoration(self):
        session=self.session();session.apply()
        policy=SimpleNamespace(done=True,close=Mock(side_effect=OSError('authored policy cleanup failure')))
        session.worker_policy=policy
        with self.assertRaises(OSError):session.restore()
        self.assertEqual((self.bin/'d3d9.dll').read_bytes(),self.original)
        self.assertFalse((self.bin/'dxwrapper.dll').exists())

    def test_runtime_requires_corrected_on12_before_any_platform_access(self):
        with patch.object(game.platform,'load') as load:
            for mode in d.MODES:
                with self.subTest(mode=mode),self.assertRaises(ValueError):
                    d.Session(self.bundle,self.case,mode,self.package,on12_runtime_build=self.root/'build')
            load.assert_not_called()

    def test_runtime_cleanup_failure_still_restores_original_renderer(self):
        session=self.session();session.apply()
        session.runtime=SimpleNamespace(restore=Mock(side_effect=OSError('authored runtime cleanup failure')))
        with self.assertRaises(OSError):session.restore()
        self.assertEqual((self.bin/'d3d9.dll').read_bytes(),self.original)
        self.assertFalse((self.bin/'dxwrapper.dll').exists())

    def test_both_wrappers_and_native_restore_original_and_preserve_saves(self):
        save = self.bundle / 'new-save.s7s'
        save.write_bytes(b'new save, never rolled back')
        for index, mode in enumerate(d.MODES):
            self.case = self.root / f'work/case-{index}'
            session = self.session(mode)
            session.apply()
            if mode == 'native':
                self.assertFalse((self.bin / 'd3d9.dll').exists())
            else:
                self.assertEqual((self.bin / 'd3d9.dll').read_bytes(), b'synthetic stub')
                self.assertEqual((self.bin / 'dxwrapper.ini').read_bytes(), d.configuration(mode))
            session.restore(); session.restore()
            self.assertEqual((self.bin / 'd3d9.dll').read_bytes(), self.original)
            self.assertFalse((self.bin / 'dxwrapper.dll').exists())
            self.assertFalse((self.bin / 'dxwrapper.ini').exists())
            self.assertEqual(save.read_bytes(), b'new save, never rolled back')

    def test_changed_target_refuses_entire_restore_before_any_mutation(self):
        session = self.session(); session.apply()
        (self.bin / 'dxwrapper.ini').write_bytes(b'newer user data')
        with self.assertRaisesRegex(RuntimeError, 'refusing to overwrite'):
            session.restore()
        self.assertEqual((self.bin / 'd3d9.dll').read_bytes(), b'synthetic stub')
        self.assertEqual((self.bin / 'dxwrapper.ini').read_bytes(), b'newer user data')

    def test_interrupted_partial_apply_recovers_from_prepared_journal(self):
        session = self.session()
        original = recover._atomic
        def fail_ini(path, raw, **kwargs):
            if Path(path) == self.bin / 'dxwrapper.ini':
                raise OSError('simulated interruption')
            return original(path, raw, **kwargs)
        with patch.object(recover, '_atomic', side_effect=fail_ini):
            with self.assertRaises(OSError):
                session.apply()
        self.assertEqual(json.loads((self.case/d.JOURNAL).read_text())['state'], 'prepared')
        session.restore()
        self.assertEqual((self.bin/'d3d9.dll').read_bytes(), self.original)

    def test_changed_backup_and_target_set_refused(self):
        session = self.session(); session.apply()
        backup = self.case / 'renderer-original/d3d9.dll'
        backup.write_bytes(b'corrupted')
        with self.assertRaisesRegex(RuntimeError, 'backup changed'):
            session.restore()
        backup.write_bytes(self.original)
        journal = self.case / d.JOURNAL
        record = json.loads(journal.read_text())
        record['targets']['../save.s7s'] = {'before': None, 'during': None}
        journal.write_text(json.dumps(record))
        with self.assertRaisesRegex(ValueError, 'target set'):
            session.restore()

    def test_live_game_blocks_staging_and_restoration(self):
        session = self.session()
        self.no_games.side_effect = RuntimeError('live game')
        with self.assertRaises(RuntimeError): session.apply()
        self.assertFalse((self.case / d.JOURNAL).exists())
        self.no_games.side_effect = None
        session.apply()
        self.no_games.side_effect = RuntimeError('live game')
        with self.assertRaises(RuntimeError): session.restore()
        self.assertEqual((self.bin / 'd3d9.dll').read_bytes(), b'synthetic stub')

    def test_artifact_drift_existing_config_and_source_drift_refused(self):
        (self.package / 'dxwrapper.dll').write_bytes(b'wrong version')
        with self.assertRaises(ValueError): self.session()
        (self.package / 'dxwrapper.dll').write_bytes(b'synthetic wrapper')
        (self.bin / 'dxwrapper.ini').write_bytes(b'existing config')
        with self.assertRaisesRegex(ValueError, 'must not be overwritten'): self.session()
        (self.bin / 'dxwrapper.ini').unlink()
        session = self.session()
        (self.bin / 'd3d9.dll').write_bytes(b'changed before staging')
        with self.assertRaisesRegex(RuntimeError, 'changed before staging'): session.apply()
        self.assertFalse((self.case / d.JOURNAL).exists())

    def test_no_production_auto_loading_or_non_lab_paths(self):
        choice = self.bundle / 'LocalData/graphics-choice.json'
        choice.parent.mkdir(); choice.write_bytes(b'{}')
        with self.assertRaisesRegex(ValueError, 'production graphics'): self.session()
        with self.assertRaises(ValueError): d.Session(self.root, self.case, 'native')

    def test_adapter_and_unrelated_diagnostics_refused_before_platform_load(self):
        for options in ({'hook': Path('never-loaded.dll')}, {'crash_watch': True},
                        {'on_initialized': lambda _: None}, {'detailed_shadows': True}):
            with self.subTest(options=options), patch.object(game.platform, 'load') as load:
                with self.assertRaisesRegex(ValueError, 'excludes native hooks'):
                    game.run('never-read', 'never-created', laboratory_renderer='native', **options)
                load.assert_not_called()
        with self.assertRaises(ValueError):
            game.run('never-read', 'never-created', dxwrapper_package=self.package)

    def fixture_build(self):
        build = self.root / 'work/build'
        build.mkdir()
        native = self.root / 'native'
        native.mkdir()
        source = native / 'dxwrapper_device_fixture.cpp'
        script = native / 'dxwrapper_device_fixture_build.ps1'
        exe = build / 'dxwrapper_device_fixture.exe'
        for path in (source, script, exe): path.write_bytes(path.name.encode())
        manifest = dict(format='s7-dxwrapper-device-fixture-v1',
                        source_sha256=d.digest(source.read_bytes()),
                        build_sha256=d.digest(script.read_bytes()),
                        binary_sha256=d.digest(exe.read_bytes()))
        (build / 'build.json').write_text(json.dumps(manifest))
        return build, source

    def test_device_fixture_preserves_each_mode_and_marks_results_synthetic(self):
        build, _ = self.fixture_build()
        output = self.root / 'work/fixture-results'
        completed = [self.completed(mode) for mode in d.MODES]
        with patch.object(game, 'ROOT', self.root), patch.object(game.memory_guard, 'observe_host_memory'), \
                patch.object(d.subprocess, 'run', side_effect=completed) as run:
            results = d.device_fixture(build, output, self.package)
        self.assertEqual(len(results), 3)
        self.assertTrue(all(r['synthetic_only'] for r in results))
        self.assertEqual(run.call_count, 3)
        self.assertFalse((output / 'native/d3d9.dll').exists())
        ini = (output / 'dxwrapper-d3d9on12/dxwrapper.ini').read_bytes()
        self.assertIn(b'IncludeProcess=dxwrapper_device_fixture.exe', ini)
        self.assertIn(b'D3d9on12=1', ini)

    def test_device_fixture_refuses_stale_build_before_output_or_gpu(self):
        build, source = self.fixture_build()
        source.write_bytes(b'new source requires rebuild')
        output = self.root / 'work/fixture-results'
        with patch.object(game, 'ROOT', self.root), patch.object(d.subprocess, 'run') as run:
            with self.assertRaisesRegex(ValueError, 'changed; rebuild'):
                d.device_fixture(build, output, self.package)
            run.assert_not_called()
        self.assertFalse(output.exists())

    def completed(self, mode, **changes):
        bridge = mode == 'dxwrapper-d3d9on12'
        value = dict(create_device_hr=0, on12_query_hr=0 if bridge else -2147467262,
                     d3d12_device_hr=0 if bridge else -2147467262,
                     raytracing_tier=11 if bridge else 0,
                     pixels_present_reset_passed=True, synthetic_only=True)
        value.update(changes)
        return SimpleNamespace(returncode=0, stdout=json.dumps(value).encode(), stderr=b'')

    def test_device_result_rejects_silent_fallback_even_with_successful_exit(self):
        fallback = self.completed('native')
        self.assertFalse(d.device_result('dxwrapper-d3d9on12', fallback)['passed'])
        for mode in d.MODES:
            self.assertTrue(d.device_result(mode, self.completed(mode))['passed'])
        self.assertFalse(d.device_result('native', self.completed('dxwrapper-d3d9on12'))['passed'])

    def test_device_result_rejects_pixel_failure_exit_failure_and_bad_schema(self):
        for value in (self.completed('native', pixels_present_reset_passed=False),
                      self.completed('native', create_device_hr=True),
                      SimpleNamespace(returncode=0, stdout=b'', stderr=b'')):
            self.assertFalse(d.device_result('native', value)['passed'])
        value = self.completed('native'); value.returncode = 7
        self.assertFalse(d.device_result('native', value)['passed'])

    def test_device_runner_reports_failure_after_preserving_all_mode_results(self):
        build, _ = self.fixture_build()
        output = self.root / 'work/failed-fixture-results'
        with patch.object(game, 'ROOT', self.root), patch.object(game.memory_guard, 'observe_host_memory'), \
                patch.object(d.subprocess, 'run', return_value=self.completed('native')):
            with self.assertRaisesRegex(RuntimeError, 'validation failed'):
                d.device_fixture(build, output, self.package)
        saved = json.loads((output / 'results.private.json').read_text())
        self.assertEqual(len(saved), 3)
        self.assertTrue(saved[0]['passed'])
        self.assertFalse(saved[2]['passed'])


if __name__ == '__main__': unittest.main()
