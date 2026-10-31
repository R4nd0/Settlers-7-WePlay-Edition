# Passive performance and simulation diagnostics

The later sustained menu investigation, repeated hangs, renderer comparison
and shared graphics choice are documented in [GRAPHICS-START.md](GRAPHICS-START.md).

The 2026-10-31 LAN failure is independently confirmed in the game's own log at
turn 461 (Fiery Chasm, three active players). Its private desync save and replay
were preserved before further tests. The native observation window contains
no reported x87/MXCSR control changes, but its untimed labels cannot identify
the precise failing callback or exclude numerical divergence elsewhere.
Review and regressions found local/peer castle-data inconsistencies; see
[the castle follow-up](MULTIPLAYER-CASTLES.md#desync-follow-up-2026-10-31).
Neither the log nor the synthetic correction establishes the actual cause.

## Scope

`tools/installer/observe_game.py` observes one explicitly selected, owned History
Edition test process. It does not operate the UI, call game functions, inject
code, suspend threads, change affinity/priority, or modify game memory/files.
It checks the executable and adapter fingerprints, exact process/module paths,
module sizes, launcher parent and process creation time. Native reads are
bounded typed counters. Unknown builds are rejected; do not guess their RVAs.

The observer records process CPU in core equivalents, working/private memory,
thread CPU and affinity, foreground status, native update counts, command turn
and command horizon. `simulation-idle` means the turn did not advance during a
sample; it is not a guessed menu/lobby state. No simulation checksum comparison
is performed. Identities, accounts, payloads and full logistics queues are not
read. Reading the complete native logistics task list would itself scale with
queue size and is unsuitable for frequent passive sampling.

Logical process I/O and page-fault deltas are also recorded. These include
cached/device I/O and soft faults and must not be labelled physical disk reads.

Example from the repository root, substituting the verified current game PID:

```powershell
python -B tools/installer/observe_game.py `
  --bundle "$PWD/work/installer-lan-v3-turn-offset-realtest" `
  --pid <current-owned-game-pid> --seconds 30 --interval 1 `
  --output "$PWD/work/new-measurement" --quiet
```

Use a new output directory below this repository's ignored `work/` directory.
Duration is bounded to 120 seconds. Detailed samples stay in `samples.jsonl`;
`--quiet` avoids printing the per-thread records. Failed metadata enumeration
stops the measurement rather than presenting an incomplete thread count.

## Optional frame capture

Add `--presentmon <path-to-PresentMon-2.6.0-x64.exe>` to capture frame intervals.
The exact standalone binary hash, Intel Authenticode signature and version
banner are checked before execution. No automatic download, installation,
privilege escalation, overlay or global collector service is involved.

The collector selects only the requested PID, disables input tracking, uses a
unique timed session and terminates only its own collector. Raw frame files
remain private. Summary records separate swapchains without exporting their
addresses. Missing CSV, event overflow and collector privilege warnings are
reported explicitly. Do not treat missing frames as zero FPS or use an
overflowed capture as a complete stutter measurement.

`etw_events_lost` also detects the distinct ETW loss warning, even without a
present-buffer overflow message. `capture_complete` is false for event loss,
overflow, truncated logs, unsuccessful collection or missing frame CSV.

`cpu_frame_rate_hz` is the producer rate derived from PresentMon `FrameTime`,
not monitor refresh or displayed FPS. CPU frame intervals include waiting and
are not CPU utilization; use process/thread CPU counters for utilization.
The summary includes p95/max intervals and counts above50ms/1000ms. Long stalls
are retained, including intervals longer than60seconds. Invalid rows are counted.

Foreground is recorded independently in every sample. The game reduces its
background frame rate to about 5 Hz in observed runs. A background measurement
must not be used to diagnose foreground menu performance. Menu camera and
day/night animation also vary between runs; keep the same scene when possible.

## Controlled two-core investigation, 2026-10-28

`native/build.ps1 -LocalNetworkPump -MaximumPhysicalCores 2 -OutputDirectory <new-folder>`
builds the two-core candidate. The default remains four for existing builds.
The selector respects the existing process allowance and chooses at most one
logical CPU per physical core, preferring higher efficiency classes on hybrid
systems. It does not continuously reassign threads each frame.

Only the isolated CPU copy was updated. PC1's two selected physical cores give
process mask0x5. A private comparison build omitted the affinity setters while
an identical launcher-imposed mask0x5 was inherited before startup in both
arms. Exact source/build differences and binaries remain in ignored `work/`.
The comparison build is not a distributable patch.

Three 20-second captures each had 20/20 foreground samples, process/bootstrap/hot
thread masks 0x5, Normal priority and no simulation turn advancement:

| Variant | Producer rate (Hz) | p95 interval (ms) | Max interval (ms) | Median CPU cores |
| --- | ---: | ---: | ---: | ---: |
| Affinity setters on, first run |120.681 |14.648 |691.252 |1.507 |
| Affinity setters off |63.628 |27.113 |32.880 |0.703 |
| Affinity setters on, repeat |63.609 |26.994 |30.836 |0.704 |

All collectors exited 0 with valid CSV and no event overflow. Their generic
privilege warning was retained. The first run contained four intervals above
50 ms. The longest was inside the capture, rather than its first frame. It had
about 3.7 ms GPU busy time versus a 691 ms CPU-frame interval. This points to
the CPU-side production path, including possible waits or scheduling; it does
not identify a busy game function. The later on/off runs were effectively equal.
This does not establish
that the affinity patch causes the user's intermittent stalls, nor does it
rule out transient contention. The first run's different animated scene and
rate preclude attributing its increased CPU to the affinity setters alone.
The regular two-core candidate was restored after the comparison.

## Review fixes and unresolved causes

Verified source corrections from the parallel review:

- The service retains one maintenance timer wait instead of creating a losing
  delay per datagram during bursts. Closed sessions also release their cached
  peer context, permitting bounded reconnections without evicting active peers.
- The shared native owner-update wrapper forwards foreign-thread calls to the
  original callback without mutating main-thread bridge state or counters.
- MSBuild excludes private `work/` and `outputs/` trees, preventing recursive
  compilation/item discovery of copied runtimes and game experiments.

Validation: 8,758 service assertions; a 512-datagram timer regression that fails with
the old implementation; a 10,000-call real foreign-thread native test; 946 pump
bootstrap checks; 24 CPU selection checks; 18 connection checks. The 19 observer boundary
and derivation tests cover long stalls, incomplete enumeration, module alias
handling, thread reuse, fixed metadata output, and collector pinning.

These are real defects/fixes, not proof of the reported desync's cause. The
real PC1-guest desync at turn 2154 remains unresolved. The received LAN host
report contains no expiry/error explaining it. No suitable large endgame save
is available, so large transport-network stability has not been exercised.
Do not alter deterministic logistics settings or disable synchronization checks
based on this review.

A separate private loopback experiment shortened the existing20minute session
lifetime to 20 seconds to compare reconnection behavior. Both the old and fixed
service cases eventually crashed in the client; the fixed service did allow
reconnection first. The common crash cause is unresolved. Those shortened
lifetime builds must never be shipped or confused with the normal service.
The new review service/native candidates are not yet a qualified LAN package.

The automatic-LAN revision adds two further confirmed registry/lifecycle fixes:
confirmed guests are not subtracted twice from the host's ordered free-slot
report, and lease generations are held across synchronous context use/send
start, without retaining a lock over `await`. Focused regressions fail when
either old defect is restored. These are lobby/control fixes, not a proven
cause of the simulation desync.

Dynamic peer-file reads run on a bounded-lifetime native helper rather than
the game update thread. This avoids introducing filesystem stalls into that
thread on the workspace's slow HDD. The helper never calls game/SDK functions.
FP control-word/MXCSR observations are limited to20minutes/120,000 bridge
pumps and fixed deduplicated labels, without changing numerical state. The
automatic-LAN test candidate uses the two-physical-core build for this next
diagnostic stage; this is not evidence that two cores fix the original problem.

## Physical-core comparison with DXVK (2026-10-30)

On PC1's Ryzen 7 9800X3D (8 physical cores, 16 logical processors), increasing
the game allowance from two to four or eight physical cores produced no
demonstrated benefit in the tested 60-FPS configuration. Keep the current
two-core default pending evidence from a CPU-bound workload. This result does
not establish that two cores are optimal for every machine or scene.

Four fresh starts used the same separate Steam-derived test copy in the order
2, 4, 8, then 2 again. Windows processor-core topology supplied one logical
processor per physical core: process masks were `0x5`, `0x55`, and `0x5555` on
this particular machine. The mask was applied before game entry; initial and
final thread-affinity snapshots also excluded SMT siblings. These masks must
not be copied to other hardware. System-wide SMT settings were unchanged.

All runs used DXVK 3.1.1 x32, the Radeon RX 9070 XT, a 60-FPS cap, VSync,
Normal priority, 1920x1080 windowed output, the simple menu, and the same
graphics settings. The map scene loaded the existing WePlay graphics QA save
on Schlacht um Tanholm, with its initial city camera and no commands or camera
movement during capture. Each scene was measured for 45 seconds after settling.
Manual load/observation timing means the simulation did not begin each capture
at exactly the same turn. This is a developed city, not a large endgame stress
save. Graphics and CPU tests were serialized with the graphics chat.

| Physical cores / run | Scene | Update loop (Hz) | p95 interval (ms) | Max interval (ms) | Process CPU (core equivalents) |
| --- | --- | ---: | ---: | ---: | ---: |
| 2 / first | Simple menu | 59.998 | 16.836 | 19.362 | 0.337 |
| 4 | Simple menu | 59.997 | 17.179 | 19.623 | 0.358 |
| 8 | Simple menu | 59.998 | 17.123 | 19.988 | 0.350 |
| 2 / repeat | Simple menu | 60.000 | 17.596 | 19.967 | 0.323 |
| 2 / first | Tanholm city | 59.998 | 16.837 | 33.756 | 1.069 |
| 4 | Tanholm city | 59.999 | 16.945 | 19.706 | 1.260 |
| 8 | Tanholm city | 59.999 | 17.690 | 19.576 | 1.674 |
| 2 / repeat | Tanholm city | 60.000 | 17.144 | 19.575 | 1.032 |

These are engine screen-update intervals from guarded, passive reads of the
byte-identical Vision90 module, not presentation timestamps or simulation
ticks. The private observer pinned the Steam executable and owned process;
it did not use Ubisoft executable RVAs. The DXVK HUD separately showed about
60 FPS. Each capture observed all 2,700 consecutive update intervals, with
the game foreground throughout and no interval above 50 ms. One CPU core
equivalent means one second of process CPU time per wall-clock second.

The map's measured CPU consumption rose with the allowance, while the update
rate stayed capped. Background host load varied (approximately 2.4 to 4.1
logical-CPU equivalents across captures), and simulation timing varied, so
neither that rise nor small timing differences establish causation. The
eight-core run did not improve the p95 interval. Uncapped throughput and a
CPU-bound endgame remain unmeasured; the cap can hide additional headroom.
The short captures also cannot rule out the previously intermittent menu
hangs, qualify multiplayer stability, or establish a desync fix.

The four- and eight-core experimental adapters built successfully and passed
the focused CPU-selection, Steam-profile and bootstrap checks. Eight cores
required widening the limit only in an ignored private source copy; no product
limit, payload pin, release package or deterministic simulation setting changed.
All four games exited cleanly, their memory sentinels reported no pressure or
monitor failure, and the original test-copy adapter and bundle fingerprints
were restored and verified after the comparison. Both original installations
were left untouched. Raw captures and experimental binaries remain in ignored
local storage.
