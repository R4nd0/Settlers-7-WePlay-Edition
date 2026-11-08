from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch
from contextlib import nullcontext
from types import SimpleNamespace

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(Path(__file__).parent)]
import desktop_backend as backend
from internet_room import Room, InternetError
import test_desktop_backend as fixtures
import lan_control
import launch_lan
from internet_private_channel import PrivateChannel

ROOM=Room('a'*32,'b'*64,'10.203.77.0/24')


class InternetAlignmentTests(unittest.TestCase):
    def desktop(self):
        desktop=object.__new__(backend.Desktop)
        for name in ('progress','probe','renderer','default_choice','record_ownership','network_id'):
            setattr(desktop,name,Mock())
        desktop.alternatives=Mock(return_value={'native':'a'*64,'dxvk':'b'*64})
        desktop.network_id.return_value='b'*64
        return desktop

    def test_alignment_uses_public_status_before_admission_not_protected_configuration(self):
        desktop=self.desktop();interface=SimpleNamespace(address='10.203.77.2')
        with patch.object(lan_control,'request',return_value={'format':'s7-lobby-control-v1','content_id':'b'*64}) as status, \
                patch.object(launch_lan,'remote_configuration',side_effect=ValueError('HTTP 403 before admission')) as protected, \
                patch.object(backend.time,'monotonic',side_effect=[0,46]), \
                patch.object(backend.graphics,'session_lock',return_value=nullcontext()), \
                patch.object(backend.choice,'_load_choice'),patch.object(backend.choice,'save_choice'):
            desktop.align_internet(Path('fixture-game'),interface,ROOM.coordinator,False)
        status.assert_called_once_with(ROOM.coordinator,interface.address,'status')
        protected.assert_not_called()
        desktop.renderer.assert_not_called();desktop.probe.assert_not_called()
        desktop.record_ownership.assert_not_called()

    def test_matching_room_never_forces_an_unsupported_host_renderer(self):
        desktop=self.desktop()
        desktop.probe.return_value={'devices':[],'failure':'unsupported'}
        desktop.renderer.side_effect=ValueError('renderer_unsupported')
        with patch.object(lan_control,'request',return_value={'content_id':'b'*64}), \
                patch.object(backend.choice,'save_choice') as save:
            desktop.align_internet(Path('fixture-game'),SimpleNamespace(address='10.203.77.2'),ROOM.coordinator,False)
        desktop.renderer.assert_not_called();save.assert_not_called()

    def test_unknown_public_content_never_changes_renderer(self):
        desktop=self.desktop()
        with patch.object(lan_control,'request',return_value={'format':'s7-lobby-control-v1','content_id':'c'*64}), \
                patch.object(launch_lan,'remote_configuration',side_effect=AssertionError('protected request before admission')):
            with self.assertRaisesRegex(ValueError,'version_mismatch'):
                desktop.align_internet(Path('fixture-game'),SimpleNamespace(address='10.203.77.2'),ROOM.coordinator,False)
        desktop.renderer.assert_not_called();desktop.record_ownership.assert_not_called()

    def test_host_alignment_does_not_request_its_own_status(self):
        desktop=self.desktop()
        with patch.object(lan_control,'request') as status,patch.object(launch_lan,'remote_configuration') as protected:
            desktop.align_internet(Path('fixture-game'),SimpleNamespace(address=ROOM.coordinator),ROOM.coordinator,True)
        status.assert_not_called();protected.assert_not_called()


class InternetBackendTests(unittest.TestCase):
    def setUp(self):
        self.fixture=fixtures.DesktopTests();self.fixture.setUp();self.addCleanup(self.fixture.doCleanups)
        self.desktop=self.fixture.desktop

    def test_join_secret_only_travels_over_private_channel(self):
        channel=Mock();channel.read.return_value=ROOM.token()
        with patch.object(backend.install,'package_manifest',return_value={'internet_easytier':True}), \
                patch('internet_private_channel.PrivateChannel',return_value=Mock(__enter__=Mock(return_value=channel),__exit__=Mock(return_value=False))), \
                patch.object(self.desktop,'play',return_value={'exit_code':0}) as play:
            self.desktop.internet_play({'player_name':'Anna','channel':'weplay-internet-'+'c'*32,'internet_consent':1},host=False)
        self.assertEqual(play.call_args.kwargs['internet']['invitation'],ROOM.token())
        self.assertNotIn(ROOM.secret,repr(self.fixture.events));self.assertNotIn(ROOM.token(),repr(self.fixture.events))

    def test_public_request_cannot_contain_invitation_or_unknown_fields(self):
        with patch.object(backend.install,'package_manifest',return_value={'internet_easytier':True}):
            with self.assertRaisesRegex(ValueError,'request_invalid'):
                self.desktop.internet_play({'player_name':'Anna','channel':'weplay-internet-'+'c'*32,'invitation':ROOM.token()},host=False)

    def test_create_requires_empty_private_request_and_explicit_mode(self):
        channel=Mock();channel.read.return_value=ROOM.token()
        data=dict(player_name='Anna',channel='weplay-internet-'+'c'*32,mode='assisted',direct_endpoint=None,internet_consent=1)
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

    def test_unchecked_consent_cannot_reach_private_channel_or_play(self):
        with patch.object(backend.install,'package_manifest',return_value={'internet_easytier':True}), \
                patch('internet_private_channel.PrivateChannel') as channel,patch.object(self.desktop,'play') as play:
            for value in (None,False,True,0,'1'):
                with self.assertRaisesRegex(ValueError,'internet_consent_required'):
                    self.desktop.internet_play(dict(player_name='Anna',channel='weplay-internet-'+'c'*32,internet_consent=value),host=False)
            channel.assert_not_called();play.assert_not_called()


if __name__=='__main__':unittest.main()
