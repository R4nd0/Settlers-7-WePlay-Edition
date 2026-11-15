# Gold/History movement precision, 2026-11-15

The experimental Gold arithmetic port passed bounded local DXVK matches against
both original History executables. The previous repeatable first checksum
divergence at finished turn 1278 did not recur within either test:

| Original History peer | Common original checksums | Finished turns | Mismatches |
| --- | ---: | --- | ---: |
| Ubisoft | 3334 | 0–3333 | 0 |
| Steam, standalone native adapter | 3137 | 0–3136 | 0 |

A subsequent run consumed the exact extracted public component in the
experimental session adapter and doubled the UI watchdog to 600 seconds.
Gold/Steam produced **4862 consecutive common original checksums, finished turns
0–4861, with zero mismatches**. Both clients again exited normally and passed
strict restoration. See the [sanitized verification receipt](receipts/gold-history-movement-precision-20261115.json).

Both runs used isolated prepared copies, two human clients and two AI players on
Monhaim Valley, DXVK at High/1920×1080, automatic original payable construction
actions and a 300-second UI watchdog. Both clients exited normally with status
zero. Strict comparison verified restoration of profiles, shared state, archives
and adapters, with no game processes left running. NEO was disabled. A requested
5-ms turn duration did not establish effective x20: observed throughput remained
about 13 turns/second. These results do not qualify mixed saves/reloads, other
maps, sustained matches, separate-PC EasyTier play or complete game equivalence.

## Cause and controlled comparisons

Gold's original AStar pathfinder and combat movement use x87 intermediate
precision where History rounds the corresponding arithmetic to single precision.
Passive records showed different paths before later movement, animation, RNG and
original checksum differences. All affected sampled movement calls ran on the
main thread; a worker-thread explanation was not supported.

Disabling the existing History terrain-search and workshop-resume repairs in a
separate control run still reproduced the first divergence at turn 1278. Those
two repairs therefore do not explain this fresh-world failure. A separate Gold
workshop initialization port also failed to remove that divergence on its own.
Other History repairs have not all been independently excluded.

A CPU-only comparison executed the original Gold and Ubisoft AStar routines
over 96 queries on 32 synthetic grids, including multiple goals, obstacles and
reused native search caches. Stock Gold produced 21 different paths; scoped
single-precision Gold produced none. Imported memory operations were modeled;
the pathfinding routines themselves were executed unchanged. This comparison
uses synthetic grids, not a replay of a captured live map.

A further 192 queries on 48 larger synthetic grids (32×32, 48×48 and 64×64)
included dense obstacles, walls, multiple goals and starts already at a goal.
Stock Gold differed in 70 queries; scoped single precision again had zero
differences. These are additional CPU comparisons, not multiplayer map tests.

## Component and integration contract

`native/crossplay_gold_movement_precision.h` contains the extracted component;
its companion pins cover 42 original code/data regions. Ordinary Gold adapter
builds do not activate it. The private experimental session adapter supplies:

- Exact supported executable binding, authenticated world ownership and the
  already guarded combat-movement wrapper.
- A mandatory additional guard callback, checked before and after installation.
- The original world-thread readiness predicate and movement scope lifetime.

The component bounds typed actors, registry membership and full generation IDs,
grid dimensions, arrays, seed vectors, stack frames and nested scope depth.
Path construction may precede selecting the movement component; execution still
requires the selected component. Foreign threads retain native precision.

The original AStar function and movement function each execute once. The scoped
x87 control change rounds their intermediates to single precision; the original
CRT atan2 calls retain double precision. Native arguments, stack, registers,
flags, LastError and control-word restoration, including SEH unwind, remain
protected. Installation retains callsite/image checks, owned detour validation,
transactional rollback and executable-memory protection. Checksums, RNG state,
AI policy and game time are not replaced or fabricated.

The caller must maintain the canonical arithmetic for the complete owned-world
lifetime before production disconnect/resume support is enabled. The current
fresh-world qualification does not cover a transport loss.

## Verification

`native/build_gold.ps1` builds and runs `gold_movement_precision_tests.cpp`.
The fixture checks the custom ESI/ECX AStar ABI and four stack arguments,
original forwarding, GP/flags/stack/LastError, typed construction versus
execution, nested precision, genuine SEH unwind, CRT precision, foreign-thread
exclusion and ordinary-host/image rejection. It uses synthetic objects only.

An independent compiled guard probe passed 128 original-image mutation and
unreadable-region checks. The complete Gold build and existing network/ABI
fixtures passed on the host, including 75 network guard checks. The sandboxed
network fixture could not exercise host loopback and was repeated successfully
outside that restriction. Raw game data, local identities, binary images and
private traces are not included in the repository.
