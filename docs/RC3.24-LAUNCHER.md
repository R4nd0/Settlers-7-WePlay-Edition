# RC3.24 - Launcher Reliability

Install with **Setup.exe**, or extract **WePlay-RC3.24.zip** and run Setup.exe.
Linux users can extract **WePlay-RC3.24-Linux-x64.tar.gz** and run WePlay.sh;
see the [Linux guide](LINUX.md). The launcher includes all previous RC3.23
graphics and lobby corrections.

## Changes

- Public Join captures a fresh selection before starting. End/Cancel works while
  waiting in a public room.
- Interrupted settings saves have an explicit recovery screen: inspect a
  diagnosis, restore the previous settings with a verified backup, or set up
  WePlay again while preserving game copies and saves. Changed evidence is
  detected before recovery.
- Save/Discard keeps the committed settings even if hardware refresh fails.
  Unsaved graphics edits survive hardware refresh; long pages keep their primary
  actions visible. Unsupported Modern presets are disabled with an explanation.
- Lobby refresh no longer hashes the full package or blocks the main controls.
  Save shares one verified package context and one coherent display snapshot;
  integrity and launch-time hardware checks remain in place.
- ReShade preparation and update cleanup can resume after interruptions without
  deleting foreign files. Downloads have cancellation, progress and bounded
  transfer time. Update checks retain verified offers when a later candidate
  cannot be fetched, while preserving signature and rollback protections.
- A fresh independent review also closes malformed-journal startup recovery,
  abrupt partial-download cleanup and cancellation immediately before installer
  handoff. See the [release review](LAUNCHER-RELEASE-REVIEW-20261109.md).
- Backend requests have bounded messages and monitored lifetimes. Read-only
  helpers can be stopped safely; settings commits and running games are protected.
  Diagnostics report the actual version. Startup defers old-copy cleanup.

## Graphics and multiplayer

Genuine SMAA remains available independently; other effects are explicit choices.
RC3.23 camera-depth-of-field, filtering and local-renderer corrections are
included. Install the launcher update and use its game-copy update/repair when
offered. All participants should use the current release and matching content.

The gameplay content contract remains v3, directory protocol 7 and NEO protocol 5.
This release changes launcher reliability, not the game transport. S7 already
uses UDP; the [QUIC assessment](UDP-QUIC-ASSESSMENT.md) explains possible future
experiments.

## Verification and limits

The [launcher fix report](LAUNCHER-FIXES-20261109.md),
[release verification](verification/rc324-20261109.json) and publication receipt
record the tests and qualification boundaries. Final packages are checked before
publication, including isolated installer, bundled Python, UI/IPC, signatures and
Linux runtime/archive checks.

Remaining shadow flicker is not fully resolved; vegetation sharpness across all
scenes remains unqualified. No new FPS, separate-PC Internet, Steam/Proton or
Linux gameplay improvement is claimed. The previous mixed-renderer loopback
match is historical evidence, not a new match for this launcher release.
Executables are not Authenticode signed; update metadata is cryptographically
signed.

Graphics background: [visual-quality guide](VISUAL-QUALITY.md),
[measured RC3.19 investigation](GRAPHICS-RC319-MEASUREMENTS.md) and
[Internet join correction](RC3.21-INTERNET-JOIN.md).
