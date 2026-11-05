# RC3.15 — NEO-Logistics and Resume Fixes

Experimental prerelease, 2026-11-05. Version `2.0.0-rc3.15`, preview sequence 14.
Install this same RC on every participant's PC, even with NEO off. The existing
network revision is 7; the additional NEO session carrier is NEO3 and rejects
older NEO adapters.

## Included

- **NEO-Logistics**, initially off in S7 map setup, for solo skirmishes and
  multiplayer. The host chooses it; it stays fixed for the match. Supported
  warehouse requests use FIFO by creation age. Group priorities are ineffective
  and their controls disabled; explicit player/AI individual priorities remain.
- **NEO saved priorities restored:** the multiplayer loader now reads the chosen
  save's bounded cache/info records and carries the complete state to both peers.
  It validates task handles, birth turns, persisted/next turns and actual start
  state. Missing enabled state is rejected; save-provided Lua is never executed.
- **Resumed-world hang correction:** extreme finite AI footprint angles are
  reduced safely before native geometry/raster traversal. Normal angle bits,
  owner/ABI guards and native desync checks remain intact.
- **Invite / Einladen:** replaces the unavailable/crashing Ubisoft overlay with
  launcher invitation guidance/copying. It does not post or join automatically.
- Includes the global public directory, private invitations, EasyTier consent
  and isolation, standalone recovery, multiplayer castles and five-minute autosave.

## Known issue: later multiplayer reload desync

**Full resumed multiplayer stability is NOT established.** A later native desync
occurs after loading a newer turn-1200 checkpoint. A controlled run of the exact
same native world with NEO off matched 427 checksums, then differed on two.
The cause is unproven; this is not proof of independence from all new shared code.
Back up the whole host profile/save data and retain timestamps before updating.
Do not use this RC as a guarantee of uninterrupted long multiplayer sessions.

## Verified scope

- Solo: Grassland Mesa, three hard AI, native x20, start/save/restart/load with
  NEO on and off. Final on-run restored all 25 individual intents exactly.
- Two local Ubisoft clients plus two hard AI restored both four and 13 saved
  intents exactly. Final old-checkpoint resume reached turn 1600 on both, saved
  again at 1200, exited normally: 600 common complete native checksums, no mismatch.
  This does not qualify another reload or later play.
- Separate terrain-fix run: two local clients plus two AI, actual guard execution,
  autosave at turn 15003, normal exits, 3924 matching common native checksums.
- Native mapping/ABI/codec, Lua lifecycle and installer fixtures pass. These are
  separate from actual gameplay evidence. No new delivery-speed gain is claimed.

Separate-PC/Steam NEO resume, populated human individual controls, final-policy
delivery benchmarks, Internet/endgame stability remain unqualified. Faster
checkpoint pacing remains a development opt-in; lobby transfer can take minutes.

## Install, update and recover

Run Setup.exe or extract the ZIP first. Close S7/WePlay; select the existing app
folder for an upgrade. For a complete standalone export, use “Select existing
standalone copy” and select its outer/App folder. A retained owned copy updates
without its original game directory. Python/.NET are bundled; Steam still needs
its client. Public downloads contain no commercial game, profiles or saves.
Use Windows Installed apps to uninstall a normal installation; back up personal
data first. Rerun Setup at the same folder for recovery; do not mix RC files.

README, licenses (including puff/S7Packer), EasyTier corresponding source and
SHA-256 checksums accompany the release. Setup updates use signed metadata;
Windows executables remain without a trusted Authenticode publisher signature.

[NEO details](NEO-LOGISTICS-MAP-OPTION.md) · [Terrain fix](MULTIPLAYER-RESUME-TERRAIN.md)
