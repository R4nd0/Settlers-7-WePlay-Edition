# Original turn comparison and measured progress

`tools/gold/world_evidence.py` is a read-only observer for the experimental
two-human Gold/History lab. It compares original sent EOT observations,
including runs with two additional AI players. It opens no network listener
and cannot start a map, acknowledge a turn or change simulation speed.

Run it alongside an owned pair with its two native turn logs:

```powershell
python tools/gold/world_evidence.py GOLD_LOG HISTORY_LOG --seconds 120
```

Each sample reports common original finished turns, the first mismatching
CRC, gaps and pending input. The observer accepts only schema 2 single-EOT
records with the original-sender flag and no synthesized turn/checksum. It
excludes initial EOTs without a finished checksum. Duplicate identical turns
are harmless; conflicting duplicates, regressing turns, source changes, log
replacement/truncation and malformed input fail closed. Reads, line lengths
and retained turns are bounded. Received observations do not count as a second
client's independently sent checksum.

Known native `TurnExcluded` diagnostics are counted separately. They preserve
the already validated CRC prefix and prevent a stability or speed-target claim.
They never become finished turns. Unknown or malformed diagnostics still fail
closed; this distinction matters when a memory stop closes the native session.

The measured rate uses a monotonic wall clock and progress after the first
complete, drained common observation. Reading an existing historical log
provides a CRC comparison but no speed measurement. A live sample needs at
least 30 seconds, consecutive matching turns from zero, no pending backlog or
partial line, and the requested rate before `speed_target_observed` is true.
The default target is 200 finished turns per second for the lab's requested
5-ms pacing. This measures arrival of original common-turn logs; disk buffering
and scheduling still affect it. It always leaves `effective_x20_qualified` and
`gameplay_qualified` false. AI activity, map compatibility, encrypted EasyTier
transport, NEO and save/reload need their own runtime evidence.

## Observed results, 2026-11-14

- The observer independently reproduced the previous Gold/Steam AI run's
  first CRC mismatch at finished turn 1,278: 1,282 shared observations through
  turn 1,281, with two mismatches. This agrees with the separate comparator.
- A later Gold/Steam DXVK run with two AI players completed 1,188 matching
  original turns, 0–1187. It ended before the known failure point, so it does
  not establish that the desync is fixed. During a 94.48-second live sampling
  interval, 844 common turns advanced, about 8.93 per second. Effective x20
  was not established.
- That later run installed a guarded experimental Gold formation transform,
  but every observed invocation was excluded: zero corrections applied and
  9,169 exclusions. It supplies no evidence that the transform fixes the
  problem. Its unchanged original fallback passed the separate native ABI
  fixture; 137 original-code mutation/readability checks also passed.
- Both clients in that run exited normally with code zero. Options, profiles,
  archives, adapters and shared state were restored; no preexisting save was
  replaced. The broker recorded one rejection during shutdown.

Seventeen automated observer tests cover original-record validation, partial
writes, initial sentinels, mismatches, gaps, duplicates, source ownership,
file replacement/truncation, bounded input and measured-rate timing. Those
tests use synthetic logs; the results above come from separate actual runs.

The longer diagnostic watchdog is now 300 seconds so slow runs can reach the
known failure point. This changes only the owned test archive's normal exit
deadline. It does not change game speed, readiness or checksums.

See [AI preparation and limits](GOLD-HISTORY-AI-PREPARATION.md) and
[EasyTier scope](GOLD-HISTORY-EASYTIER-AI.md).
