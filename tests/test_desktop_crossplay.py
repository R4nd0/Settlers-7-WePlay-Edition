"""Gated desktop mixed IPC, private invitations and original process lifetime."""
from contextlib import nullcontext
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock,MagicMock,patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(ROOT/'tests')]
import desktop_backend as backend
import crossplay_content as content
import crossplay_network as network
from crossplay_invitation import Invitation
from internet_room import Room
import test_desktop_backend as desktop_fixture
import test_crossplay_content as content_fixture

ROOM=Room('a'*32,'b'*64,'10.203.77.0/24')


class DesktopCrossplayTests(unittest.TestCase):
    def setUp(self):
        fixture=desktop_fixture.DesktopTests();fixture.setUp();self.addCleanup(fixture.doCleanups)
        self.desktop=fixture.desktop;self.events=fixture.events
        self.fixture=content_fixture.CrossplayContentTests();self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.catalog=self.fixture.catalog();self.fixture.approve(self.catalog)
        self.enterContext(patch.object(self.desktop,'verification',side_effect=lambda:nullcontext()))
        self.manifest=self.enterContext(patch.object(self.desktop,'package_manifest',return_value={'crossplay_mixed':True}))
        self.enterContext(patch.object(self.desktop,'installed',return_value={'package_id':'fixture','renderer_policy':'dxvk'}))
        self.enterContext(patch.object(self.desktop,'package_id',return_value='fixture'))
        self.enterContext(patch.object(self.desktop,'verify_instance'))
        self.enterContext(patch.object(self.desktop,'check_start_memory'))
        self.enterContext(patch.object(backend.graphics,'ensure_closed'))
        self.enterContext(patch.object(backend.graphics,'session_lock',side_effect=lambda *_:nullcontext()))
        self.enterContext(patch.object(content,'package_catalog',return_value=self.catalog))
        self.game_root=self.enterContext(patch.object(self.desktop,'game'))
        import platform_standalone
        self.load=self.enterContext(patch.object(platform_standalone,'load'))
        self.edition(1)
        import internet_private_channel,internet_room
        self.channel=MagicMock();self.channel.read.return_value=None
        self.private=self.enterContext(patch.object(internet_private_channel,'PrivateChannel',return_value=self.channel))
        self.channel.__enter__.return_value=self.channel
        self.enterContext(patch.object(backend,'enumerate_interfaces',return_value=[]))
        self.enterContext(patch.object(internet_room,'create',return_value=ROOM))
        self.owner=MagicMock();self.owner.__enter__.return_value=self.owner
        self.host=self.enterContext(patch.object(network.NetworkSession,'host',return_value=self.owner))
        self.guest=self.enterContext(patch.object(network.NetworkSession,'guest',return_value=self.owner))
        self.run=self.enterContext(patch.object(self.desktop,'crossplay_game',return_value={'exit_code':0}))

    def edition(self,edition):
        root=self.fixture.roots[edition]
        self.game_root.return_value=root
        exe=root/content.REQUIRED[edition][0]
        self.load.return_value=({},exe,root/content.REQUIRED[edition][1])

    def request(self,host=True):
        value=dict(player_name='WePlay Player',channel='weplay-internet-'+'c'*32,internet_consent=1)
        if host:value.update(history_edition=3,mode='assisted',direct_endpoint=None)
        return value

    def invite(self,edition):
        return Invitation(ROOM,edition,b's'*16,b'k'*32,b'i'*16,self.catalog.digest).token()

    def test_disabled_package_cannot_open_private_channel_or_network(self):
        self.manifest.return_value={}
        with self.assertRaisesRegex(ValueError,'crossplay_unavailable'):
            self.desktop.crossplay_play(self.request(),host=True)
        self.private.assert_not_called();self.host.assert_not_called();self.run.assert_not_called()

    def test_consent_types_roles_and_extra_request_fields_fail_before_network(self):
        variants=[dict(self.request(),internet_consent=x) for x in (True,0,'1',None)]
        variants.extend((dict(self.request(),history_edition=True),dict(self.request(),history_edition=1),
                         dict(self.request(),arbitrary_endpoint='127.0.0.1'),dict(self.request(),player_name='\n')))
        for request in variants:
            with self.assertRaises(ValueError):self.desktop.crossplay_play(request,host=True)
        self.private.assert_not_called();self.host.assert_not_called()

    def test_gold_host_routes_only_to_mixed_owner_and_private_invitation_callback(self):
        self.assertEqual(self.desktop.crossplay_play(self.request(),host=True),{'exit_code':0})
        self.host.assert_called_once()
        self.assertEqual(self.host.call_args.args,(self.fixture.roots[1],self.catalog))
        self.assertEqual(self.host.call_args.kwargs['history'],3)
        self.assertIs(self.host.call_args.kwargs['room'],ROOM)
        options=self.run.call_args.kwargs
        self.assertEqual(set(options),{'invitation_ready','startup'})
        self.assertIs(options['invitation_ready'],self.channel.invitation_ready)
        self.assertIsInstance(options['startup'],backend.desktop_settings.GraphicsStartup)
        self.guest.assert_not_called()

    def test_original_history_guests_bind_only_the_invited_edition(self):
        for edition in (2,3):
            self.edition(edition);token=self.invite(edition)
            self.channel.read.return_value=token
            self.desktop.crossplay_play(self.request(False),host=False)
            self.assertEqual(self.guest.call_args.kwargs['invitation'],token)
            self.assertIsNone(self.run.call_args.kwargs['invitation_ready'])
            self.assertNotIn(token,str(self.events))
        self.host.assert_not_called()

    def test_wrong_role_or_changed_content_never_opens_the_channel(self):
        self.edition(3)
        with self.assertRaisesRegex(ValueError,'edition_mismatch'):
            self.desktop.crossplay_play(self.request(),host=True)
        self.edition(1)
        (self.fixture.roots[1]/'Data/Base/BBA/reviewed-gameplay.bba').write_bytes(b'changed')
        with self.assertRaisesRegex(ValueError,'content_changed'):
            self.desktop.crossplay_play(self.request(),host=True)
        self.private.assert_not_called();self.host.assert_not_called()

    def test_invited_other_history_edition_never_starts_transport(self):
        self.edition(3);self.channel.read.return_value=self.invite(2)
        with self.assertRaisesRegex(ValueError,'edition_mismatch'):
            self.desktop.crossplay_play(self.request(False),host=False)
        self.guest.assert_not_called();self.run.assert_not_called()

    def test_native_initialization_attaches_game_before_private_invitation(self):
        # Restore the method for this test; other launcher boundaries stay mocked.
        self.desktop.crossplay_game=backend.Desktop.crossplay_game.__get__(self.desktop)
        import platform_standalone,memory_guard
        game=Mock(pid=123);game.poll.return_value=0;game.wait.return_value=0
        order=[]
        def launch(*args,**kwargs):
            self.assertIs(kwargs['crossplay'],self.owner.binding)
            self.assertIs(kwargs['crossplay_catalog'],self.catalog)
            kwargs['on_initialized'](game);order.append('initialized');return game
        self.owner.binding.edition=3;self.owner.attach.side_effect=lambda _:order.append('attached')
        self.owner.invitation.return_value='private-fixture-invitation'
        received=[]
        with patch.object(platform_standalone,'launch',side_effect=launch), \
                patch.object(backend.choice,'saved_launch_environment',side_effect=lambda _,env,**_options:env), \
                patch.object(backend.choice,'game_arguments',return_value=[]), \
                patch.object(backend.choice,'start_window_mode'), \
                patch.object(memory_guard,'Sentinel'),patch.object(memory_guard,'Probe'):
            self.desktop.crossplay_game(self.fixture.roots[3],self.catalog,self.owner,
                invitation_ready=lambda token:(received.append(token),order.append('invited')))
        self.assertEqual(order,['attached','initialized','invited'])
        self.assertEqual(received,['private-fixture-invitation'])
        self.assertNotIn('private-fixture-invitation',str(self.events))
        game.terminate.assert_not_called();game.kill.assert_not_called();game.close.assert_called_once()

    def test_post_start_private_pipe_failure_waits_for_original_game_exit(self):
        self.desktop.crossplay_game=backend.Desktop.crossplay_game.__get__(self.desktop)
        import platform_standalone,memory_guard
        game=Mock(pid=123);game.poll.return_value=None
        def wait(**_kwargs):game.poll.return_value=0;return 0
        game.wait.side_effect=wait
        self.owner.binding.edition=3
        def launch(*_args,**kwargs):kwargs['on_initialized'](game);return game
        callback=Mock(side_effect=RuntimeError('fixture-channel-failed'))
        with patch.object(platform_standalone,'launch',side_effect=launch), \
                patch.object(backend.choice,'saved_launch_environment',side_effect=lambda _,env,**_options:env), \
                patch.object(backend.choice,'game_arguments',return_value=[]), \
                patch.object(backend.choice,'start_window_mode'), \
                patch.object(memory_guard,'Sentinel'),patch.object(memory_guard,'Probe'):
            with self.assertRaisesRegex(RuntimeError,'fixture-channel-failed'):
                self.desktop.crossplay_game(self.fixture.roots[3],self.catalog,self.owner,invitation_ready=callback)
        self.owner.close.assert_called_once();game.wait.assert_called_once_with(close=False)
        game.terminate.assert_not_called();game.kill.assert_not_called();game.close.assert_called_once()
        self.assertIn(('warning',{'code':'crossplay_service_failed'}),self.events)
        kinds=[kind for kind,_ in self.events if kind in ('game_started','warning','game_finished')]
        self.assertEqual(kinds,['game_started','warning','game_finished'])


if __name__=='__main__':unittest.main()
