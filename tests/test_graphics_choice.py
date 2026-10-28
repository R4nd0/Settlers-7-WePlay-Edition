import json
import os
from pathlib import Path
import subprocess
import sys
from types import SimpleNamespace
import unittest
from unittest.mock import patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/lan'), str(Path(__file__).parent)]
import graphics_choice as choice
import graphics_profile as graphics
import compatibility as content
import test_graphics_profile as fixtures


class GraphicsChoiceTests(unittest.TestCase):
    setUp = fixtures.GraphicsProfileTests.setUp
    snapshot = fixtures.GraphicsProfileTests.snapshot
    install = fixtures.GraphicsProfileTests.install
    state_dir = fixtures.GraphicsProfileTests.state_dir

    def result(self, model=None, code=0, raw=None):
        return SimpleNamespace(returncode=code, stdout=raw if raw is not None else graphics.encoded(
            {'format': choice.FORMAT, 'choice': model}), stderr=b'')

    def choose(self, result, inherited=None):
        with patch.object(subprocess, 'run', return_value=result) as runner:
            environment = choice.choose_launch_environment(self.root, inherited)
        return environment, runner

    def saved(self):
        return json.loads((self.root / choice.CHOICE_FILE).read_bytes())

    def test_native_auto_has_hidden_gui_every_start_and_no_fake_gpu_filter(self):
        before = self.snapshot()
        inherited = {'PATH': 'fixture', 'dxvk_filter_device_name': 'old GPU'}
        environment, runner = self.choose(self.result(), inherited)
        self.assertEqual(environment, {'PATH': 'fixture'})
        self.assertEqual(inherited['dxvk_filter_device_name'], 'old GPU')
        self.assertEqual(self.snapshot(), before)
        self.assertEqual(self.saved(), {'format': choice.FORMAT, 'choice': None})
        command = runner.call_args.args[0]
        self.assertIn('-STA', command)
        self.assertIn('-NonInteractive', command)
        self.assertEqual(command[command.index('-WindowStyle') + 1], 'Hidden')
        self.assertEqual(command[-2:], ['-RendererMode', 'native'])
        self.assertEqual(runner.call_args.kwargs['creationflags'], subprocess.CREATE_NO_WINDOW)
        self.assertNotIn('shell', runner.call_args.kwargs)
        self.assertEqual(json.loads(runner.call_args.kwargs['input']), self.saved())
        _, second = self.choose(self.result(), inherited)
        second.assert_called_once()

    def test_dxvk_selection_reuses_profile_interface_and_keeps_content_id(self):
        self.install()
        before = content.verify(self.root)
        old_environment = dict(os.environ)
        with patch.object(content, 'inventory', side_effect=AssertionError('startup full inventory')):
            environment, runner = self.choose(self.result('Fixture GPU'))
        self.assertEqual(environment['DXVK_FILTER_DEVICE_NAME'], 'Fixture GPU')
        self.assertEqual(Path(environment['DXVK_CONFIG_FILE']), self.root / graphics.CONF)
        self.assertEqual(self.saved()['choice'], 'Fixture GPU')
        self.assertEqual(runner.call_args.args[0][-1], 'dxvk')
        self.assertEqual(content.verify(self.root), before)
        self.assertEqual(dict(os.environ), old_environment)
        environment, runner = self.choose(self.result())
        self.assertNotIn('DXVK_FILTER_DEVICE_NAME', environment)
        self.assertEqual(json.loads(runner.call_args.kwargs['input'])['choice'], 'Fixture GPU')

    def test_cancellation_writes_nothing_and_returns_no_launch_environment(self):
        before = self.snapshot()
        environment, _ = self.choose(self.result(code=choice.CANCELLED, raw=b'ignored'))
        self.assertIsNone(environment)
        self.assertEqual(self.snapshot(), before)
        self.assertFalse((self.root / 'LocalData').exists())
        self.install()
        self.choose(self.result('Fixture GPU'))
        record = (self.root / choice.CHOICE_FILE).read_bytes()
        environment, _ = self.choose(self.result(code=choice.CANCELLED))
        self.assertIsNone(environment)
        self.assertEqual((self.root / choice.CHOICE_FILE).read_bytes(), record)

    def test_native_renderer_rejects_concrete_choice_from_child(self):
        with self.assertRaises(ValueError): self.choose(self.result('Fixture GPU'))
        self.assertFalse((self.root / choice.CHOICE_FILE).exists())

    def test_previous_model_passed_privately_and_stale_auto_saved(self):
        self.install()
        self.choose(self.result('Old graphics model'))
        environment, runner = self.choose(self.result())
        self.assertNotIn('DXVK_FILTER_DEVICE_NAME', environment)
        self.assertIsNone(self.saved()['choice'])
        self.assertEqual(json.loads(runner.call_args.kwargs['input'])['choice'], 'Old graphics model')
        self.assertFalse(any('Old graphics model' in argument for argument in runner.call_args.args[0]))

    def test_record_schema_strings_and_unicode_bounds(self):
        for name in (None, 'Intel Fixture', 'AMD Fixture', 'NVIDIA Fixture', 'Other Vendor', 'GPU é'):
            self.assertEqual(choice.validate_choice({'format': choice.FORMAT, 'choice': name})['choice'], name)
        invalid = [True, 7, [], {}, '', '  ', ' GPU ', 'bad\0', 'bad\n', '\u200fGPU',
                   '\ud800', 'a' * 257, 'é' * 129]
        for name in invalid:
            with self.subTest(value=repr(name)):
                with self.assertRaises(ValueError): choice.validate_choice({'format': choice.FORMAT, 'choice': name})
        for value in ({}, {'format': choice.FORMAT, 'choice': None, 'device_id': 'forbidden'},
                      {'format': 'other', 'choice': None}, []):
            with self.assertRaises(ValueError): choice.validate_choice(value)

    def test_malformed_failed_or_excessive_child_output_is_not_persisted(self):
        bad = [b'not json', b'\xff', b'{}', b'[]', b'x' * 4097,
               b'{"format":"s7-graphics-choice-v1","choice":null,"choice":"GPU"}',
               graphics.encoded({'format': choice.FORMAT, 'choice': None, 'serial': 'forbidden'})]
        for raw in bad:
            with self.assertRaises(ValueError): self.choose(self.result(raw=raw))
            self.assertFalse((self.root / choice.CHOICE_FILE).exists())
        with self.assertRaises(ValueError): self.choose(self.result(code=2))

    def test_damaged_preference_is_preserved_and_no_dialog_started(self):
        path = self.root / choice.CHOICE_FILE
        path.parent.mkdir()
        path.write_bytes(b'foreign file')
        with patch.object(subprocess, 'run') as runner:
            with self.assertRaises(ValueError): choice.choose_launch_environment(self.root, {})
        runner.assert_not_called()
        self.assertEqual(path.read_bytes(), b'foreign file')

    def test_unknown_renderer_never_runs_selection(self):
        (self.root / graphics.DLL).write_bytes(b'foreign d3d9')
        with patch.object(subprocess, 'run') as runner:
            with self.assertRaises(ValueError): choice.choose_launch_environment(self.root, {})
        runner.assert_not_called()

    def test_inactive_owned_profile_is_honest_native_mode(self):
        self.install()
        graphics.restore(self.root)
        environment, runner = self.choose(self.result(), {})
        self.assertEqual(environment, {})
        self.assertEqual(runner.call_args.args[0][-1], 'native')

    def test_pending_profile_journal_rejects_dialog(self):
        self.install()
        (self.state_dir() / 'transaction.json').write_bytes(graphics.encoded(
            {'format': graphics.PROFILE, 'from': False, 'to': True}))
        with patch.object(subprocess, 'run') as runner:
            with self.assertRaises(ValueError): choice.choose_launch_environment(self.root, {})
        runner.assert_not_called()

    def test_choice_file_changed_while_dialog_open_is_preserved(self):
        self.choose(self.result())
        altered = graphics.encoded({'format': choice.FORMAT, 'choice': 'New external choice'})
        def dialog(*args, **kwargs):
            (self.root / choice.CHOICE_FILE).write_bytes(altered)
            return self.result()
        with patch.object(subprocess, 'run', side_effect=dialog):
            with self.assertRaises(ValueError): choice.choose_launch_environment(self.root, {})
        self.assertEqual((self.root / choice.CHOICE_FILE).read_bytes(), altered)

    def test_renderer_changed_during_dialog_aborts_without_saving(self):
        def dialog(*args, **kwargs):
            (self.root / graphics.DLL).write_bytes(b'unexpected renderer')
            return self.result()
        with patch.object(subprocess, 'run', side_effect=dialog):
            with self.assertRaises(ValueError): choice.choose_launch_environment(self.root, {})
        self.assertFalse((self.root / choice.CHOICE_FILE).exists())
        self.assertEqual((self.root / graphics.DLL).read_bytes(), b'unexpected renderer')

    def test_existing_session_lock_can_be_owned_by_caller(self):
        with graphics.session_lock(self.root):
            environment, _ = self.choose(self.result(), {})
        self.assertEqual(environment, {})

    def test_atomic_save_failure_preserves_previous_choice(self):
        self.install()
        self.choose(self.result('Old model'))
        before = (self.root / choice.CHOICE_FILE).read_bytes()
        with patch.object(graphics, 'atomic_write', side_effect=OSError('fixture disk failure')):
            with self.assertRaises(OSError): self.choose(self.result('New model'))
        self.assertEqual((self.root / choice.CHOICE_FILE).read_bytes(), before)

    def test_actual_linked_preferences_are_rejected(self):
        outside = self.parent / 'external.json'
        outside.write_bytes(graphics.encoded({'format': choice.FORMAT, 'choice': None}))
        path = self.root / choice.CHOICE_FILE
        path.parent.mkdir()
        try:
            path.symlink_to(outside)
        except OSError as exc:
            self.skipTest('Host disallows fixture symlink: ' + str(exc.winerror))
        with patch.object(subprocess, 'run') as runner:
            with self.assertRaises(ValueError): choice.choose_launch_environment(self.root, {})
        runner.assert_not_called()
        self.assertEqual(json.loads(outside.read_bytes())['choice'], None)
        path.unlink()

    def test_simple_menu_defaults_legacy_toggle_and_fixed_arguments(self):
        self.assertEqual(choice.game_arguments(self.root), ())
        self.assertFalse((self.root / 'LocalData').exists())
        self.choose(self.result())  # Legacy v1 record, no new field.
        self.assertEqual(choice.game_arguments(self.root), ())
        before = content.verify(self.root)
        for enabled, expected in [(True, ('-vSynch', '-noMapInMainMenu')), (False, ())]:
            raw = graphics.encoded({'format': choice.FORMAT, 'choice': None, 'simple_menu': enabled})
            self.choose(self.result(raw=raw))
            self.assertEqual(choice.game_arguments(self.root), expected)
            self.assertIs(self.saved()['simple_menu'], enabled)
            self.assertEqual(content.verify(self.root), before)

    def test_simple_menu_rejects_coerced_bools_and_extra_argv(self):
        for value in (None, 0, 1, 'true', 'false', [], {}, '-developerMode'):
            with self.subTest(value=value):
                raw = graphics.encoded({'format': choice.FORMAT, 'choice': None, 'simple_menu': value})
                with self.assertRaises(ValueError): self.choose(self.result(raw=raw))
                self.assertFalse((self.root / choice.CHOICE_FILE).exists())
        with self.assertRaises(ValueError):
            choice.validate_choice({'format': choice.FORMAT, 'choice': None,
                                    'simple_menu': True, 'arguments': ['-developerMode']})

    def test_saved_bad_menu_bool_is_not_ignored_and_environment_cannot_enable_it(self):
        with patch.dict(os.environ, {'S7_SIMPLE_MENU': 'true', 'S7_GAME_ARGS': '-noMapInMainMenu'}):
            self.assertEqual(choice.game_arguments(self.root), ())
        path = self.root / choice.CHOICE_FILE; path.parent.mkdir()
        raw = graphics.encoded({'format': choice.FORMAT, 'choice': None, 'simple_menu': 1})
        path.write_bytes(raw)
        with patch.object(subprocess, 'run') as runner:
            with self.assertRaises(ValueError): choice.choose_launch_environment(self.root, {})
            with self.assertRaises(ValueError): choice.game_arguments(self.root)
        runner.assert_not_called()
        self.assertEqual(path.read_bytes(), raw)

    def test_cancel_preserves_previously_selected_simple_menu(self):
        raw = graphics.encoded({'format': choice.FORMAT, 'choice': None, 'simple_menu': True})
        self.choose(self.result(raw=raw))
        previous = (self.root / choice.CHOICE_FILE).read_bytes()
        environment, _ = self.choose(self.result(code=choice.CANCELLED))
        self.assertIsNone(environment)
        self.assertEqual((self.root / choice.CHOICE_FILE).read_bytes(), previous)


class PowerShellChoiceFixtureTests(unittest.TestCase):
    def test_actual_pure_functions_without_ui_or_cim(self):
        # Parse the production script, load ONLY its pure function definitions,
        # and invoke them with literal fixtures. No top-level code, GUI or CIM.
        script = ROOT / 'tools/installer/Choose-Graphics.ps1'
        command = r'''
$ErrorActionPreference = 'Stop'
$tokens = $null; $parseErrors = $null
$ast = [Management.Automation.Language.Parser]::ParseFile($args[0], [ref]$tokens, [ref]$parseErrors)
if ($parseErrors.Count) { throw 'PowerShell syntax failure.' }
$functions = $ast.FindAll({param($node) $node -is [Management.Automation.Language.FunctionDefinitionAst]}, $true)
foreach ($function in $functions) { . ([scriptblock]::Create($function.Extent.Text)) }
$models = @(Normalize-ModelNames @(' NVIDIA Fixture ', 'Intel Fixture', 'AMD Fixture', 'nvidia fixture', 'Other Vendor', "bad`nname", 13, '', ('x' * 257)))
$invalidMenuRejected = $false
try { [void](ConvertTo-ChoiceJson $null 'false') } catch { $invalidMenuRejected = $true }
$output = @{
    models = $models
    selected = Resolve-PreferredChoice 'nvidia fixture' $models $true
    stale = Resolve-PreferredChoice 'Removed GPU' $models $true
    native = Resolve-PreferredChoice 'AMD Fixture' $models $false
    automatic = ConvertTo-ChoiceJson $null
    record = ConvertTo-ChoiceJson 'Other Vendor'
    simpleMenu = ConvertTo-ChoiceJson $null $true
    invalidMenuRejected = $invalidMenuRejected
    invalidControl = Test-ModelName "bad`tname"
    tooManyBytes = Test-ModelName (([char]0x00e9).ToString() * 129)
}
$output | ConvertTo-Json -Compress
'''
        # Use a literal path as an argument, not interpolated executable code.
        invocation = '& { ' + command + ' } ' + "'" + str(script).replace("'", "''") + "'"
        result = subprocess.run(['powershell.exe', '-NoLogo', '-NoProfile', '-NonInteractive',
                                 '-WindowStyle', 'Hidden', '-Command', invocation],
                                capture_output=True, timeout=30, creationflags=subprocess.CREATE_NO_WINDOW)
        self.assertEqual(result.returncode, 0, result.stderr.decode(errors='replace'))
        value = json.loads(result.stdout.decode('utf-8-sig'))
        self.assertEqual(value['models'], ['AMD Fixture', 'Intel Fixture', 'NVIDIA Fixture', 'Other Vendor'])
        self.assertEqual(value['selected'], 'NVIDIA Fixture')
        self.assertIsNone(value['stale'])
        self.assertIsNone(value['native'])
        self.assertEqual(json.loads(value['automatic']), {'format': choice.FORMAT, 'choice': None, 'simple_menu': False})
        self.assertEqual(json.loads(value['record'])['choice'], 'Other Vendor')
        self.assertIs(json.loads(value['simpleMenu'])['simple_menu'], True)
        self.assertTrue(value['invalidMenuRejected'])
        self.assertFalse(value['invalidControl'])
        self.assertFalse(value['tooManyBytes'])


if __name__ == '__main__':
    unittest.main()
