# RC3.16 — Map Variant Multiplayer Fix

Experimental prerelease, 2026-11-06. Version `2.0.0-rc3.16`, preview sequence 16.
This is a new release and tag; all RC3.15b and earlier published files remain
unchanged. Update every participant to RC3.16, including with NEO disabled.
Network revision 7 and the NEO3 session carrier remain unchanged; matching
native and UI content is required on all peers.

## Fixed: lost Kartograph variant names and checkpoint loading

The original native template getter returned a pointer into a destroyed string.
Short variant names could disappear before Lua copied them. In the local
Grassland Mesa control, both session descriptors held the right name, but Lua
received nil: the host required 25 victory points while the guest loaded the
standard 6-point rules. Checkpoint metadata also lost the name. Longer names
depended on freed heap storage and could appear to work by accident.

The guarded bridge copies the live session name into stable adapter storage
for the original result constructor. Executable, callsite, object, thread and
connection guards remain active, with register and floating-point state
preserved. Simulation, victory-point rules and native desync detection are
not overridden.

The multiplayer save browser now validates and restores the saved template,
including its name in the native resume lobby. A missing host template is
reported before lobby creation. Standard-map saves retain their existing path.

## Verified locally

Two isolated Ubisoft clients, two hard AI opponents, NEO off, Grassland Mesa
with nine added sector victory points and 25 total/required points:

- Fresh match: both reach turn 3600; real save at turn 3000.
- Both processes fully restarted: load that checkpoint through the multiplayer
  save browser, continue to turn 6600 and save again at turn 6000.
- Both peers retain the correct template and 25 required points. Both saves
  contain the template name in their native save cache. Both runs end normally.
- **1200 common native checksum records, zero mismatches**, complete replay
  streams; isolated source copies restored and memory monitors healthy.

The actual rule mismatch was reproduced; the originally reported few-minute
native desync itself was not reproduced in this investigation. The cold-loaded
match passed six further game minutes. This does not establish live Steam,
separate-PC/Internet, extended-session or populated-human-command stability.
Steam has independent native mapping and synthetic ABI/guard coverage.

Keep identical templates on participating PCs. Automatic template distribution
or content comparison is not part of this fix. Names already lost in old save
metadata cannot be reconstructed automatically. Back up whole host profiles,
save folders and timestamps together before updating.

## Installation

Close S7 and WePlay, then run Setup.exe; extract the ZIP fully before using its
Setup. Update an ordinary copy at its existing app folder. For a complete
private standalone export, choose its outer/App folder using the standalone
selection. Python/.NET are bundled; Steam still needs its client.

The previous workshop-AI resume correction, terrain guard, NEO option, public
lobby, invitations, multiplayer castles, manual saves, five-minute host
autosave and standalone/update recovery remain included. Save transfer retains
normal pacing and can take minutes.

Downloads contain no game files, profiles or saves. The release includes
licenses, corresponding EasyTier source, SHA-256 sums and signed update metadata.
Windows executables have no trusted Authenticode publisher signature. Use the
complete release rather than mixing individual RC files.

[Cause, guards and measurements](MULTIPLAYER-MAP-VARIANTS.md) ·
[Previous workshop correction](MULTIPLAYER-RESUME-WORKSHOP.md)
