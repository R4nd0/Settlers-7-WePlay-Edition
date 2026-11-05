# RC3.15b — Workshop Resume Fix

Experimental prerelease, 2026-11-05. Version `2.0.0-rc3.15b`, preview sequence 15.
This is a new release/tag. RC3.15's published files and tag remain unchanged.
Install RC3.15b on every participant's PC; do not mix its adapter with RC3.15.
Network revision remains 7, with the existing NEO3 session carrier and native
content-identity checks.

## Fixed: the reproduced later multiplayer-load desync

A resumed workshop-AI search used an uninitialized seven-angle constant table.
Host and guest read different allocator leftovers and built different candidate
lists. The search constructor and native save visitor omitted those invariants;
only fresh searches performed their normal initialization. This explains the
reproduced turn-1200-save failure, including the NEO-off comparison.

The guarded fix calls the original constructor, then initializes only the seven
invariant angles. Saved search work, phase, candidates, priorities and RNG are
retained. Original executable, callsite, owner/type and ABI/FP guards remain;
native desync detection is not suppressed. The extreme-angle guard also remains.

## Verification

Three completed runs used two local Ubisoft clients on Grassland Mesa with two
hard AI opponents:

- NEO on, loading the previously failing turn-1200 checkpoint: both restore all
  13 individual intents exactly, pass the former failure at 1628, save at 1850,
  reach 2100 and exit normally: 600 common complete native checksums, all matching.
- Cold-load that new turn-1850 save: all 18 intents restore exactly on both peers;
  save at 2500, reach 2750, normal exits: 550 common checksums, all matching.
- NEO off with the original native world/info: reach 2100 and save at 1850,
  normal exits: 600 common checksums, all matching.

Total: **1750 common complete native checksums, zero mismatches**. Inputs,
profiles/timestamps, adapters and runtime configuration were restored; allocation
and address-space monitors stayed healthy. The 331-check constructor suite covers
Ubisoft and Steam layouts, exact field bounds, return ABI/FP state and rollback.

These results qualify the reproduced failure and bounded repeated-load tests.
They do not establish Steam live resume, separate-PC or general long-match
stability. Older PC2-host desync reports are not all explained by this fix.
Populated human individual-priority controls and delivery benchmarks of the
final NEO policy remain unqualified. No new delivery-speed gain is claimed.

## Complete RC3.15 feature set retained

NEO remains an optional, initially off **map setup** choice for solo/multiplayer.
The host chooses it; it is fixed for the match. FIFO follows order age; group
priorities/controls are disabled while explicit player/AI individual priorities
remain effective and saved. The global public lobby, private invitations,
EasyTier consent/isolation, in-game Invite replacement, multiplayer castles,
manual saves, five-minute host autosave and standalone/update recovery remain.
Checkpoint transfer keeps normal pacing and may still take minutes.

## Installation and recovery

Close S7/WePlay and run Setup.exe, or extract the ZIP first. Update an ordinary
copy at its existing app folder. For a complete standalone export, use “Select
existing standalone copy” and select the outer/App folder; its owned game can
update without the original directory. Keep whole profiles/save folders and
timestamps backed up together. Python/.NET are bundled; Steam still needs its
client. Public downloads contain no game files, profiles or saves.

The release includes README, notes, licenses (puff/S7Packer), matching EasyTier
source, SHA-256 checksums and signed update metadata. Windows executables remain
without a trusted Authenticode publisher signature. Rerun Setup for recovery;
do not mix individual RC files.

[Root cause and evidence](MULTIPLAYER-RESUME-WORKSHOP.md) ·
[NEO behavior](NEO-LOGISTICS-MAP-OPTION.md)
