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
RC3.14 assets and existing installed copies have not been changed. Current
source adapter pins advanced separately for the in-game invitation replacement
and the AI terrain-search correction; those builds still leave catch-up disabled.

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

## Healthy-memory two-client comparison

A subsequent cold-start comparison used the same protected checkpoint and
two isolated Ubisoft clients on one PC, with two human slots and two AI slots.
The guest selected Ready once. Start stayed disabled during the queue and
became enabled normally when it drained.

| Measured native hold-to-queue-drained interval | Duration |
| --- | ---: |
| Existing pacing | 169.308 s |
| Opt-in bounded catch-up | 54.690 s |

The measured queue wait was about 68% shorter. These are actual game-process
observations, distinct from the scheduling model. They measure the sender
queue gate, not total load time or network throughput. Both runs kept healthy
memory sentinels: the minimum free host RAM was 8,696 MiB in the baseline and
6,220 MiB in the candidate.

The baseline clients closed normally before world start. The candidate then
loaded the saved world, but its subsequent readiness/progress check stalled:
the guest displayed the host as blocked at 100%, and window activation timed
out for both clients. A five-second passive sample measured approximately
4.8 CPU seconds on the host and 0.17 on the guest. This does not establish the
cause or show that the pacing change caused it; the baseline world was not
tested in this comparison. Private dumps and status records were preserved.
Only the two verified test clients were stopped under the user's standing
authorization. Their original adapter/UI files were restored and the source
checkpoint hash remained unchanged.

That candidate run therefore **did not qualify successful continued multiplayer
gameplay**. Catch-up remains opt-in. Subsequent investigation found an extreme
AI angle and overflowing native terrain-search bounds in the stalled host. The
same loop also occurred in an older run before the pacing candidate existed;
repeat world starts with both pacing policies progressed. See the separate
[terrain-search correction and verification](MULTIPLAYER-RESUME-TERRAIN.md).
No desync-free, Steam live-game or two-PC claim follows from the timing comparison.
