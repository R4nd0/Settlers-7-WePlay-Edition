"""Read-only, bounded comparison of original mixed-edition EOT observations.

The lab supplies these logs. This observer cannot start a game, change its speed,
acknowledge a turn, or establish VPN, NEO, AI or save/reload compatibility.
"""
import argparse
import json
import math
import os
from pathlib import Path
import time

MAX_CHUNK = 1024 * 1024
MAX_LINE = 16 * 1024
MAX_TURNS = 100_000


class EvidenceError(ValueError):
    pass


def _uint(value, maximum=0xffffffff):
    return type(value) is int and 0 <= value <= maximum


class TurnLog:
    """Retain only finished-turn CRCs, never raw payloads or account metadata."""
    def __init__(self, path):
        self.path = Path(path)
        self.identity = None
        self.offset = 0
        self.pending = b''
        self.turns = {}
        self.source = None
        self.native_exclusions = 0

    def poll(self):
        try:
            stream = self.path.open('rb')
        except FileNotFoundError:
            if self.identity is not None:
                raise EvidenceError('observed-log-disappeared') from None
            return
        with stream:
            stat = os.fstat(stream.fileno())
            identity = (stat.st_dev, stat.st_ino)
            if self.identity is not None and identity != self.identity:
                raise EvidenceError('observed-log-replaced')
            if stat.st_size < self.offset:
                raise EvidenceError('observed-log-truncated')
            self.identity = identity
            stream.seek(self.offset)
            chunk = stream.read(MAX_CHUNK)
        self.offset += len(chunk)
        lines = (self.pending + chunk).split(b'\n')
        self.pending = lines.pop()
        if len(self.pending) > MAX_LINE:
            raise EvidenceError('observation-line-too-large')
        for line in lines:
            if not line or len(line) > MAX_LINE:
                raise EvidenceError('invalid-observation-line-size')
            try:
                row = json.loads(line)
            except (ValueError, UnicodeError):
                raise EvidenceError('invalid-observation-json') from None
            self._row(row)

    def _row(self, row):
        if isinstance(row, dict) and row.get('excluded') is True:
            if ('direction' in row or any(not _uint(row.get(k), 1)
                    for k in ('world_guard', 'methods_guard', 'source_guard'))
                    or any(not _uint(row.get(k))
                           for k in ('type', 'source', 'own', 'other', 'async'))):
                raise EvidenceError('invalid-native-exclusion-record')
            if self.native_exclusions >= MAX_TURNS:
                raise EvidenceError('observation-exclusion-limit')
            self.native_exclusions += 1
            return
        if not isinstance(row, dict) or row.get('direction') not in ('sent', 'received'):
            raise EvidenceError('invalid-observation-direction')
        if row['direction'] != 'sent':
            return
        if (row.get('original_sender_used') is not True
                or row.get('turn_or_checksum_synthesized') is not False
                or row.get('native_field_schema') != 2
                or row.get('first_fields_present') is not True
                or type(row.get('nested_count')) is not int or row['nested_count'] != 1):
            raise EvidenceError('original-single-eot-not-established')
        kind = row.get('first_crc_types')
        turn, crc = row.get('first_turn_finished'), row.get('first_crc_values')
        source, outer = row.get('first_source_slot'), row.get('outer_source_slot')
        if (not _uint(kind, 1) or not _uint(turn) or not _uint(crc)
                or not _uint(source, 3) or not _uint(outer, 3) or source == 0 or source != outer):
            raise EvidenceError('invalid-native-eot-fields')
        if self.source is not None and source != self.source:
            raise EvidenceError('native-source-changed')
        self.source = source
        if kind == 0:
            return  # Original initial/unfinalized EOT; it proves no finished CRC.
        if turn == 0xffffffff:
            raise EvidenceError('invalid-finished-turn-sentinel')
        if turn in self.turns:
            if self.turns[turn] != crc:
                raise EvidenceError('conflicting-original-turn')
            return
        if len(self.turns) >= MAX_TURNS:
            raise EvidenceError('observation-turn-limit')
        if self.turns and turn <= next(reversed(self.turns)):
            raise EvidenceError('original-turn-order-regressed')
        self.turns[turn] = crc


class PairEvidence:
    def __init__(self, gold, history, *, clock=time.monotonic, target_tps=200.0):
        if not isinstance(target_tps, (int, float)) or isinstance(target_tps, bool) or not math.isfinite(target_tps) or target_tps <= 0:
            raise ValueError('Invalid measured speed target')
        self.logs = TurnLog(gold), TurnLog(history)
        self.clock = clock
        self.target = target_tps
        self.baseline = None
        self.last_time = None

    def poll(self):
        now = self.clock()
        if not math.isfinite(now) or self.last_time is not None and now < self.last_time:
            raise EvidenceError('observation-clock-regressed')
        self.last_time = now
        for log in self.logs:
            log.poll()
        gold, history = self.logs
        if gold.source is not None and gold.source == history.source:
            raise EvidenceError('distinct-native-human-sources-not-established')
        common = sorted(gold.turns.keys() & history.turns.keys())
        mismatches = [t for t in common if gold.turns[t] != history.turns[t]]
        consecutive = bool(common) and all(b == a + 1 for a, b in zip(common, common[1:]))
        backlog = any(log.identity is not None and log.path.stat().st_size > log.offset for log in self.logs)
        partial = any(log.pending for log in self.logs)
        exclusions = sum(log.native_exclusions for log in self.logs)
        if common and not backlog and not partial and self.baseline is None:
            self.baseline = now, common[-1]
        elapsed = now - self.baseline[0] if self.baseline else 0.0
        advanced = common[-1] - self.baseline[1] if common and self.baseline else 0
        tps = advanced / elapsed if elapsed > 0 else None
        return {
            'schema': 1, 'original_crcs_retained': True,
            'shared_native_crc_count': len(common),
            'first_common_finished_turn': common[0] if common else None,
            'last_common_finished_turn': common[-1] if common else None,
            'consecutive_shared_turns': consecutive,
            'first_crc_mismatch_finished_turn': mismatches[0] if mismatches else None,
            'mismatch_count': len(mismatches),
            'matching_from_world_start': consecutive and common[0] == 0 and not mismatches and not exclusions,
            'native_eot_exclusion_count': exclusions,
            'measured_seconds': elapsed, 'measured_turns_advanced': advanced,
            'measured_shared_turns_per_second': tps,
            'target_turns_per_second': self.target,
            'measurement_kind': 'original-common-turn-log-arrival-wall-clock',
            'speed_target_observed': elapsed >= 30 and advanced > 0 and tps >= self.target
                and consecutive and common[0] == 0 and not mismatches and not exclusions and not backlog and not partial,
            'effective_x20_qualified': False,
            'gameplay_qualified': False,
            'partial_lines_pending': partial,
            'backlog_pending': backlog,
        }


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('gold', type=Path)
    parser.add_argument('history', type=Path)
    parser.add_argument('--seconds', type=float, default=0)
    parser.add_argument('--target-tps', type=float, default=200)
    args = parser.parse_args()
    if not math.isfinite(args.seconds) or not 0 <= args.seconds <= 3600:
        parser.error('--seconds must be between zero and 3600')
    observer = PairEvidence(args.gold, args.history, target_tps=args.target_tps)
    deadline = time.monotonic() + args.seconds
    while True:
        result = observer.poll()
        print(json.dumps(result, allow_nan=False), flush=True)
        if time.monotonic() >= deadline and not result['backlog_pending']:
            break
        time.sleep(.25)


if __name__ == '__main__':
    main()
