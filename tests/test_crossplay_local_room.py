"""Installed-room launcher fixtures. No game or platform process is started."""
from pathlib import Path
import sys
import unittest
from unittest.mock import Mock, patch

sys.path.insert(0, str(Path(__file__).resolve().parents[1] / 'tools/installer'))
import crossplay_room as rooms
import test_crossplay_content as content_fixtures


class LocalRoomTests(unittest.TestCase):
    def setUp(self):
        fixture = content_fixtures.CrossplayContentTests()
        fixture.setUp()
        self.addCleanup(fixture.doCleanups)
        self.catalog = fixture.catalog()
        fixture.approve(self.catalog)
        self.players = tuple(rooms.Player(fixture.roots[edition], edition, f'Player {edition}')
                             for edition in (1, 2, 3))
        for player in self.players:
            suffix = 'Profiles' if player.edition == 1 else 'THE SETTLERS 7 - History Edition'
            (player.root / 'LocalData/ProfileRoot' / suffix).mkdir(parents=True, exist_ok=True)
        self.options = dict(host_slot=2, map_path='Data/Maps/Map', map_slots=(1, 2, 3), capacity=4)

    def test_unapproved_catalog_stops_before_broker_or_platform_services(self):
        with patch('crossplay_content.APPROVED_DIGESTS', frozenset()), \
                patch.object(rooms.platform_standalone, 'load'), \
                patch.object(rooms, 'CrossplayRoomBroker') as broker:
            with self.assertRaisesRegex(ValueError, 'catalog_unapproved'):
                rooms.LocalRoom(self.players, self.catalog, **self.options)
            broker.assert_not_called()

    def test_uid_and_installed_route_bindings_include_the_selected_history_host(self):
        gold = Mock(auth_port=4561, config_port=4562)
        gold.__enter__ = Mock(return_value=gold)
        gold.__exit__ = Mock()
        history_services = []

        def history(*args, **kwargs):
            service = Mock()
            service.__enter__ = Mock(return_value=service)
            service.__exit__ = Mock()
            history_services.append(service)
            return service

        game = Mock()
        game.poll.return_value = None

        def launch(*args, on_initialized, **kwargs):
            on_initialized(game)
            return game

        with patch.object(rooms.platform_standalone, 'load'), \
                patch.object(rooms.platform_standalone, 'launch', side_effect=launch) as standalone, \
                patch.object(rooms, 'gold_service_class', return_value=Mock(return_value=gold)), \
                patch.object(rooms, 'HistorySessionService', side_effect=history):
            with rooms.LocalRoom(self.players, self.catalog, **self.options) as room:
                self.assertEqual([binding.host_slot for binding in room.bindings], [2, 2, 2])
                self.assertEqual([binding.edition for binding in room.bindings], [1, 2, 3])
                self.assertEqual(len({binding.session for binding in room.bindings}), 1)
                self.assertEqual(len({binding.key for binding in room.bindings}), 3)
                self.assertEqual(room.bindings[0].gold_auth_port, 4561)
                self.assertIsNone(room.bindings[1].gold_auth_port)
                self.assertEqual(len(history_services), 2)
                self.assertIs(room.launch(2), game)
                self.assertEqual(standalone.call_args.args, (self.players[1].root,))
                self.assertIs(standalone.call_args.kwargs['crossplay'], room.bindings[1])
                self.assertIs(standalone.call_args.kwargs['crossplay_catalog'], self.catalog)
                with self.assertRaisesRegex(ValueError, 'configuration_invalid'):
                    room.launch(2)
            game.terminate.assert_not_called()


if __name__ == '__main__':
    unittest.main()
