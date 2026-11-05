# Resumed multiplayer: uninitialized workshop search rotations

The later desync reported for RC3.15 has a concrete native cause. Resuming an
active `EAI::CPlanPlaceWorkshop` search uses seven uninitialized rotation values.
The original constructor leaves this array untouched, and the native save visitor
does not serialize it. A fresh search initializes it to 0, 30, 60, 90, 120, 150
and 180 degrees; a restored search already in progress skips that initialization.

## Evidence

Two isolated Ubisoft clients resumed the same Grassland Mesa checkpoint at
logic turn 1200, with two human slots and two hard AI opponents. The original
NEO-priority restoration succeeded on both clients. The complete native replay
streams then matched for 427 checksum records and differed in the following two;
both generated native desync saves at logic turn 1628. An earlier controlled
NEO-off run also failed at that point. Disabling FIFO does not correct the native
AI initialization.

Bounded scalar-only diagnostics identified the earlier divergence. On the first
resumed AI update, the rotation arrays already differed between processes and
contained unrelated allocator contents, including text bytes. The native search
then accumulated different candidate counts at turn 1216. The RNG recordings
matched throughout the early candidate divergence and again through the final
desync window; the later run recorded 2545 identical RNG rows at turns 1580–1629.
The missing initialization is directly visible in the independently inspected
constructor, save visitor and fresh-search initialization for both executable
layouts. No instruction dumps, game data or private recordings are distributed.

## Correction and guards

The guarded factory call runs the original workshop-plan constructor, then
initializes only its seven invariant rotations. It retains the saved phase,
queued search work, candidate list, chosen candidate, resources and priorities.
It does not restart or skip the search, alter RNG, or suppress desync detection.
Fresh searches later write the same constants through their original code.

`workshop_angles_bridge.inc` is included only with the native network pump. It
requires completed initialization, the established owner thread, local-connect
mode and the exact workshop-plan vtable. The original executable hashes,
edition-specific factory-call bytes and mapped constructor remain guarded.
The bridge preserves the constructor's return registers, flags, complete
x87/SSE state, LastError and original stack cleanup. It has no per-frame scan
or growing allocation.

The existing extreme-angle reduction remains a defensive guard. Initializing
the constants removes the demonstrated source of invalid workshop rotations.

## Verification and release status

The synthetic suite passes 331 checks across Ubisoft and Steam mappings. It
uses poisoned surrounding object memory and a deliberately clobbering helper to
verify the exact field boundary, native constructor invocation, return ABI,
floating-point state, negative owner/mode/type guards and patch rollback.
Bootstrap, Steam profile, directory, NEO saved-state and FIFO regressions also pass.

Three completed cold-start comparisons use the same two local Ubisoft clients,
Grassland Mesa and two hard AI opponents:

- The previously failing turn-1200 checkpoint restores all 13 NEO individual
  intents exactly on both clients. Both continue through the old turn-1628
  failure, save again at turn 1850 and reach turn 2100. Both exit normally.
  Their complete native replay streams contain 600 common checksum records,
  with zero mismatches.
- Cold-loading that new turn-1850 save restores all 18 intents exactly on both
  clients. They save again at turn 2500, reach turn 2750 and exit normally.
  Their complete replay streams contain 550 common checksum records, all matching.
- With NEO disabled, the original turn-1200 native world also reaches turn 2100,
  saves at turn 1850 and exits normally on both clients. Its complete replay
  streams contain 600 common checksum records with zero mismatches. Only the
  private NEO cache choice changes for this control; native world/info bytes
  are preserved.

All three owning runners restore the input copies, profile files and timestamps,
adapters, interfaces and runtime configuration. Allocation/32-bit-address-space
sentinels remain healthy. A longer initial run also produced identical observed
AI and RNG traces, but its forced timeout left incomplete replay streams; it
is not counted as checksum evidence.

Actual Steam resume, separate PCs and arbitrary long matches are separate
qualification work; synthetic layout/ABI checks do not establish those results.

RC3.15's already-published installer and ZIP predate this correction. They are
not modified in place. RC3.15b includes the new verified adapter;
all participants need matching builds, including the same RC3.15b revision.
Adapter fingerprints are part of the
existing network content identity.

[Structured verification receipt](verification/workshop-resume-20261105.json)
