"""Pure boundary/derivation tests: no Windows handles or running game needed."""
import base64
import json
import math
import os
from pathlib import Path
import sys
import tempfile
from types import SimpleNamespace
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
import observe_game as observer


class ObserverTests(unittest.TestCase):
    def test_bounded_sampling_rejects_unbounded_and_nonfinite_values(self):
        for pid, seconds, interval in [(0, 1, 1), (-1, 1, 1), (2**32, 1, 1),
                (1, 121, 1), (1, 0, 1), (1, math.inf, 1), (1, math.nan, 1),
                (1, 30, .01), (1, 30, math.nan), (1, 30, 11)]:
            with self.subTest((pid, seconds, interval)), self.assertRaises(ValueError):
                observer.bounded_options(pid, seconds, interval)
        self.assertEqual(observer.bounded_options(1, 120, .25), 480)

    def test_output_only_new_owned_work_directory(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temp:
            root = Path(temp); (root / 'work').mkdir()
            self.assertEqual(observer.owned_output(root / 'work/new', root), (root / 'work/new').resolve())
            for bad in (root / 'outputs/new', root / 'work', root / 'work/../outside'):
                with self.assertRaises(ValueError): observer.owned_output(bad, root)
            (root / 'work/existing').mkdir()
            with self.assertRaises(ValueError): observer.owned_output(root / 'work/existing', root)

    def test_state_labels_do_not_guess_ui_from_network_flags(self):
        a = {'command_turn': 17, 'master_started': 1, 'master_state': 3}
        self.assertEqual(observer.state_label(a, a), 'simulation-idle')
        self.assertEqual(observer.state_label(a, {**a, 'command_turn': 18}), 'simulation-advancing')
        self.assertEqual(observer.state_label(a, {**a, 'command_turn': 0}), 'simulation-counter-reset')
        self.assertEqual(observer.state_label(None, a), 'native-state-unavailable')
        self.assertEqual(observer.state_label({'command_turn': 0xffffffff}, {'command_turn': 0}), 'simulation-advancing')

    def test_counter_cpu_deltas_do_not_charge_recycled_threads(self):
        old = {'cpu_seconds': 10, 'native': {'command_turn': 4, **dict.fromkeys(
            ('core_dispatches', 'owner_updates', 'manager_updates', 'echo_updates'), 0xffffffff)},
            'threads': [{'tid': 1, 'created_100ns': 99, 'cpu_seconds': 2},
                        {'tid': 2, 'created_100ns': 98, 'cpu_seconds': 5}]}
        new = {'cpu_seconds': 11, 'native': {'command_turn': 5, **dict.fromkeys(
            ('core_dispatches', 'owner_updates', 'manager_updates', 'echo_updates'), 1)},
            'threads': [{'tid': 1, 'created_100ns': 99, 'cpu_seconds': 2.5},
                        {'tid': 2, 'created_100ns': 100, 'cpu_seconds': 50}]}
        result = observer.interval_record(old, new, 2)
        self.assertEqual(result['cpu_core_equivalents'], .5)
        self.assertEqual(result['threads'][0]['cpu_core_equivalents'], .25)
        self.assertIsNone(result['threads'][1]['cpu_core_equivalents'])
        self.assertEqual(result['native_counter_deltas']['core_dispatches'], 2)

    def test_presentmon_command_is_pid_scoped_input_disabled_and_isolated(self):
        args = observer.presentmon_command('collector.exe', 55, 12.5, 'frames.csv', 'unique')
        self.assertEqual(args[args.index('--process_id')+1], '55')
        self.assertIn('--no_track_input', args)
        self.assertEqual(args[args.index('--timed')+1], '13')
        self.assertIn('--terminate_after_timed', args)
        for forbidden in ('--stop_existing_session', '--terminate_existing_session',
                          '--restart_as_admin', '--process_name', '--hotkey'):
            self.assertNotIn(forbidden, args)

    def test_missing_csv_is_not_reported_as_zero_fps(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temp:
            result = observer.summarize_frames(Path(temp) / 'missing.csv', 55)
            self.assertEqual(result, {'status': 'no-frame-csv', 'frames': 0})
            self.assertNotIn('fps', json.dumps(result))

    def test_collector_diagnostics_export_only_fixed_flags(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temp:
            path = Path(temp) / 'collector.log'
            path.write_text('untrusted secret\nStarted recording.\n'
                'warning: requires elevated privilege\n66 overflowed present events detected.\n')
            result = observer.collector_diagnostics(path)
            self.assertTrue(result['recording_started'])
            self.assertFalse(result['recording_stopped'])
            self.assertTrue(result['privilege_warning'])
            self.assertTrue(result['event_overflow_warning'])
            self.assertNotIn('secret', json.dumps(result))

    def test_two_core_map_does_not_change_existing_observer_registry(self):
        candidate = 'cfb1e3432a7990559bd167a5cdef97e68c539312535f0b846b3c7db3f3f5cb7f'
        self.assertEqual(observer.ADAPTERS[candidate], (0x3dfd6, 0x3e068, 0x3e06c, 0x3e070))
        self.assertNotIn(candidate, observer.INPUT_ADAPTERS)
        control = 'd6dfc374b823666b4f97b24165eabefd68d6a9623fe2fd4d216287a00a2348c4'
        self.assertEqual(observer.ADAPTERS[control], (0x3cfd6, 0x3d068, 0x3d06c, 0x3d070))
        self.assertEqual(observer.ADAPTER_IMAGES[control], 0x43000)
        self.assertEqual(observer.ADAPTER_IMAGES[candidate], 0x44000)

    def test_collector_fixed_flags_support_windows_utf16(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temp:
            path = Path(temp) / 'collector.log'
            for encoding in ('utf-16', 'utf-16-le'):
                path.write_bytes('Started recording.\nStopped recording.\n'.encode(encoding))
                result = observer.collector_diagnostics(path)
                self.assertTrue(result['recording_started'])
                self.assertTrue(result['recording_stopped'])

    def test_frame_summary_rejects_other_pids_bad_values_and_separates_chains(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temp:
            path = Path(temp) / 'f.csv'
            path.write_text('ProcessID,SwapChainAddress,FrameTime,secret\n'
                '55,0x100,10,hidden\n55,0x100,30,hidden\n55,0x200,100,hidden\n'
                '999,0x300,1,hidden\n55,0x100,nan,hidden\ninvalid,0x100,5,hidden\n')
            result = observer.summarize_frames(path, 55)
            self.assertEqual(result['frames'], 3)
            self.assertEqual(result['swapchains'][0]['cpu_frame_rate_hz'], 50)
            self.assertEqual(result['swapchains'][1]['cpu_frame_rate_hz'], 10)
            self.assertEqual(result['rejected_rows'], 3)
            self.assertNotIn('hidden', json.dumps(result))
            self.assertNotIn('0x100', json.dumps(result))

    def test_long_stall_is_retained_and_invalid_intervals_are_counted(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temp:
            path = Path(temp) / 'f.csv'
            path.write_text('ProcessID,SwapChainAddress,FrameTime\n'
                '55,0x100,16\n55,0x100,70000\n55,0x100,0\n'
                '55,0x100,-1\n55,0x100,inf\n')
            result = observer.summarize_frames(path, 55)
            self.assertEqual(result['frames'], 2)
            self.assertEqual(result['rejected_rows'], 3)
            chain = result['swapchains'][0]
            self.assertEqual(chain['max_frame_ms'], 70000)
            self.assertEqual(chain['frames_over_50_ms'], 1)
            self.assertEqual(chain['frames_over_1000_ms'], 1)
            self.assertLess(chain['cpu_frame_rate_hz'], .1)

    def test_presentmon_rejects_unknown_hash_before_signature_or_execution(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temp:
            path = Path(temp) / 'unknown.exe'; path.write_bytes(b'unknown')
            with patch.object(observer.subprocess, 'run') as run:
                with self.assertRaises(RuntimeError): observer.validate_presentmon(path)
                run.assert_not_called()

    def test_presentmon_checks_signature_version_with_literal_path(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temp:
            path = Path(temp) / "collector's file.exe"; path.write_bytes(b'fake')
            signature = b'{"Status":0,"StatusText":"Valid","ProductVersion":null,"SignerSubject":"CN=Intel Corporation, O=Intel Corporation, S=California, C=US"}'
            responses = [SimpleNamespace(returncode=0, stdout=signature),
                         SimpleNamespace(returncode=1, stdout=b'', stderr=b'PresentMon 2.6.0\r\n\r\nCapture Target Options:\r\n')]
            with patch.object(observer, 'digest', return_value=observer.PRESENTMON_SHA), \
                 patch.object(observer.subprocess, 'run', side_effect=responses) as run:
                self.assertEqual(observer.validate_presentmon(path), path.resolve())
                command = run.call_args_list[0].args[0]
                self.assertEqual(command[3], '-EncodedCommand')
                script = base64.b64decode(command[4]).decode('utf-16-le')
                self.assertIn("collector''s file.exe", script)
                self.assertIn('Get-AuthenticodeSignature -LiteralPath $p', script)
                self.assertEqual(run.call_args_list[1].args[0], [str(path.resolve()), '--help'])
            with patch.object(observer, 'digest', return_value=observer.PRESENTMON_SHA), \
                 patch.object(observer.subprocess, 'run', return_value=SimpleNamespace(returncode=2)):
                with self.assertRaises(RuntimeError): observer.validate_presentmon(path)

    def test_authenticode_actual_numeric_enum_and_missing_pe_version(self):
        actual = {'Status': 0, 'StatusText': 'Valid', 'ProductVersion': None,
                  'SignerSubject': 'CN=Intel Corporation, O=Intel Corporation, S=California, C=US'}
        observer.validate_signature_json(json.dumps(actual))
        for changes in ({'Status': False}, {'Status': 'Valid'}, {'Status': 1},
                        {'StatusText': 'UnknownError'}, {'SignerSubject': 'CN=Other Corporation'},
                        {'SignerSubject': None}):
            with self.subTest(changes), self.assertRaises(RuntimeError):
                observer.validate_signature_json(json.dumps({**actual, **changes}))
        for invalid in (b'[]', b'invalid', b'null'):
            with self.assertRaises(RuntimeError): observer.validate_signature_json(invalid)

    def test_module_guard_requires_exact_path_and_image_size(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temp:
            path = Path(temp) / 'known.dll'; path.write_bytes(b'x')
            other = Path(temp) / 'other.dll'; other.write_bytes(b'x')
            modules = [SimpleNamespace(path=str(path), image_size=0x44000, base=0x100000)]
            self.assertEqual(observer.OwnedGame.module_base(modules, path.resolve(), 0x44000), 0x100000)
            for requested, size in ((path, 0x45000), (other, 0x44000)):
                with self.assertRaises(RuntimeError): observer.OwnedGame.module_base(modules, requested, size)

    def test_snapshot_does_not_silently_accept_enumeration_failure(self):
        for first_result in (False, True):
            for error in (18, 5):
                with self.subTest(first=first_result, error=error):
                    api = observer.Windows.__new__(observer.Windows)
                    api.k = SimpleNamespace(CreateToolhelp32Snapshot=Mock(return_value=123),
                        Thread32First=Mock(return_value=first_result),
                        Thread32Next=Mock(return_value=False), CloseHandle=Mock())
                    with patch.object(observer.c, 'get_last_error', return_value=error, create=True):
                        snapshot = api.snapshot(4, 0, observer.Thread, 'Thread32First', 'Thread32Next')
                        if error == 18:
                            self.assertEqual(len(list(snapshot)), int(first_result))
                        else:
                            with self.assertRaisesRegex(RuntimeError, 'unvollstaendig'):
                                list(snapshot)
                    api.k.CloseHandle.assert_called_once_with(123)

    def test_unrelated_missing_driver_and_identical_wow64_views_are_safe(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temp:
            root = Path(temp)
            target = root / 'known.dll'; target.write_bytes(b'x')
            own = SimpleNamespace(path=str(target), image_size=0x44000, base=0x100000)
            missing = SimpleNamespace(path=str(root / 'missing-driver.dll'), image_size=1, base=0x200000)
            self.assertEqual(observer.OwnedGame.module_base([missing, own], target.resolve(), 0x44000), 0x100000)
            self.assertEqual(observer.OwnedGame.module_base([own, own], target.resolve(), 0x44000), 0x100000)
            distinct = SimpleNamespace(path=str(target), image_size=0x44000, base=0x200000)
            with self.assertRaises(RuntimeError):
                observer.OwnedGame.module_base([own, distinct], target.resolve(), 0x44000)
            wrong = SimpleNamespace(path=str(root / 'other/known.dll'), image_size=0x44000, base=0x300000)
            with self.assertRaises(RuntimeError):
                observer.OwnedGame.module_base([wrong], target.resolve(), 0x44000)

    def test_native_read_guard_rejects_unbounded_and_non_x86_reads(self):
        game = observer.OwnedGame.__new__(observer.OwnedGame)
        for address, size in ((0, 4), (0x10000, 5), (0x10000, 0), (0xffffffff, 4)):
            with self.assertRaises(RuntimeError): game.read(address, size)

    def test_unrecognized_game_and_adapter_rejected_before_any_handle(self):
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as temp:
            root = Path(temp); binary = root / 'Base/Bin'; binary.mkdir(parents=True)
            (binary / 'Settlers7R.exe').write_bytes(b'unknown')
            (binary / 'uplay_r1_loader.dll').write_bytes(b'unknown')
            api = SimpleNamespace()
            with self.assertRaisesRegex(RuntimeError, 'History'):
                observer.OwnedGame(api, root, 1)
            with patch.object(observer, 'digest', side_effect=[observer.EXE, 'unknown']):
                with self.assertRaisesRegex(RuntimeError, 'Adapter'):
                    observer.OwnedGame(api, root, 1)


if __name__ == '__main__':
    unittest.main()
