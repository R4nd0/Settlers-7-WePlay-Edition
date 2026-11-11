# Gold local simulation candidate

Development checkpoint, 2026-11-10. Build with
`native/build_gold.ps1 -OutputDirectory <private-output> -LocalSimulation`.
The ordinary build excludes the candidate. Gold/History crossplay and release
admission remain unavailable. The separately enabled two-human Gold experiment
below now advances in an actual native network match.

## Two-human network experiment, 2026-11-11

With `-LocalSimulation -AccountLogin`, the additional process opt-in
`WEPLAY_GOLD_LOCAL_MULTIPLAYER=1` requires an independent loopback slot 2–9.
It permits the original Ready call's network argument only for the authenticated,
service-owned playing session: exact native types, state/mode 2, valid handles,
local index 1 or 2, and exactly two connected human rows with unique indices.
AI rows are excluded. Caller, thread, facade, backend, core, output and
reentrancy guards remain active. Native Ready/Advance results, readiness output,
network argument and LastError are forwarded; no turn or checksum is invented.

Forty additional fixture checks exercise invalid session/roster contexts, with
both local roles and original true/false results checked through the x86 ABI.
Ordinary, local-simulation, SDK-login and account builds retain their checks.

Actual DXVK High pair 96 used the regular source build, without diagnostic
network hooks. Both players logged in independently, joined Monhaim Valley,
propagated readiness and entered visible worlds. Read-only samples recorded
host turns 32 then 301 and guest turns 138 then 451, with corresponding native
core progress and empty controller queues. Samples were sequential, so these
numbers are neither a same-turn comparison nor a checksum equality claim.

An earlier diagnostic build ran both worlds beyond 3,600 turns. A host-built
residence completed and appeared on the guest, including its enemy-building
tooltip. A guest-placed foundation subsequently appeared on the host; its
completion was not observed. These command observations belong to that earlier
build, not the shorter regular-build run.

Closing the host exited normally; the remaining client terminated at the
retained two-human guard. Graceful peer departure, reconnect, AI, multiplayer
save/resume and sustained equal native checksums remain unqualified. Each
runner verified all 16 shared profile/save files unchanged. See the
[network simulation receipt](verification/gold-network-simulation-20261111.json).

### Native checksum comparison

Subsequent DXVK High pair 98 used the same regular simulation path with a
private observer on the native transport Send entry. It recorded only the
original end-of-turn scalar fields, then forwarded all five original arguments
and LastError. The native writer, packets, results and game state were unchanged.
The two clients independently produced equal type-1 native checksums for every
turn 0–2,768: 2,769 comparisons, zero mismatches. Type-zero records and the
initial sentinel turn were excluded. This is a bounded same-edition result;
other checksum types and longer games remain unqualified.

A new host residence foundation appeared on the guest with the original
enemy-residence tooltip. It remained unfinished because its entrance lacked
a road connection; this run does not qualify completed construction or delivery.
The multiplayer menu exposed no manual-save action. Both worlds continued
past turn 2,650, the host exited normally, and the guest stopped at the retained
peer-departure guard. Both runners again verified 16 unchanged shared files.
See the [checksum receipt](verification/gold-network-checksums-20261111.json).

## Two separate waits

Gold's launcher facade waits for its former controller before calling the
native message handler. The guarded local entry now calls that handler in an
owned single-player world. Its initial preparation follows the independently
inspected History local entry; Gold still calculates readiness and advances
the actual simulation.

That correction alone reached turn 241 and then stopped. Two logistics queries
created at turn 142 remained in the outgoing queue. Gold's readiness delay is
18 turns, and its old controller wait becomes blocking after another 80 turns.
This accounts for the observed threshold: `241 - 18 - 142 = 81`.

The queries resolve to native `ELLog::CRequestTask` objects. Gold already
contains the transport searches; History calls them locally where Gold asks
the former controller to supply a result. The candidate enters after Gold's
original owner lookup and preferred-parent checks, then performs the existing
transport search, evaluates the alternative cost, and invokes the fallback
when no target was found. It retains the original result consumer and native
simulation clock. It does not turn a queued request into an invented reply.

## Guards and fixture checks

The candidate requires the pinned Gold executable and independently checked
native entries, caller bytes, vtables and object layouts. Runtime checks require
the owning main thread, active single-player state, absent legacy controller,
matching facade/backend/core, writable result storage and an idle request task.
Network calls outside the separately enabled two-human experiment and unexpected
contexts terminate the owned candidate process.

Authored x86 fixtures exercise stack/register preservation, native true/false
propagation, existing/primary/cheaper-alternative/fallback result selection,
changed entry/caller rejection and transactional restoration. Thirteen fatal
context checks pass. Both candidate and ordinary builds pass the existing
75 network guards and Gold-specific import/loopback guards, with no compiler
warnings or errors. The 35 Gold Python tests include actual Lua 5.1 execution
of the profile fallback and preservation checks.

## Observed game result

A separate, backed-up Gold copy ran with the corrected executable, native
startup and pinned DXVK at 1920x1080/high on the selected dedicated GPU.
The diagnostic launcher initialized before the game entry point and restored
that entry; Frida was not used.

On Monhaim Valley with three normal AI opponents, native observations recorded
turns 321, 1,039, 3,862 and 12,304. Both controller queues remained empty and the native
error flag stayed clear. The UI showed ordinary resources/population and AI
trading, research and conquest notifications. A newly written save contains
`LogicTurn=2667`; this is serialized simulation progress, not just a display
counter. These observations establish progress beyond the prior waits.

This diagnostic interval exited normally. All 16 shared profile/save files
retained their file set, hashes and timestamps. After restarting a client
with the [native graphics port](GOLD-GRAPHICS-PORT.md), the new turn-2,667 save
loaded and advanced through turns 2,731, 3,881 and 7,920 with empty controller
queues and a clear native error flag. That interval also exited normally with
all 16 shared files unchanged.

The local tester subsequently built and confirmed completed construction and
building-material delivery. Completed buildings and population capacity 27
were observed, and the resulting turn-11,982 checkpoint loaded after restart.
This is a single-map check; additional maps and prolonged economic operation
still require actual-game checks. The fixture suite does not establish those
outcomes. A separate guarded [offline cloud-upload correction](GOLD-OFFLINE-SAVING.md)
also passes actual manual save, autosave and reload checks.

## Other corrections in this checkpoint

The DXVK factory probe now accepts multiple display adapters. The previous
exactly-one requirement misclassified the two-display configuration. Actual
Gold device creation requires display enumeration on the diagnostic PC.

An empty title catalog now selects the neutral "No Title" entry for a new
profile, allowing Continue without a manual card click. Existing profiles and
earned-title catalogs retain their selection. This archive edit has fixture
coverage; deployment/runtime qualification is tracked separately.

See the [machine-readable receipt](verification/gold-local-simulation-20261110.json)
and [remaining integration plan](GOLD-EDITION.md#remaining-implementation-and-acceptance-plan).
