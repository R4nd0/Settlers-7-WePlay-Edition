# Multiplayer checkpoint transfer and the Start button

A player reported that a saved LAN match with two human players and two AI
opponents resumed successfully, but the host's Start button remained disabled
for approximately five minutes. That is an external player report, not a new
controlled two-PC measurement.

The checkpoint is transferred in the lobby. An early Ready click does not
remove the native sender's pending fragments. WePlay keeps the existing
readiness result false while its own queue is pending and refreshes the lobby
after the queue drains. This gate does not contain a five-minute countdown.
The five-minute host autosave interval concerns a running world and is separate.

## Pacing limitation

The existing sender uses 1-KiB fragments, at most eight per eligible owner-thread
update, with a deadline reset to `now + 20 ms` after each batch. This bounds
native allocations, but it discards unused send intervals when menu updates are
slower. At one update every 125 ms, the resulting ceiling is approximately
64 KiB/s, rather than the nominal 400 KiB/s. Actual transfer and readiness also
depend on native transport, receiving and validation.

An opt-in candidate retains the nominal rate and allows bounded catch-up for
at most four intervals: no more than 32 fragments in one update. Excess credit
after a long stall is discarded. The same-clock caller cannot repeatedly spend
old credit. Session cancellation and changed-manager rejection discard queued
fragments and pacing state. Original native dispatch, fragment contents,
checksum, recipient and readiness checks remain in place.

Build the candidate explicitly with `native/build.ps1 -LocalPairLab
-GameDirectory -ResumeCatchup -OutputDirectory <private-work-folder>`. The
normal build retains the existing eight-fragment pacing policy. Published
RC3.14 assets, adapter pins and existing installed copies have not been changed.

## Verification so far

Deterministic tests model a synthetic 21-MiB transfer:

| Menu update interval | Existing pacing | Bounded catch-up |
| --- | ---: | ---: |
| 20 ms | 53.740 s | 53.740 s |
| 63 ms | 169.281 s | 53.802 s |
| 125 ms | 335.875 s | 84.000 s |

These are scheduling-model results, not measured multiplayer speedups. Native
fixture tests additionally exercise both Ubisoft and Steam dispatch/constructor
ABIs, byte ownership, the maximum catch-up batch, cancellation, LastError and
unchanged readiness restrictions.

Two real isolated Ubisoft clients loaded the saved 2+2 roster and joined the
same native lobby. The guest selected Ready once. The host was visibly held
until its send queue drained, then Start became enabled without another Ready
click. The observed held-to-drained interval was 169.281 seconds. However, host
RAM reached the recovery threshold and one memory monitor failed. Both clients
were closed normally with exit code 0 before starting a world. This run is not
a clean performance baseline or a verification of the faster candidate.

A healthy-memory baseline/candidate comparison and continued-world checks are
still required. No faster real-game result or new desync claim is established
by the model or fixture tests.
