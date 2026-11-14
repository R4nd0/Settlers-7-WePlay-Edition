import importlib.util
import json
from pathlib import Path
import tempfile
import unittest
from unittest.mock import patch

spec = importlib.util.spec_from_file_location('world_evidence', Path(__file__).parents[1] / 'tools/gold/world_evidence.py')
evidence = importlib.util.module_from_spec(spec)
spec.loader.exec_module(evidence)


def row(turn, crc=123, source=1, **changes):
    result = dict(direction='sent', sequence=turn+1, nested_count=1,
                  original_sender_used=True, turn_or_checksum_synthesized=False,
                  native_field_schema=2, first_fields_present=True,
                  first_turn_finished=turn, first_crc_types=1, first_crc_values=crc,
                  outer_source_slot=source, first_source_slot=source)
    result.update(changes)
    return result


class WorldEvidenceTests(unittest.TestCase):
    def setUp(self):
        self.tmp = tempfile.TemporaryDirectory()
        self.addCleanup(self.tmp.cleanup)
        self.gold, self.history = (Path(self.tmp.name)/name for name in ('gold', 'history'))
        self.now = 0.0
        self.observer = evidence.PairEvidence(self.gold, self.history, clock=lambda: self.now)

    def append(self, gold, history=None):
        for path, rows in ((self.gold, gold), (self.history, history if history is not None else [dict(r, first_source_slot=3, outer_source_slot=3) for r in gold])):
            with path.open('ab') as stream:
                for value in rows:
                    stream.write(json.dumps(value).encode()+b'\n')

    def test_absent_logs_establish_nothing(self):
        result = self.observer.poll()
        self.assertEqual(result['shared_native_crc_count'], 0)
        self.assertFalse(result['matching_from_world_start'])

    def test_initial_sentinel_excluded_and_equal_original_crcs_compared(self):
        self.append([row(0xffffffff, first_crc_types=0), row(0), row(1)])
        result = self.observer.poll()
        self.assertEqual(result['shared_native_crc_count'], 2)
        self.assertTrue(result['matching_from_world_start'])
        self.assertFalse(result['gameplay_qualified'])

    def test_actual_mismatch_turn_not_later_desync_event_turn(self):
        self.append([row(t) for t in range(5)], [row(t, 555 if t >= 3 else 123, 3) for t in range(5)])
        result = self.observer.poll()
        self.assertEqual(result['first_crc_mismatch_finished_turn'], 3)
        self.assertEqual(result['mismatch_count'], 2)
        self.assertFalse(result['matching_from_world_start'])

    def test_no_initial_or_truncated_prefix_stability_claim(self):
        for turns in ((2, 3), (0, 2)):
            with self.subTest(turns=turns):
                self.gold.unlink(missing_ok=True); self.history.unlink(missing_ok=True)
                self.append([row(t) for t in turns])
                result = evidence.PairEvidence(self.gold, self.history).poll()
                self.assertFalse(result['matching_from_world_start'])

    def test_partial_append_is_deferred(self):
        self.append([row(0)])
        line = json.dumps(row(1)).encode()
        with self.gold.open('ab') as stream: stream.write(line[:19])
        result = self.observer.poll()
        self.assertTrue(result['partial_lines_pending'])
        self.assertIsNone(self.observer.baseline)
        with self.gold.open('ab') as stream: stream.write(line[19:]+b'\n')
        self.append([], [row(1, source=3)])
        self.assertEqual(self.observer.poll()['last_common_finished_turn'], 1)

    def test_conflicting_duplicate_rejected_exact_duplicate_allowed(self):
        self.append([row(0), row(0)])
        self.assertEqual(self.observer.poll()['shared_native_crc_count'], 1)
        self.append([row(0, 456)])
        with self.assertRaisesRegex(evidence.EvidenceError, 'conflicting'): self.observer.poll()

    def test_monotonic_turns_source_identity_and_finished_sentinel(self):
        bad_sets = ([row(2), row(1)], [row(0), row(1, source=2)], [row(0xffffffff)])
        for rows in bad_sets:
            with self.subTest(rows=rows):
                self.gold.write_bytes(b''.join(json.dumps(r).encode()+b'\n' for r in rows))
                with self.assertRaises(evidence.EvidenceError): evidence.TurnLog(self.gold).poll()

    def test_ambiguous_or_synthetic_rows_cannot_count(self):
        for change in ({'original_sender_used':False}, {'turn_or_checksum_synthesized':True}, {'nested_count':2}, {'nested_count':True}, {'first_fields_present':False}, {'native_field_schema':1}, {'first_crc_values':True}, {'first_turn_finished':-1}, {'first_source_slot':True}, {'outer_source_slot':True}, {'first_crc_types':2}):
            with self.subTest(change=change):
                self.gold.write_text(json.dumps(row(0, **change))+'\n')
                with self.assertRaises(evidence.EvidenceError): evidence.TurnLog(self.gold).poll()

    def test_received_rows_do_not_count(self):
        self.append([row(0, direction='received')])
        self.assertEqual(self.observer.poll()['shared_native_crc_count'], 0)

    def test_same_native_source_on_both_clients_rejected(self):
        self.append([row(0)], [row(0)])
        with self.assertRaisesRegex(evidence.EvidenceError, 'distinct'): self.observer.poll()

    def test_truncate_replace_and_disappear_fail_closed(self):
        for operation in ('truncate', 'replace', 'remove'):
            with self.subTest(operation=operation):
                self.gold.write_text(json.dumps(row(0))+'\n'); log = evidence.TurnLog(self.gold); log.poll()
                if operation == 'truncate': self.gold.write_bytes(b'')
                elif operation == 'remove': self.gold.unlink()
                else:
                    other = self.gold.with_suffix('.new'); other.write_text(json.dumps(row(0))+'\n'); other.replace(self.gold)
                with self.assertRaises(evidence.EvidenceError): log.poll()

    def test_malformed_and_oversized_lines_and_bounded_history(self):
        for data in (b'broken\n', b'[]\n', b'\n', b'x'*(evidence.MAX_LINE+1), b'x'*(evidence.MAX_LINE+1)+b'\n'):
            self.gold.write_bytes(data)
            with self.assertRaises(evidence.EvidenceError): evidence.TurnLog(self.gold).poll()
        self.append([row(t) for t in range(3)])
        # Replace the malformed fixture, then deliberately exercise a tiny cap.
        self.gold.write_bytes(b''.join(json.dumps(row(t)).encode()+b'\n' for t in range(3)))
        with patch.object(evidence, 'MAX_TURNS', 2):
            with self.assertRaisesRegex(evidence.EvidenceError, 'turn-limit'): evidence.TurnLog(self.gold).poll()

    def test_measured_target_requires_thirty_seconds_and_fresh_progress(self):
        self.append([row(0)]); self.observer.poll()
        self.now = 30.; self.append([row(t) for t in range(1, 6001)])
        result = self.observer.poll()
        while result['backlog_pending']: result = self.observer.poll()
        self.assertEqual(result['measured_shared_turns_per_second'], 200.)
        self.assertTrue(result['speed_target_observed'])
        self.assertFalse(result['effective_x20_qualified'])
        self.now = 60.; self.assertFalse(self.observer.poll()['speed_target_observed'])

    def test_backlog_does_not_create_speed_baseline(self):
        self.append([row(t) for t in range(8)])
        with patch.object(evidence, 'MAX_CHUNK', 512):
            result = self.observer.poll()
        self.assertTrue(result['backlog_pending'])
        self.assertIsNone(self.observer.baseline)

    def test_invalid_speed_and_backward_clock(self):
        for value in (0, -1, float('inf'), float('nan'), True):
            with self.assertRaises(ValueError): evidence.PairEvidence(self.gold, self.history, target_tps=value)
        self.observer.poll(); self.now = -1
        with self.assertRaisesRegex(evidence.EvidenceError, 'clock'): self.observer.poll()


if __name__ == '__main__': unittest.main()
