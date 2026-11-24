"""Desktop preparation boundaries using disposable game and package fixtures."""
from pathlib import Path
import unittest
from types import SimpleNamespace
from unittest.mock import patch

import test_desktop_backend as desktop_fixtures
import desktop_backend as backend
import install


class DesktopSetupTransactionTests(unittest.TestCase):
    def setUp(self):
        self.fixture = desktop_fixtures.DesktopTests()
        self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.d = self.fixture.desktop
        self.fixture.prepare()
        self.old = self.d.game()

    def test_failed_preference_copy_reclaims_prepared_game_before_returning(self):
        def fail(_old, root):
            (root/'preserve.txt').write_bytes(b'unrelated data')
            raise OSError('preference write failure')
        with patch.object(self.d,'transfer_preferences',side_effect=fail):
            with self.assertRaisesRegex(OSError,'preference write failure'):
                self.fixture.prepare()
        abandoned, = [root for root in self.old.parent.iterdir() if root!=self.old]
        self.assertEqual((abandoned/'preserve.txt').read_bytes(),b'unrelated data')
        self.assertFalse((abandoned/'Base/BBA/map.bba').exists())
        self.assertEqual(self.d.game(),self.old)
        self.assertEqual(list((self.d.state/'Preparations').glob('*.json')),[])

    def test_published_installation_survives_temporarily_busy_journal_retirement(self):
        journal = self.d.state/'installation-transaction.json'
        unlink = Path.unlink
        def busy(path,*args,**kwargs):
            if path == journal:
                raise PermissionError('journal in use by another reader')
            return unlink(path,*args,**kwargs)
        with patch.object(Path,'unlink',busy):
            result = self.fixture.prepare()
        self.assertTrue(result['installed'])
        current = self.d.game()
        self.assertNotEqual(current,self.old)
        self.assertTrue(journal.exists())
        self.d.recover_publication()
        self.assertFalse(journal.exists())
        self.assertEqual(self.d.game(),current)

    def test_interrupted_allocation_with_only_owned_empty_receipts_is_reclaimed(self):
        instance = 'e'*32
        root = self.old.parent/instance
        root.mkdir()
        self.d.write(root/'desktop-owner.json',dict(format=backend.FORMAT,owner=self.d.owner,instance=instance))
        self.d.write(root/'desktop-files.json',dict(format=backend.FORMAT,owner=self.d.owner,files={}))
        pending = self.d.state/'Preparations'/(instance+'.json')
        self.d.write(pending,dict(format='weplay-preparation-v1',owner=self.d.owner,
            instance=instance,container=str(root.parent),allocated=False))
        self.d.cleanup_preparations()
        self.assertFalse(root.exists())
        self.assertFalse(pending.exists())
        self.assertEqual(self.d.game(),self.old)

    def test_oversized_old_preferences_do_not_block_save_preserving_repair(self):
        old_settings = self.old/'LocalData/graphics-choice.json'
        old_settings.write_bytes(b' '*65537)
        saves = self.old/'LocalData/saves';saves.mkdir()
        (saves/'progress.s7s').write_bytes(b'original playthrough')
        result = self.fixture.prepare()
        self.assertTrue(result['installed'])
        self.assertEqual((self.d.game()/'LocalData/saves/progress.s7s').read_bytes(),b'original playthrough')
        self.assertEqual(old_settings.read_bytes(),b' '*65537)
        self.assertIn(('warning',{'code':'preferences_reset'}),self.fixture.events)

    def test_retirement_preserves_empty_personal_profile_directories(self):
        personal = self.old/'LocalData/Personal archive'
        personal.mkdir()
        self.fixture.prepare()
        self.assertTrue(personal.is_dir())
        self.assertFalse((self.old/'Base/BBA/map.bba').exists())

    def test_explicit_other_prepared_source_keeps_that_copys_saves(self):
        active_saves = self.old/'LocalData/saves';active_saves.mkdir()
        (active_saves/'checkpoint.s7s').write_bytes(b'currently installed unrelated checkpoint')
        other = self.fixture.fixture.root/'prepared elsewhere'
        install.prepare(self.d.package,self.fixture.fixture.source,other,emit=lambda _: None,publish_starters=False)
        chosen_saves = other/'LocalData/saves';chosen_saves.mkdir(parents=True)
        (chosen_saves/'checkpoint.s7s').write_bytes(b'user-selected checkpoint')
        with patch.object(backend.subprocess,'run',return_value=SimpleNamespace(returncode=0,stdout=b'[]')):
            self.d.install_game(other)
        self.assertEqual((self.d.game()/'LocalData/saves/checkpoint.s7s').read_bytes(),b'user-selected checkpoint')
        self.assertEqual((chosen_saves/'checkpoint.s7s').read_bytes(),b'user-selected checkpoint')
        self.assertEqual((active_saves/'checkpoint.s7s').read_bytes(),b'currently installed unrelated checkpoint')


if __name__ == '__main__':
    unittest.main()
