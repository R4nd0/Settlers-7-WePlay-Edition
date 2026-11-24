"""Interrupted-run fixtures only; no real profiles, games or GPU calls."""
import hashlib
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

from tools.graphics_lab import recover as r


class RecoveryTests(unittest.TestCase):
    def setUp(self):
        temporary = tempfile.TemporaryDirectory()
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)
        self.game = self.root / 'work/game'
        self.case = self.root / 'work/case'
        self.case.mkdir(parents=True)
        self.bin = self.game / 'Base/Bin'
        self.bin.mkdir(parents=True)
        files = ('Settlers7R.exe', 'uplay_r1_loader.dll', 'd3d9.dll', 'Vision90.dll', 'vBase90.dll')
        for file in files:
            (self.bin / file).write_bytes(('synthetic ' + file).encode())
        digest = lambda path: hashlib.sha256(path.read_bytes()).hexdigest()
        self.critical = {p.relative_to(self.game).as_posix(): digest(p) for p in self.bin.iterdir()}
        self.options = self.game / 'LocalData/ProfileRoot/THE SETTLERS 7 - History Edition/Options.ini'
        self.options.parent.mkdir(parents=True)
        self.options.write_bytes(b'original private options')
        self.runtime = self.game / 'LocalData/s7-local-runtime.ini'
        self.config = self.bin / 'dxvk.conf'
        self.config.write_bytes(b'original DXVK config')
        self.targets = {'options': self.options, 'runtime': self.runtime, 'config': self.config}
        self.shared = self.root / 'shared-profile'
        self.shared.mkdir()
        self.save = self.shared / 's1.s7s'
        self.save.write_bytes(b'original shared save')
        self.private_save = self.options.parent / 's1.s7s'
        self.private_save.write_bytes(b'original private save')
        for target, value in ((('ROOT',), self.root), (('EXES',), {digest(self.bin / 'Settlers7R.exe')}),
                              (('DXVK',), digest(self.bin / 'd3d9.dll'))):
            p = patch.object(r, target[0], value)
            p.start()
            self.addCleanup(p.stop)
        self.no_games = patch.object(r, '_no_games')
        self.no_games.start()
        self.addCleanup(self.no_games.stop)

    def prepare(self):
        return r.prepare(self.game, self.case, critical=self.critical,
                         targets=self.targets, shared={'shared': self.shared})

    def mutate(self):
        self.options.write_bytes(b'diagnostic options')
        self.runtime.write_bytes(b'diagnostic runtime')
        self.config.write_bytes(b'diagnostic config')

    def test_recovers_interrupted_settings_and_preserves_current_diagnostics_and_newer_private_save(self):
        self.prepare()
        before = {name: r._current(path)[1] for name, path in self.targets.items()}
        self.mutate()
        self.private_save.write_bytes(b'newer private save must survive')
        result = r.recover(self.case)
        self.assertTrue(result['recovered'])
        self.assertFalse(result['saves_restored'])
        for name, path in self.targets.items():
            self.assertEqual(r._current(path)[1], before[name])
        self.assertEqual(self.private_save.read_bytes(), b'newer private save must survive')
        preserved = self.case / 'recovery-preserved/attempt-01'
        self.assertEqual((preserved / 'options.bin').read_bytes(), b'diagnostic options')
        self.assertEqual((preserved / 'runtime.bin').read_bytes(), b'diagnostic runtime')
        self.assertEqual((preserved / 'config.bin').read_bytes(), b'diagnostic config')

    def test_completed_recovery_is_idempotent_but_will_not_roll_back_a_later_run(self):
        self.prepare()
        self.mutate()
        r.recover(self.case)
        journal = (self.case / r.JOURNAL).read_bytes()
        self.assertTrue(r.recover(self.case)['already_restored'])
        self.assertEqual((self.case / r.JOURNAL).read_bytes(), journal)
        self.options.write_bytes(b'newer independent run options')
        with self.assertRaisesRegex(RuntimeError, 'newer private settings'):
            r.recover(self.case)
        self.assertEqual(self.options.read_bytes(), b'newer independent run options')

    def test_shared_save_change_refuses_every_restore_and_keeps_newer_save(self):
        self.prepare()
        self.mutate()
        self.save.write_bytes(b'newer real shared save')
        with self.assertRaisesRegex(RuntimeError, 'Shared profiles changed'):
            r.recover(self.case)
        self.assertEqual(self.options.read_bytes(), b'diagnostic options')
        self.assertEqual(self.save.read_bytes(), b'newer real shared save')

    def test_changed_binary_or_original_backup_refuses_restore(self):
        self.prepare()
        self.mutate()
        binary = self.bin / 'Vision90.dll'
        original = binary.read_bytes()
        binary.write_bytes(b'changed binary')
        with self.assertRaisesRegex(RuntimeError, 'binary changed'):
            r.recover(self.case)
        binary.write_bytes(original)
        (self.case / 'recovery-original/options.bin').write_bytes(b'changed backup')
        with self.assertRaisesRegex(RuntimeError, 'backup changed'):
            r.recover(self.case)
        self.assertEqual(self.options.read_bytes(), b'diagnostic options')

    def test_resumes_after_crash_between_private_restores(self):
        self.prepare()
        self.mutate()
        original = r._atomic
        restores = []
        def interrupted(path, raw, mtime_ns=None):
            if path in self.targets.values():
                restores.append(path)
                if len(restores) == 2:
                    raise OSError('synthetic power interruption')
            return original(path, raw, mtime_ns)
        with patch.object(r, '_atomic', interrupted), self.assertRaises(OSError):
            r.recover(self.case)
        self.assertEqual(json.loads((self.case / r.JOURNAL).read_text())['state'], 'restoring')
        self.assertTrue(r.recover(self.case)['recovered'])
        self.assertEqual(self.options.read_bytes(), b'original private options')
        self.assertFalse(self.runtime.exists())
        self.assertTrue((self.case / 'recovery-preserved/attempt-02/files.private.json').exists())

    def test_preservation_interruption_does_not_block_fresh_bounded_attempt(self):
        self.prepare()
        self.mutate()
        (self.case / 'recovery-preserved/attempt-01').mkdir(parents=True)
        self.assertTrue(r.recover(self.case)['recovered'])
        self.assertTrue((self.case / 'recovery-preserved/attempt-02/options.bin').exists())

    def test_running_game_and_missing_legacy_journal_refuse_recovery(self):
        with self.assertRaisesRegex(ValueError, 'older case'):
            r.recover(self.case)
        self.prepare()
        self.mutate()
        with patch.object(r, '_no_games', side_effect=RuntimeError('game running')):
            with self.assertRaisesRegex(RuntimeError, 'game running'):
                r.recover(self.case)
        self.assertEqual(self.options.read_bytes(), b'diagnostic options')

    def test_journal_cannot_redirect_a_target_to_a_save_or_an_external_path(self):
        self.prepare()
        journal = self.case / r.JOURNAL
        original = journal.read_bytes()
        for target in ('LocalData/ProfileRoot/THE SETTLERS 7 - History Edition/s1.s7s', '../outside.ini'):
            data = json.loads(original)
            data['targets']['options']['relative'] = target
            journal.write_text(json.dumps(data))
            with self.subTest(target=target), self.assertRaises(ValueError):
                r.recover(self.case)
        self.assertEqual(self.private_save.read_bytes(), b'original private save')

    def test_journal_replacement_flushes_file_before_atomic_replace(self):
        events = []
        original_fsync, original_replace = r.os.fsync, Path.replace
        def flushed(fd):
            events.append('fsync')
            return original_fsync(fd)
        def replaced(path, target):
            events.append('replace')
            return original_replace(path, target)
        with patch.object(r.os, 'fsync', flushed), patch.object(Path, 'replace', replaced):
            self.prepare()
        self.assertIn('replace', events)
        self.assertIn('fsync', events[:events.index('replace')])

    def test_malformed_target_record_refuses_before_any_private_change(self):
        self.prepare()
        self.mutate()
        journal = self.case / r.JOURNAL
        value = json.loads(journal.read_text())
        value['targets']['options'] = []
        journal.write_text(json.dumps(value))
        with self.assertRaisesRegex(ValueError, 'Invalid private-settings'):
            r.recover(self.case)
        self.assertEqual(self.options.read_bytes(), b'diagnostic options')


if __name__ == '__main__':
    unittest.main()
