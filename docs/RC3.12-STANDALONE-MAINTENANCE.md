# RC3.12 - Standalone maintenance and taller launcher

RC3.12 lets WePlay maintain its own prepared Ubisoft Connect/Uplay or Steam
History Edition copy after the original installation is removed. The launcher
also uses more vertical screen space so its menus fit without scrolling on the
tested display.

## Changes

- Detection selects your owned WePlay game copy for updates and repairs. It no
  longer asks for the original game folder when local repair inputs are valid.
- New copies retain the exact original executable and interface archive in
  `RepairSource`. Generated interface, adapter, service, launcher and runtime
  files are rebuilt from the verified package and these local inputs.
- Existing copies can recover the original interface from `ArchiveStage`;
  recovery must match the exact supported original SHA-256.
- Repair preserves saves and compatible preferences. A replacement is verified
  and published before old game files are cleaned up. Private standalone exports
  remain relocatable after a managed repair or game-content update.
- A launcher update can open the owned standalone copy's repair view before its
  game files are rebuilt; the old game is not silently marked as updated.
- The launcher starts taller and expands to fit the selected menu, including
  advanced options, within the monitor's usable height. Manual resizing is
  respected; smaller displays retain scrolling.

Keep `RepairSource` with the prepared game. Missing or damaged unchanged game
assets still require a healthy private backup or original source: the public
patch package contains no game assets. Steam still requires its installed,
running client and normal ownership check. Original installations are never
modified by preparation.

## Verification and limits

The final installer/backend/export suite passed 90 tests, covering source
removal for both editions, generated-file repair, save preservation, relocation,
legacy archive recovery and corrupt-backup rejection. A broader regression run
passed 184 tests with 19 optional platform checks skipped.

Bounded content checks using the actual production executables, Steam API and
real interface archive passed detection, package-change repair and save-file
preservation with original-source access forbidden. These were content checks;
no new game was launched. Two attempted full-size copy checks were stopped
because of slow I/O and do not count as completed verification.

The bilingual launcher self-test passed its 16 checks and private IPC round trip.
All six menus fit without vertical overflow at the tested available window
height of 1,376 pixels; the minimum-window fallback also rendered successfully.

The native adapter and simulation patches are unchanged from RC3.11. Its prior
autosave/resume, desync, crash and multiplayer qualification limits still apply;
this release makes no new gameplay-stability claim. Use the same WePlay version
on all participating PCs.

## Package

Version `2.0.0-rc3.12`, preview update sequence **11**, network revision **7**.
The ZIP contains Setup.exe, checksums, signed update metadata, these notes and
the corresponding EasyTier source. Proprietary game files, saves, identities and
machine-specific state are excluded. The update manifest is signed; Setup.exe
has no Authenticode signature.
