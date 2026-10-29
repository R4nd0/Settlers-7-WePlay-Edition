from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(Path(__file__).parent)]
import desktop_backend as backend
from internet_room import Room, InternetError
import test_desktop_backend as fixtures
from internet_private_channel import PrivateChannel

ROOM=Room('a'*32,'b'*64,'10.203.77.0/24')


class InternetBackendTests(unittest.TestCase):
    def setUp(self):
        self.fixture=fixtures.DesktopTests();self.fixture.setUp();self.addCleanup(self.fixture.doCleanups)
        self.desktop=self.fixture.desktop

    def test_join_secret_only_travels_over_private_channel(self):
        channel=Mock();channel.read.return_value=ROOM.token()
        with patch.object(backend.install,'package_manifest',return_value={'internet_easytier':True}), \
                patch('internet_private_channel.PrivateChannel',return_value=Mock(__enter__=Mock(return_value=channel),__exit__=Mock(return_value=False))), \
                patch.object(self.desktop,'play',return_value={'exit_code':0}) as play:
            self.desktop.internet_play({'player_name':'Anna','channel':'weplay-internet-'+'c'*32},host=False)
        self.assertEqual(play.call_args.kwargs['internet']['invitation'],ROOM.token())
        self.assertNotIn(ROOM.secret,repr(self.fixture.events));self.assertNotIn(ROOM.token(),repr(self.fixture.events))

    def test_public_request_cannot_contain_invitation_or_unknown_fields(self):
        with patch.object(backend.install,'package_manifest',return_value={'internet_easytier':True}):
            with self.assertRaisesRegex(ValueError,'request_invalid'):
                self.desktop.internet_play({'player_name':'Anna','channel':'weplay-internet-'+'c'*32,'invitation':ROOM.token()},host=False)

    def test_create_requires_empty_private_request_and_explicit_mode(self):
        channel=Mock();channel.read.return_value=ROOM.token()
        data=dict(player_name='Anna',channel='weplay-internet-'+'c'*32,mode='assisted',direct_endpoint=None)
        with patch.object(backend.install,'package_manifest',return_value={'internet_easytier':True}), \
                patch('internet_private_channel.PrivateChannel',return_value=Mock(__enter__=Mock(return_value=channel),__exit__=Mock(return_value=False))), \
                patch.object(self.desktop,'play') as play:
            with self.assertRaises(InternetError):self.desktop.internet_play(data,host=True)
        play.assert_not_called()

    def test_old_packages_do_not_silently_gain_internet_capability(self):
        with patch.object(self.desktop,'play') as play:
            with self.assertRaisesRegex(ValueError,'package_invalid'):
                self.desktop.internet_play(dict(player_name='Anna',channel='weplay-internet-'+'c'*32),host=False)
        play.assert_not_called()

    def test_private_channel_name_never_opens_arbitrary_files(self):
        for name in ('C:/private.txt',r'\\.\pipe\someone-else','weplay-internet-'+'c'*31,'weplay-internet-'+'c'*32+'\\x'):
            with self.assertRaises(InternetError):PrivateChannel(name)


if __name__=='__main__':unittest.main()
