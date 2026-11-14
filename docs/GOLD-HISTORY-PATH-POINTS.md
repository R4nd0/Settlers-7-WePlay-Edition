# Gold/History animal path-point arithmetic

The opt-in laboratory adapter corrects the reproduced finished-turn-2792
divergence on the Monhaim pair fixture. Gold/Ubisoft subsequently matched 4,539
consecutive original turn CRCs; Gold/Steam matched 4,949. Both used DXVK,
standalone local sessions, automatic original Ready/Start controls and normal
payable storehouse/street commands from both players. Gold's loading overlay
remained visible in the Steam run: advancing simulation does not establish a
complete Gold UI transition. NEO and mixed save/reload remain unqualified.

Gold retains x87 intermediate precision while interpolating two points between
adjacent path nodes. Both original History routines round each operation to
single precision. A small point difference changed the original smoothing
choice; a tracked animal arrived two turns later in Gold. Its animation
transition and subsequent original random-number requests then diverged.

`native/crossplay_path_point_math.h` reproduces the History operation order with
scalar SSE intrinsics. `crossplay_gold_animal_path_points.h` corrects only the
temporary point before either original vector append. Search, allocation,
smoothing, animal state transitions, random generation and outgoing CRCs remain
original. Installation verifies full instruction regions and both call targets;
runtime guards check owner thread, active world/path system, animal object,
native stack/caller and finite bounded operands. The bridge preserves registers,
flags, floating-point state, stack balance and `LastError`.

The compiled bridge harness passed 42 accepted and eight excluded contexts,
covering both original append call sites and ABI preservation. Guard tests cover
25 full original regions and 77 mutation/unreadability checks, including the
existing movement-time guards.

`tools/gold/verify_path_points.py` executes the complete fingerprinted original
path-output arithmetic routines on a modeled x86 CPU. For 2,053 node-list cases
and 18,458 points per History edition, the compiled correction matches both
editions exactly; uncorrected Gold differs in 1,817 cases. Vector append and
temporary-point construction are modeled. This is an arithmetic oracle, not a
full native pathfinding test.

In each real pair, all four original build/street commands were sent and natively
enqueued once. Both new storehouses reached the original completed state on both
clients. No outgoing native desync was observed. Both games exited normally with
code zero and full profile/archive/adapter/shared-state restoration; preexisting
saves were not replaced. The 600-second application-frame exit deadline measures
elapsed runtime, not 600 seconds of simulated world time.

See the [bounded verification receipt](verification/gold-history-path-points-20261114.json).
Other maps, longer sessions, complete Gold UI entry, NEO policy/transport, mixed
save/reload and LAN remain unqualified. Ordinary launchers do not enable this
laboratory adapter; the private runtime and oracle are not release packages.
