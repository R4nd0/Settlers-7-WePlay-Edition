import unittest
from unittest.mock import Mock

from crossplay_acceptance import Run, SLOTS, seen_label


class AcceptanceDriverTests(unittest.TestCase):
    def test_saved_room_waits_for_native_admission_before_ready_and_start(self):
        for edition, slots in SLOTS.items():
            with self.subTest(host=edition):
                desktop = Mock()
                desktop.ready.return_value = True
                run = Run(None, desktop, edition)
                run.games = {number: number for number in SLOTS}
                actions = []
                desktop.click.side_effect = lambda game, x, y: actions.append((game, x, y))
                run.ready_guest = Mock(side_effect=lambda game, y, _: actions.append((game, 919, y)))
                run.wait_event = Mock(side_effect=lambda event, count, **_: actions.append((event, count)))
                run.open_lobby('weplay123')
                self.assertEqual(actions[4], ('saved-native-file-receipt', 2))
                self.assertEqual(actions[5:7], [(guest, 919, {2: 250, 3: 322}[slot])
                                               for guest, slot in zip(SLOTS, slots) if guest != edition])
                self.assertEqual(actions[7:], [('saved-ready-applied', 2), (edition, 915, 630),
                                               ('saved-native-start-receipt', 2)])

    def test_ready_needs_native_confirmation_from_that_guest(self):
        desktop = Mock()
        run = Run(None, desktop, 1)
        run.games = {3: 'Steam guest'}
        unrelated = {'deliveries': [['native-ready-intent', 2, 1]]}
        confirmed = {'deliveries': [['native-ready-intent', 3, 1]]}
        desktop.ready.return_value = True
        run.sample = Mock(side_effect=[unrelated, unrelated, unrelated, confirmed])
        run.ready_guest(3, 322, False)
        desktop.click.assert_called_once_with('Steam guest', 919, 322)

    def test_fuzzy_label_accepts_typo_but_not_other_dialog_choice(self):
        self.assertTrue(seen_label('cortinue', 'continue'))
        self.assertFalse(seen_label('exit to desktop', 'save game'))

    def test_rejects_mismatched_original_turn_checksums(self):
        run = Run(None, Mock(), 1)
        run.sample = Mock(return_value={'rooms_ready': 3, 'world': {'crc_mismatches': 1}})
        with self.assertRaisesRegex(RuntimeError, 'mismatch'):
            run.world(1)

    def test_accepts_only_synchronized_turns_for_all_three_clients(self):
        run = Run(None, Mock(), 1)
        world = dict(common_finished_turns=10, contiguous_from_zero=True, elapsed_since_first_measurement=2)
        run.sample = Mock(side_effect=[{'rooms_ready': 2, 'world': world}, {'rooms_ready': 3, 'world': world}])
        self.assertEqual(run.world(1), world)
        self.assertEqual(run.sample.call_count, 2)


if __name__ == '__main__':
    unittest.main()
