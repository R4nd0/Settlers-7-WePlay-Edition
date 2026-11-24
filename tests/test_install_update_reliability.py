"""Ordinary update transfer and migration regressions with isolated fixtures."""
import errno
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


if __name__ == '__main__':
    unittest.main()
