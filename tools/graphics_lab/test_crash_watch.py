from pathlib import Path
from contextlib import nullcontext
import json
import os
import subprocess
import tempfile
import unittest
from unittest.mock import Mock, patch

from tools.graphics_lab import crash_watch as w


def identity(**changes):
    value = dict(pid=123, parent=456, path='C:/private/cdb.exe',
                 sha256=w.CDB_PINS['cdb.exe'], creation_time=789)
    value.update(changes)
    return value


class CommandTests(unittest.TestCase):
    def test_fixed_commands_capture_both_chances_and_leave_handler_original(self):
        text = w.commands()
        self.assertIn('sxe -c "', text)
        self.assertIn('-c2 "', text)
        self.assertIn('first-chance.dmp; gn', text)
        self.assertIn('second-chance.dmp }; qd', text)
        self.assertIn('@$t0 < 3', text)
        self.assertIn('@$t0 < 4', text)
        self.assertEqual(text.count('.dump /m /u'), 2)
        for command in ('.exr -1', '.ecxr', 'kv', 'lm'):
            self.assertEqual(text.count(command), 2)
        for forbidden in (' gh', '/ma', '/o ', '.shell', '.call', 'ExitProcess', '.kill', '-iae'):
            self.assertNotIn(forbidden, text)

    def test_argv_independence_and_pd_are_unconditional(self):
        path = Path('C:/with spaces/a;&$name/cdb.exe')
        args = w.arguments(path, 123)
        self.assertEqual(args[0], str(path))
        self.assertIn('-pd', args)
        self.assertIn('-noshell', args)
        self.assertIn('-nosqm', args)
        self.assertIn('-hd', args)
        self.assertNotIn('-pn', args)
        self.assertNotIn('-c', args)
        self.assertEqual(args[-2:], ['-p', '123'])
        self.assertEqual(args[args.index('-cf') + 1], 'watch.cmd')
        self.assertNotIn(str(path), w.commands())

    def test_steam_header_reads_are_exact_fault_scoped_and_bounded(self):
        text = w.commands(steam_heap=True)
        for command in ('@eip == (Settlers7R+0x9350e0)', 'dd @edi-0x20 L40',
                        'dd @esi L20', 'dd (Settlers7R+0xf756b0) L1','dd @edi-0xa18 L40'):
            self.assertEqual(text.count(command), 2)
        # Failed pointer reads must not abort the subsequent dump/gn commands.
        self.assertNotIn('poi(', text)
        self.assertNotIn('Settlers7R',w.commands())
        for command in ('.writemem', '.readmem', 'ed ', 'eb ', 'bp ', 'ba '):
            self.assertNotIn(command, text)

    def test_pid_is_never_command_text(self):
        for pid in (True, 0, -1, 0x100000000, '123;qd', '123'):
            with self.subTest(pid=pid), self.assertRaises(ValueError):
                w.arguments('cdb.exe', pid)

    def test_environment_drops_debug_paths_and_leaves_gamepace_alone(self):
        env = dict(PATH='Windows', _NT_SYMBOL_PATH='srv*evil', _NT_ALT_SYMBOL_PATH='evil',
                   _NT_DEBUGGER_EXTENSION_PATH='evil', _NT_EXECUTABLE_IMAGE_PATH='evil',
                   DBGHELP='evil', CDB='evil', WINDBG='evil', unrelated='value')
        self.assertEqual(w.environment(env), dict(PATH='Windows', unrelated='value'))

    def test_cli_cannot_select_pid_export_or_commands(self):
        for extra in (['--pid', '123'], ['--export', 'X'], ['--command', 'g']):
            with self.subTest(extra=extra), patch('sys.stderr'), self.assertRaises(SystemExit) as ctx:
                w.main(['--case', 'case', '--output', 'out', *extra])
            self.assertEqual(ctx.exception.code, 2)

    def test_lifetime_bound_before_any_native_operation(self):
        for seconds in (299, 601, True, '300'):
            with self.subTest(seconds=seconds), patch.object(w, 'Native') as native, self.assertRaises(ValueError):
                w.watch('case', 'out', seconds=seconds)
            native.assert_not_called()

    def test_echoed_commands_are_not_access_violation_events(self):
        raw = ('0:000> ' + w.commands() + '\nS7_OWNED_CRASH_WATCH_READY\n').encode('ascii')
        self.assertEqual(w.av_event_counts(raw), dict(first_chance=0, second_chance=0))
        self.assertEqual(w.av_event_counts(raw + b'S7_FIRST_CHANCE_AV\r\nS7_SECOND_CHANCE_AV\n'),
                         dict(first_chance=1, second_chance=1))
        self.assertIsNone(w.av_event_counts(b'x' * (w.MAX_BYTES + 1)))


class IdentityTests(unittest.TestCase):
    def test_every_original_identity_field_is_required(self):
        expected = identity()
        w._match(expected, dict(expected))
        for key in expected:
            changed = dict(expected)
            changed[key] = changed[key] + 1 if isinstance(changed[key], int) else changed[key] + 'x'
            with self.subTest(key=key), self.assertRaises(RuntimeError):
                w._match(expected, changed)

    def test_pid_reuse_even_identical_file_parent_refused(self):
        with self.assertRaises(RuntimeError):
            w._match(identity(), identity(creation_time=790))

    def test_admission_requires_after_load_and_save(self):
        owned = Mock()
        with self.assertRaises(ValueError):
            w.admit(owned, 'case')
        owned.owned.assert_not_called()

    def test_admission_refuses_exited_original_handle(self):
        owned = Mock(native_initialized=True)
        owned.poll.return_value = 0
        with self.assertRaises(ValueError):
            w.admit(owned, 'case', load_and_new_save_confirmed=True)

    def test_missing_or_changed_case_is_not_admitted(self):
        with patch.object(w, '_case', side_effect=ValueError('closed')):
            with self.assertRaises(ValueError):
                w._admitted('case', Mock())

    def test_duplicate_json_rejected(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d) / 'case.json'; p.write_text('{"pid":1,"pid":2}')
            with self.assertRaises(ValueError):
                w._json(p)

    def test_original_handle_admission_records_child_parent_and_create_time(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d); case = root / 'case'; case.mkdir()
            game_root = root / 'game'; game_root.mkdir()
            dll = root / 'WePlaySupersamplingLab.dll'; dll.write_bytes(b'hook')
            owned = Mock(pid=123, expected='4' * 64, native_initialized=True,
                         executable=game_root / 'Settlers7R.exe', _handle=456)
            owned.poll.return_value = None
            record = dict(pid=123, parent=os.getpid(), path=str(owned.executable),
                          sha256=owned.expected, native_initialized=True)
            (case / 'owned.private.json').write_text(json.dumps(record))
            for name in (w.recover.JOURNAL, 'checkpoint.private.json',
                         'hook-initialized.private.json', 'hook-build.private.json'):
                (case / name).write_text('{}')
            child = identity(pid=123, parent=os.getpid(), path=str(owned.executable),
                             sha256=owned.expected, creation_time=1000)
            parent = identity(pid=os.getpid(), parent=44, creation_time=900)
            native = Mock(); native.modules.return_value = [dll]
            native.identity.side_effect = [child, parent]
            with patch.object(w, '_case', return_value=(case, game_root)), \
                    patch.object(w, '_hook', return_value=dll), \
                    patch.object(w, 'Native', return_value=native):
                result = w.admit(owned, case, dll, load_and_new_save_confirmed=True)
            self.assertEqual(result['child'], child)
            self.assertEqual(result['parent'], parent)
            self.assertEqual(result['hook_sha256'], w.digest(dll))
            self.assertEqual(result['watcher_sha256'], w.digest(w.__file__))
            self.assertTrue((case / w.ADMISSION).is_file())
            native.identity.assert_any_call(123, 456)

    def test_unadmitted_case_never_opens_any_process(self):
        with tempfile.TemporaryDirectory() as d:
            case = Path(d); native = Mock()
            with patch.object(w, '_case', return_value=(case, case / 'game')):
                with self.assertRaises(FileNotFoundError):
                    w._admitted(case, native)
            native.open.assert_not_called()

    def test_cdb_rejects_basename_and_unexpected_module(self):
        with tempfile.TemporaryDirectory() as d:
            root = Path(d)
            with patch.object(w.recover, 'lab', side_effect=lambda p: Path(p)):
                with self.assertRaises(ValueError):
                    w.verify_cdb(root / 'game.exe', Mock())
                (root / 'cdb.exe').write_bytes(b'notcdb')
                with self.assertRaises(ValueError):
                    w.verify_cdb(root / 'cdb.exe', Mock())

    def test_case_requires_explicit_allowance_and_exact_watcher_checkpoint(self):
        with tempfile.TemporaryDirectory() as d:
            case = Path(d)
            journal = dict(format=w.recover.FORMAT, state='prepared', root=str(case / 'game'), critical={})
            base = dict(files={}, crash_watch_allowed=True,
                        sources={'tools/graphics_lab/crash_watch.py': w.digest(w.__file__)})
            for changes in (
                    {'crash_watch_allowed': False},
                    {'crash_watch_allowed': 1},
                    {'sources': {'tools/graphics_lab/game.py': '0' * 64}},
                    {'sources': {'tools/graphics_lab/crash_watch.py': '0' * 64}}):
                with self.subTest(changes=changes), \
                        patch.object(w.recover, 'lab', side_effect=lambda p: Path(p)), \
                        patch.object(w.recover, '_critical'), \
                        patch.object(w, '_json', side_effect=[journal, {**base, **changes}]):
                    with self.assertRaises(ValueError):
                        w._case(case)


class LimitTests(unittest.TestCase):
    def test_usage_counts_private_dumps_and_logs(self):
        with tempfile.TemporaryDirectory() as d:
            p = Path(d)
            (p / 'one.dmp').write_bytes(b'a' * 4)
            (p / 'two.dmp').write_bytes(b'b' * 6)
            (p / 'cdb.private.log').write_bytes(b'c' * 3)
            (p / 'symbols').mkdir()
            self.assertEqual(w.usage(p), dict(dumps=2, bytes=13))

    def test_qd_success_does_not_terminate_any_process(self):
        p = Mock(pid=123, _handle=99)
        p.poll.return_value = None
        native = Mock(); native.identity.return_value = identity()
        result = w.stop_debugger(p, identity(), native)
        p.stdin.write.assert_called_once_with(b'qd\n')
        p.wait.assert_called_once_with(timeout=2)
        p.terminate.assert_not_called()
        self.assertEqual(result, dict(qd_attempted=True, debugger_terminated=False))

    def test_timeout_terminates_only_retained_pinned_cdb_after_qd(self):
        p = Mock(pid=123, _handle=99)
        p.poll.return_value = None
        p.wait.side_effect = [subprocess.TimeoutExpired('cdb', 2), 0]
        original = identity(parent=999)
        native = Mock(); native.identity.return_value = original
        with patch.object(w.os, 'getpid', return_value=999):
            result = w.stop_debugger(p, original, native)
        p.terminate.assert_called_once_with()
        self.assertEqual(native.identity.call_count, 2)
        self.assertTrue(result['debugger_terminated'])
        self.assertLess(p.mock_calls.index(unittest.mock.call.stdin.write(b'qd\n')),
                        p.mock_calls.index(unittest.mock.call.terminate()))

    def test_unpinned_or_reused_debugger_never_terminated(self):
        for altered in (identity(creation_time=790), identity(sha256='0' * 64), identity(parent=1234)):
            p = Mock(pid=123, _handle=99); p.poll.return_value = None
            p.wait.side_effect = subprocess.TimeoutExpired('cdb', 2)
            native = Mock(); native.identity.return_value = altered
            with self.subTest(altered=altered), self.assertRaises(RuntimeError):
                w.stop_debugger(p, identity(), native)
            p.terminate.assert_not_called()

    def test_already_exited_debugger_never_writes_or_terminates(self):
        p = Mock(); p.poll.return_value = 0
        w.stop_debugger(p, identity(), Mock())
        p.stdin.write.assert_not_called(); p.terminate.assert_not_called()


class RuntimeTests(unittest.TestCase):
    def fixture(self, directory):
        case = Path(directory) / 'case'; case.mkdir()
        game = Path(directory) / 'game'; game.mkdir()
        child = identity(pid=11, parent=12, path=str(game / 'Settlers7R.exe'), creation_time=100)
        parent = identity(pid=12, parent=13, creation_time=90)
        cdb = Path(directory) / 'cdb.exe'
        debugger = identity(pid=99, parent=os.getpid(), path=str(cdb))
        admission = dict(child=child, parent=parent, game_root=str(game), hook=str(game / 'hook.dll'))
        native = Mock()
        native.state.return_value = dict(alive=True, exit_code=None)
        native.identity.side_effect = lambda pid, handle: {11: child, 12: parent, 99: debugger}[pid]
        proc = Mock(pid=99, _handle=88, returncode=None)
        proc.poll.side_effect = lambda: proc.returncode
        def waited(timeout):
            proc.returncode = 0
            return 0
        proc.wait.side_effect = waited
        return case, cdb, admission, native, proc

    def test_fake_timeout_detaches_only_debugger_original_handle_survives(self):
        with tempfile.TemporaryDirectory() as d:
            case, cdb, admission, native, proc = self.fixture(d)
            output = Path(d) / 'watch'
            with patch.object(w, 'Native', return_value=native), \
                    patch.object(w, '_admitted', return_value=(case, admission, 11, 12)), \
                    patch.object(w.recover, 'lab', side_effect=lambda p: Path(p)), \
                    patch.object(w, 'verify_cdb', return_value=cdb), \
                    patch('tools.graphics_lab.load_hook.file_lease', side_effect=lambda p: nullcontext()), \
                    patch.object(w.subprocess, 'Popen', return_value=proc) as popen, \
                    patch.object(w.time, 'monotonic', side_effect=[0, 301]):
                result = w.watch(case, output)
            args, kwargs = popen.call_args
            self.assertEqual(args[0], w.arguments(cdb, 11))
            self.assertIs(kwargs['shell'], False)
            self.assertEqual(kwargs['cwd'], output)
            self.assertEqual(kwargs['creationflags'], 0x08000000)
            self.assertEqual(result['stop_reason'], 'timeout')
            self.assertTrue(result['target_after']['alive'])
            self.assertIs(result['game_terminated_by_watcher'], False)
            proc.stdin.write.assert_called_once_with(b'qd\n')
            proc.terminate.assert_not_called()
            native.close.assert_any_call(11); native.close.assert_any_call(12)
            self.assertTrue((output / 'watch-result.private.json').is_file())

    def test_existing_output_refuses_before_debugger_creation(self):
        with tempfile.TemporaryDirectory() as d:
            case, cdb, admission, native, _ = self.fixture(d)
            with patch.object(w, 'Native', return_value=native), \
                    patch.object(w, '_admitted', return_value=(case, admission, 11, 12)), \
                    patch.object(w.recover, 'lab', side_effect=lambda p: Path(p)), \
                    patch.object(w.subprocess, 'Popen') as popen:
                with self.assertRaises(ValueError):
                    w.watch(case, case)
            popen.assert_not_called()
            native.close.assert_any_call(11); native.close.assert_any_call(12)

    def test_output_limit_requests_detach_without_game_termination(self):
        with tempfile.TemporaryDirectory() as d:
            case, cdb, admission, native, proc = self.fixture(d)
            output = Path(d) / 'watch'
            with patch.object(w, 'Native', return_value=native), \
                    patch.object(w, '_admitted', return_value=(case, admission, 11, 12)), \
                    patch.object(w.recover, 'lab', side_effect=lambda p: Path(p)), \
                    patch.object(w, 'verify_cdb', return_value=cdb), \
                    patch('tools.graphics_lab.load_hook.file_lease', side_effect=lambda p: nullcontext()), \
                    patch.object(w.subprocess, 'Popen', return_value=proc), \
                    patch.object(w, 'usage', return_value=dict(dumps=4, bytes=100)):
                result = w.watch(case, output)
            self.assertEqual(result['stop_reason'], 'output_limit')
            self.assertTrue(result['target_after']['alive'])
            proc.terminate.assert_not_called()


class FinalTargetTests(unittest.TestCase):
    def test_natural_exit_waits_on_same_handle_and_never_reopens_pid(self):
        native = Mock(); native.state.return_value = dict(alive=False, exit_code=0)
        state, race = w.final_target(native, 77, identity(), debugger_exited_naturally=True)
        native.wait_exit.assert_called_once_with(77, 500)
        native.open.assert_not_called(); native.identity.assert_not_called()
        self.assertEqual(state, dict(alive=False, exit_code=0)); self.assertFalse(race)

    def test_query_access_denied_is_tolerated_only_after_retained_handle_exits(self):
        native = Mock()
        native.state.side_effect = [dict(alive=True, exit_code=None), dict(alive=False, exit_code=0)]
        native.identity.side_effect = OSError(5, 'Access denied while exiting')
        native.wait_exit.return_value = True
        state, race = w.final_target(native, 77, identity(), debugger_exited_naturally=False)
        self.assertTrue(race); self.assertFalse(state['alive']); self.assertEqual(state['exit_code'], 0)
        native.open.assert_not_called(); native.wait_exit.assert_called_once_with(77, 500)

    def test_query_error_in_still_running_original_is_not_downgraded(self):
        native = Mock(); native.state.return_value = dict(alive=True, exit_code=None)
        native.identity.side_effect = OSError(5, 'Access denied')
        native.wait_exit.return_value = False
        with self.assertRaises(OSError):
            w.final_target(native, 77, identity(), debugger_exited_naturally=False)
        native.open.assert_not_called()

    def test_reused_pid_or_identity_mismatch_never_becomes_accepted_exit_race(self):
        native = Mock(); native.state.return_value = dict(alive=True, exit_code=None)
        native.identity.return_value = identity(creation_time=790)
        with self.assertRaises(RuntimeError):
            w.final_target(native, 77, identity(), debugger_exited_naturally=False)
        native.wait_exit.assert_not_called(); native.open.assert_not_called()

    def test_ambiguous_wait_result_does_not_accept_alive_state(self):
        native = Mock(); native.state.return_value = dict(alive=True, exit_code=None)
        native.identity.side_effect = RuntimeError('Cannot query process')
        native.wait_exit.return_value = True
        with self.assertRaises(RuntimeError):
            w.final_target(native, 77, identity(), debugger_exited_naturally=False)



if __name__ == '__main__':
    unittest.main()
