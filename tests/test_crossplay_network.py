"""Pair ownership around real local services; no VPN or native game launch."""
from pathlib import Path
import sys
import unittest
import uuid
from unittest.mock import Mock, patch

ROOT=Path(__file__).resolve().parents[1]
sys.path[:0]=[str(ROOT/'tools/installer'),str(ROOT/'tests')]
import crossplay_network as network
import crossplay_content as content
import test_crossplay_content as fixtures
from internet_room import Room
from crossplay_invitation import parse

ROOM=Room('a'*32,'b'*64,'10.203.77.0/24')


class CrossplayNetworkTests(unittest.TestCase):
    def setUp(self):
        self.fixture=fixtures.CrossplayContentTests();self.fixture.setUp()
        self.addCleanup(self.fixture.doCleanups)
        self.catalog=self.fixture.catalog();self.fixture.approve(self.catalog)
        self.overlay=Mock(room=ROOM)
        self.gateway=Mock(port=12345,closed=False)
        self.open=self.enterContext(patch.object(network,'open_overlay',return_value=self.overlay))
        self.classes=self.enterContext(patch.object(network,'gateway_classes',return_value=(
            Mock(return_value=self.gateway),Mock(return_value=self.gateway))))
        self.enterContext(patch.object(network.sys,'platform','win32'))
        self.history=Mock()
        self.history.start.return_value=self.history
        self.history_factory=self.enterContext(patch.object(network,'HistorySessionService',return_value=self.history))

    def host(self,edition=3):
        result=network.NetworkSession.host(self.fixture.roots[1],self.catalog,room=ROOM,
            history=edition,name='Host',consent=1)
        self.addCleanup(result.close)
        return result

    def test_unapproved_content_and_missing_consent_open_no_overlay_or_services(self):
        for consent in (None,True,0,'1'):
            with self.assertRaisesRegex(ValueError,'consent_required'):
                network.NetworkSession.host(self.fixture.roots[1],self.catalog,room=ROOM,
                    history=3,name='Host',consent=consent)
        with patch.object(content,'APPROVED_DIGESTS',frozenset()):
            with self.assertRaisesRegex(ValueError,'catalog_unapproved'):self.host()
        self.open.assert_not_called();self.classes.assert_not_called()

    def test_host_listens_before_invitation_and_binds_one_real_gold_service(self):
        pair=self.host()
        self.overlay.start.assert_called_once()
        self.classes.return_value[0].assert_called_once_with(self.overlay,broker_port=pair.pair.broker.port)
        invite=parse(pair.invitation())
        self.assertEqual(invite.edition,3);self.assertEqual(invite.content,self.catalog.digest)
        self.assertNotEqual(invite.key,pair.binding.key)
        game=Mock();game.poll.return_value=None
        pair.attach(game);self.assertIs(pair.runtime.game,game)
        pair.close()
        self.assertTrue(pair.pair.gold.closed)
        self.assertTrue(pair.runtime.closed)
        self.overlay.close.assert_called_once()
        game.terminate.assert_not_called();game.kill.assert_not_called()
        with self.assertRaisesRegex(ValueError,'service_closed'):pair.invitation()

    def test_both_original_history_guests_use_only_the_invited_binding(self):
        for edition in (2,3):
            host=self.host(edition)
            guest=network.NetworkSession.guest(self.fixture.roots[edition],self.catalog,
                invitation=host.invitation(),name='Guest',consent=1)
            self.addCleanup(guest.close)
            self.assertIsNone(guest.pair)
            self.assertEqual(guest.binding.edition,edition)
            self.assertEqual(guest.binding.port,12345)
            self.assertIsNone(guest.binding.gold_auth_port)
            self.assertEqual(guest.binding.key,host.pair._guest.key)
            self.history_factory.assert_called_with(self.fixture.roots[edition],
                profile_id=self.history_factory.call_args.kwargs['profile_id'],name='Guest',cancel_check=None)
            profile=self.history_factory.call_args.kwargs['profile_id']
            self.assertEqual(str(uuid.UUID(profile)),profile)
            self.assertNotEqual(uuid.UUID(profile).bytes,guest.binding.identity)
            self.assertIs(guest.runtime.history,self.history)
            self.classes.return_value[1].assert_called_with(self.overlay,peer=ROOM.coordinator)
            guest.close();host.close()

    def test_history_start_failure_closes_transport_and_prevents_game_attachment(self):
        host=self.host();self.history.start.side_effect=RuntimeError('fixture history bootstrap failure')
        with self.assertRaisesRegex(RuntimeError,'history bootstrap failure'):
            network.NetworkSession.guest(self.fixture.roots[3],self.catalog,
                invitation=host.invitation(),name='Guest',consent=1)
        self.history.close.assert_called_once()
        self.gateway.close.assert_called();self.overlay.close.assert_called()

    def test_mixed_guest_cancellation_reaches_local_history_bootstrap(self):
        host = self.host()
        cancel = Mock(side_effect=RuntimeError('cancelled'))
        def bootstrap(*_args, cancel_check=None, **_options):
            if cancel_check:
                self.history.start.side_effect = cancel_check
            return self.history
        self.history_factory.side_effect = bootstrap
        with self.assertRaisesRegex(RuntimeError, '^cancelled$'):
            network.NetworkSession.guest(self.fixture.roots[3], self.catalog,
                invitation=host.invitation(), name='Guest', consent=1, cancel_check=cancel)
        cancel.assert_called_once()
        self.history.close.assert_called_once()

    def test_wrong_guest_content_fails_before_a_second_overlay(self):
        pair=self.host();token=pair.invitation();self.open.reset_mock()
        path=self.fixture.roots[3]/'Base/BBA/reviewed-gameplay.bba'
        path.write_bytes(b'changed')
        with self.assertRaises(ValueError):
            network.NetworkSession.guest(self.fixture.roots[3],self.catalog,
                invitation=token,name='Guest',consent=1)
        self.open.assert_not_called()

    def test_transport_start_failure_closes_owned_local_sockets_and_overlay(self):
        self.classes.return_value[0].side_effect=RuntimeError('fixture')
        created=[];original=network.Host
        def factory(*args,**kwargs):
            result=original(*args,**kwargs);created.append(result);return result
        with patch.object(network,'Host',side_effect=factory):
            with self.assertRaisesRegex(RuntimeError,'fixture'):self.host()
        self.assertTrue(created[0].gold.closed)
        self.assertEqual(created[0].broker.listener.fileno(),-1)
        self.overlay.close.assert_called_once()

    def test_a_still_running_service_thread_prevents_tunnel_cleanup(self):
        owner=network.NetworkSession();owner.overlay=self.overlay;owner.runtime=Mock()
        owner.runtime.close.side_effect=RuntimeError('crossplay_service_shutdown_failed')
        with self.assertRaisesRegex(RuntimeError,'shutdown_failed'):owner.close()
        self.overlay.close.assert_not_called()
        owner.runtime.close.side_effect=None;owner.close()
        self.overlay.close.assert_called_once()


if __name__=='__main__':unittest.main()
