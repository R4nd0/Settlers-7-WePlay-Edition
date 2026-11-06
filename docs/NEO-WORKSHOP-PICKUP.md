# NEO workshop pickup deadlock — 2026-11-06

Included in RC3.18; the already running user game is not modified.

## Reproduced failure

The preserved multiplayer checkpoint starts at logic turn 67349. Blue weaving
mill 65564 is attached to warehouse 65550. Four wool requests have reached the
warehouse and appear in the workshop's ready-pickup queue. The worker is present
in its manager's active list and repeatedly cycles through idle/start/pickup
selection. Neither a missing worker nor a missing warehouse notification explains
this stall.

Production task 3343818 has native state 4, but its child wool request 3015613
is still awaiting collection. The native production admission check rejects
that task because the child input-completion flag is false. Separately, the
pickup selector treats the existence of that state-4 task as a veto against
collecting inputs. Production cannot start, and pickup cannot proceed.

This establishes the immediate deadlock mechanism. The earlier event that first
left the saved production task and its input request in this inconsistent state
has not been reconstructed.

## Correction

At the native workshop pickup decision, retain the selected production task as
a pickup veto only if its input requests pass the same completion gate used by
production admission. On the contradictory state, clear only the temporary
selector result. Native pickup reservation, movement, delivery and production
then proceed normally. The task, its creation turn, inventory and saved state
are not rewritten; no requests are cancelled or recreated.

The correction applies only to an active NEO world on its simulation owner
thread. NEO off retains the original result. Object types, complete generational
handles, parent links and vector bounds are checked before accepting a change.
Unexpected objects retain the native result. Ubisoft and Steam profiles have
independently matched callsites and byte guards for production admission, the
original selector and the pickup veto.

FIFO transport order and explicit player/AI individual priorities are unchanged.
Group priorities remain disabled in NEO. Lobby capability **NEO5** rejects NEO4
and older clients before play, including with NEO off. Existing NEO2 save payloads
and individual-priority restoration remain compatible.

## Actual game comparison

Both loopback multiplayer runs load the same untouched checkpoint, with two
human slots and two hard computer opponents. No production toggle is issued.

| Observation | Control | Corrected pickup selector |
| --- | --- | --- |
| Simulation advance | 1,500 turns | 1,500 turns |
| First observed refining state | None | Turn 67446, within 97 turns of the checkpoint |
| Four original wool requests | All remain blocked | All leave the pending workshop request set by turn 68300 |
| Native client exits | 0 / 0 | 0 / 0 |
| Recorded peer checksum comparisons | 451 matching | 451 matching |

The approximately five-turn read-only sampling interval bounds the event times;
these are observation times, not an exact per-turn delivery trace. Native worker
state changes independently show pickup, movement and repeated refinement.
The 451 recorded checksums cover the available diagnostic window, not every
turn of the 1,500-turn run. This is evidence for this saved deadlock, not a
general productivity or separate-PC qualification.

Private runs are `work/weavery-20261106/pair-04` (control) and `pair-05`
(correction). The final NEO5 build also passed a separate 500-turn resumed-world
test (`pair-06`): both clients exit normally, all 451 recorded peer checksums
match, and their sequence matches the corrected comparison run. Read-only
snapshots show wool carried at turn 67401, refinement at 67443 and the worker
carrying produced cloth at 67621. There is no production-mode toggle. All three
runs restore their test adapters, interfaces, profiles and runtime files exactly.
Raw game records and the checkpoint remain private under ignored `work/`.

## Synthetic verification

`neo_workshop_tests` exercises both address profiles, the native selection call
ABI, NEO off, foreign-thread fallback, completed/unfinished inputs, multiple
children, stale handles, wrong workshop/parent links and preservation of task records.
Map-option tests reject every mutated byte of the additional prerequisite
guards and reject downlevel lobby capabilities. Existing FIFO, explicit-intent,
distance, save-state and patch-installation tests remain required.

Both final build suites pass: 22 lab and 23 production executables/fixtures,
including 234 workshop pickup checks per build, 426/420 map-option checks,
4,147/2,466 bootstrap checks and 2,005/1,973 Steam-profile checks. Saved NEO2
intent decoding, FIFO and distance regressions also pass. Steam native runtime
and separate-PC multiplayer are not exercised by this loopback test.
