# NEO-Logistics: FIFO transport experiment

This candidate changes warehouse transport selection to use the transport
request's creation turn. It does not change production scheduler ranks or
pathfinding. Automatic ranks inherited from older production roots no longer
let newer, ordinary transport requests overtake older requests.

The separate [distance investigation](NEO-LOGISTICS-DISTANCE.md) records the
requested nearest-source policy and the native route-cost findings. No distance
override is currently enabled.

The existing scheduler can prefer a younger transport request because its
production root is older. The isolated replay demonstrated this at source
warehouse 65547 while requests for workshops supplied through warehouse 65569
continued waiting. This establishes an order-selection problem, rather than
proving an unreachable route or infinite starvation.

## Explicit priorities

Human and AI task-priority entry points populate a private intent ledger.
Automatic initialization calls do not count as explicit priorities. A ledger
entry matches the full task handle, creation turn and object address, and
intent is inherited through a checked parent chain. Native rank ordering is
retained within explicitly preferred/deferred classes. Equal-age ordinary
requests retain the native stable list order.

The group-aware variant also retains a deliberately changed group permutation.
An unchanged default order does not override FIFO. The comparison variant
ignores group permutations while retaining explicit individual priorities.
Unsupported task kinds and incomplete ancestry receive conservative treatment;
they are not evidence of fully qualified FIFO behavior for every native task.

## Laboratory build

```powershell
./native/build.ps1 -OutputDirectory work/neo-fifo-build -LocalPairLab -NeoFifoLab -MaximumPhysicalCores 2
```

`-NeoFifoLab` requires an isolated local-pair build and rejects `-GameDirectory`.
Only the pinned NEO executable digest enables the three native hooks. Ordinary
builds exclude the hooks and retain their existing executable acceptance rules.
Set these variables only in the owned test child's environment:

| Variant | `S7_NEO_FIFO_ENABLE` | `S7_NEO_FIFO_GROUPS` |
| --- | --- | --- |
| Control | 0 | 1 |
| FIFO with explicit group priorities | 1 | 1 |
| FIFO with individual priorities only | 1 | 0 |

The experiment retains native ordering until simulation turn 58000. It requires
a newly observed world at or before turn 1000. The intent ledger is not saved,
so this is not a fix for resuming existing saved games. No Steam, multiplayer
or general release qualification is claimed.

The x86 bridge preserves registers, flags, floating-point state, stack and
last-error state. Executable, callsite, object type, owner thread and full-handle
guards remain enabled. Native priority functions continue executing normally.

## Comparison method

The pinned Grassland Mesa replay runs with three hard AI opponents at requested
20x speed on an invisible desktop with an active renderer. This is not a true
headless game. The original installation is preserved.

Primary measurements cover turns 60000–110000 and fixed workshops 131162,
65648 and 65649. Their identities, warehouse association and coverage must match.
Worker states estimate time waiting for goods and time refining. Stable external
snapshots measure pending request age at source warehouse 65547. Physical cargo
pickups and warehouse arrivals are observed lower bounds, not inferred from a
request disappearing. UI state sampling does not establish exact completion
times.

The native production history supplies a separate whole-blue-player production
comparison. Its bucket counters are not cumulative. Completed-request age alone
would be misleading: clearing an old backlog can increase that age while
improving service.

All game assets, raw process observations, replay copies and runtime configuration
remain in ignored private work directories. Only source, tests and summarized
findings belong in Git.

## Observed comparison, 2026-11-05

All four isolated runs completed past turn 110000 with exit code zero, no memory
pressure and no intent/comparator exceptions. Every run used the same executable,
adapter and observer archive. The first 3578 priority events before turn 58000
match exactly. The first 95 production-history buckets also match across the
three captures. These checks do not establish a complete native world CRC match.

| Measurement | Native control | FIFO with group priorities | FIFO without group priorities |
| --- | ---: | ---: | ---: |
| Worker time waiting for goods | 75.50% | 72.26% | 50.08% |
| Mean age of pending source requests, game minutes | 15.85 | 7.00 | 3.91 |
| Maximum observed pending request age, game minutes | 51.81 | 30.44 | 24.43 |
| Observed physical arrivals for the fixed workshops | 0 | 0 | 11 |
| Blue-player production, complete buckets 100–179 | 2664 | 2677 | 2989 |

Worker/backlog/arrival figures use probes 28, 29 and 30. Production uses the
additional native-control probe 31 and the same two FIFO runs, summing 80 complete
one-minute history buckets, corresponding to turns 60000–108000. The sum counts
concrete produced goods, not aggregate food aliases or current-bucket samples.
This is a count of units, not an economic value or a per-building output measure.
The production mix changes; individual products need not increase.

The repeated control measured 75.13% goods waiting, mean pending age 16.00 minutes
and one observed arrival. Arrival counts are sampling-dependent lower bounds;
zero observed arrivals does not mean zero actual deliveries. All fixed building
profiles, positions and warehouse associations remain unchanged, with at least
99.99% observation coverage. Transient unstable external snapshots are discarded.

Without group priorities, goods waiting falls by 25.42 percentage points (33.67%
relative), and total produced units rise by 12.20% over the production control.
With group priorities, waiting falls by 3.25 points (4.30% relative) and produced
units rise by 0.49%. Explicit individual human/AI priority handling remains active
in both FIFO modes. The group-aware mode also retains deliberate group ordering.
This supports the group-free variant in this scenario, not a universal result
for every map, AI strategy, product or saved game. The fixed cohort contains a
butchery and two gatherer workshops; this is not a separate reproduction of the
initially reported weapon-smith wood shortage.

The new suites pass 1135917 policy checks (including every group permutation),
19 integration checks, 1664 comparator ABI checks, 13 priority-capture checks and
2176 priority-entry ABI checks. Existing bootstrap, Steam, logistics and terrain
tests pass in the relevant configurations. Normal and existing laboratory
bootstrap preprocessing matches the previous source when `NeoFifoLab` is absent.
The integrated opt-in bootstrap preprocesses identically to the private candidate
used in the live tests. Neither original game assets nor a release package were
changed. Each run's baseline files and timestamps were restored exactly.

Local evidence and finite replay runners remain under the ignored directory
`work/carrier-routing-investigation/NEO-Logistics`, probes 28–31. The compiled
candidate remains laboratory-only, with the fresh-world restriction above.
