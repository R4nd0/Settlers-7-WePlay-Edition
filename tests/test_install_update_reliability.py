"""Ordinary update transfer and migration regressions with isolated fixtures."""
import errno
import importlib.util
import io
import os
from pathlib import Path
import sys
import tempfile
import unittest
from unittest.mock import Mock, patch

ROOT = Path(__file__).resolve().parents[1]
sys.path[:0] = [str(ROOT / 'tools/installer'), str(ROOT / 'tools/linux')]
import linux_profiles
import game_detection
import update_client as updater


class UpdateTransferTests(unittest.TestCase):
    def transfer_fixture(self, body, length=None, *, status=200):
        response = Mock(status=status)
        response.getheader.side_effect = lambda name: {
            'Content-Length': length,
            'Location': 'https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases',
        }.get(name)
        response.read1.side_effect = io.BytesIO(body).read
        connection = Mock(sock=None)
        connection.getresponse.return_value = response
        return connection, response

    def test_abbreviated_response_with_content_length_is_rejected(self):
        connection, response = self.transfer_fixture(b'[]', '10')
        with self.assertRaisesRegex(updater.UpdateError, '^update_download_failed$'):
            updater.transfer(updater.RELEASES, 50, connection_factory=Mock(return_value=connection))

    def test_redirect_closes_its_response_before_opening_next_connection(self):
        redirect, redirect_response = self.transfer_fixture(b'', status=302)
        receiver, receiver_response = self.transfer_fixture(b'[]', '2')
        factory = Mock(side_effect=[redirect, receiver])
        self.assertEqual(updater.transfer(updater.RELEASES, 50, connection_factory=factory), b'[]')
        redirect_response.close.assert_called_once()
        receiver_response.close.assert_called_once()
        redirect.close.assert_called_once()
        receiver.close.assert_called_once()

    def test_volume_filling_during_transfer_reports_disk_full(self):
        connection, response = self.transfer_fixture(b'payload', '7')
        output = Mock()
        output.write.side_effect = OSError(errno.ENOSPC, 'disk full')
        with patch.object(Path, 'open', return_value=output), \
                self.assertRaisesRegex(updater.UpdateError, '^update_disk_full$'):
            updater.transfer(updater.RELEASES, 50, destination=ROOT/'work/download-fixture',
                             connection_factory=Mock(return_value=connection))
        output.close.assert_called_once()


class SetupOwnershipTests(unittest.TestCase):
    def setUp(self):
        scratch = ROOT/'work'
        temporary = tempfile.TemporaryDirectory(prefix='setup-audit-', dir=scratch)
        self.addCleanup(temporary.cleanup)
        self.root = Path(temporary.name)

    def test_wrong_launcher_parent_cannot_trigger_cached_job_retirement(self):
        app = self.root/'app'
        app.mkdir()
        (app/'desktop-release.json').write_bytes(updater.canonical(dict(
            version='2.0.0-rc4.10', update_sequence=100, update_channel='preview')))
        with patch.object(updater, 'reclaim_jobs') as reclaim, \
                self.assertRaisesRegex(updater.UpdateError, '^update_parent_invalid$'):
            updater.prepare(app, os.getpid(), 1)
        reclaim.assert_not_called()

    def test_migration_ignores_unrelated_file_named_like_game_folder(self):
        source, destination = self.root/'source', self.root/'destination'
        documents = source/'LocalData/WineHome/Documents'
        documents.mkdir(parents=True)
        (documents/'WePlay').write_text('unrelated personal file')
        destination.mkdir()
        self.assertEqual(linux_profiles.migrate(source,destination),0)
        self.assertFalse((destination/'LocalData/WineHome/Documents/WePlay').exists())

    def test_migration_deduplicates_identical_profiles_from_wine_and_proton(self):
        source, destination = self.root/'source', self.root/'destination'
        for prefix, save in (('WinePrefix','first.s7local'),('ProtonData/pfx','second.s7local')):
            game = source/prefix/'drive_c/users/weplay/Documents/Settlers7'
            game.mkdir(parents=True)
            (game/'Options.ini').write_bytes(b'fixture-shared-settings')
            (game/save).write_bytes(save.encode('ascii'))
        destination.mkdir()
        self.assertEqual(linux_profiles.migrate(source,destination),3)
        installed = destination/'LocalData/WineHome/Documents/Settlers7'
        self.assertEqual((installed/'Options.ini').read_bytes(),b'fixture-shared-settings')
        self.assertTrue((installed/'first.s7local').is_file())
        self.assertTrue((installed/'second.s7local').is_file())

    @unittest.skipUnless(sys.platform=='linux','requires real Wine profile symlinks')
    def test_foreign_wine_documents_are_refused_before_any_profile_is_copied(self):
        source, destination = self.root/'source', self.root/'destination'
        profile = source/'LocalData'
        profile.mkdir(parents=True)
        (profile/'local-profile-id.txt').write_text('fixture original')
        foreign = self.root/'foreign'
        foreign.mkdir()
        user = source/'WinePrefix/drive_c/users/weplay'
        user.mkdir(parents=True)
        (user/'Documents').symlink_to(foreign,target_is_directory=True)
        destination.mkdir()
        with self.assertRaisesRegex(ValueError,'outside the owned profile'):
            linux_profiles.migrate(source,destination)
        self.assertFalse((destination/'LocalData/local-profile-id.txt').exists())
        self.assertEqual((profile/'local-profile-id.txt').read_text(),'fixture original')

    @unittest.skipUnless(os.name=='nt','Windows registry discovery')
    def test_setup_uses_expanded_quoted_registry_install_location(self):
        import winreg
        base = self.root/'Game Directory'
        location = '"%S7_TEST_DETECTION_ROOT%\\Game Directory"'
        with patch.dict(os.environ,{'S7_TEST_DETECTION_ROOT':str(self.root)}), \
                patch.object(winreg,'OpenKey'), patch.object(winreg,'QueryInfoKey',return_value=(1,0,0)), \
                patch.object(winreg,'EnumKey',return_value='fixture'), \
                patch.object(winreg,'QueryValueEx',side_effect=lambda _key,name:
                             ('Settlers 7 History Edition',winreg.REG_SZ) if name=='DisplayName'
                             else (location,winreg.REG_EXPAND_SZ)):
            self.assertEqual(set(game_detection.registry_locations()),{base})

    @unittest.skipUnless(os.name=='nt','Windows registry discovery')
    def test_duplicate_installation_entries_do_not_crowd_out_unique_game(self):
        old=self.root/'Old';new=self.root/'Actual Game'
        with patch.object(game_detection,'registry_locations',return_value=[old]*256+[new]), \
                patch.object(Path,'is_dir',return_value=False):
            self.assertEqual(game_detection.candidates(),[old,new])

    def test_retirement_resolves_manifest_spelling_on_case_sensitive_storage(self):
        source = ROOT/'tools/desktop/check_package.py'
        spec = importlib.util.spec_from_file_location('package_retire_fixture',source)
        check = importlib.util.module_from_spec(spec)
        spec.loader.exec_module(check)
        with tempfile.TemporaryDirectory(dir=os.environ.get('S7_TEST_TEMP_ROOT')) as folder:
            app=Path(folder)/'App';backup=app/'State/PackageBackups'/('.weplay-package-'+'d'*32)
            notice=backup/'Runtime/Notices/GPL-3.0.txt'
            notice.parent.mkdir(parents=True)
            if (backup/'runtime').exists():
                self.skipTest('requires case-sensitive package storage')
            notice.write_bytes(b'fixture licence')
            (backup/'package.json').write_bytes(updater.canonical(dict(format='s7-patch-package-v1',
                files={'runtime/notices/gpl-3.0.txt':check.digest(notice)})))
            result=check.retire(app,backup)
            self.assertEqual(result['removed'],1)
            self.assertFalse(result['remaining'])


if __name__ == '__main__':
    unittest.main()
