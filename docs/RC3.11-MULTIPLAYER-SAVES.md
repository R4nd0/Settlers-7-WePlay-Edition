# WePlay RC3.11: multiplayer saves and host autosave

RC3.11 adds saving and continuing multiplayer matches, including the human
slots, teams, start positions and AI settings. The host automatically saves
every five minutes of running game time. The automatic slot is separate from
manual saves, and manual saving does not postpone the next autosave.

## Save and continue

1. The host can use **Save game** in the in-game menu. Autosave also runs
   automatically while the multiplayer match is active.
2. To continue later, start WePlay again. The same host opens **Multiplayer →
   Load game** and chooses the automatic or manual checkpoint.
3. The other human players join that session in their original slots and
   select **Ready**. Wait for the host's **Start** button to become available
   while the actual checkpoint is transferred, then start the match.

Keep the host's game profile and save data together. This version uses the
host profile's checkpoint index as well as metadata embedded in the save;
copying an individual `.s7s` file into another profile is not supported.
Older saves without the required WePlay metadata and Map Forge templates
are rejected. The latest autosave replaces the previous automatic slot;
use a manual slot to retain an additional checkpoint.

## Included corrections

The native sender uses bounded, paced fragments for checkpoint transfer.
Readiness remains blocked while its actual send queue is pending, and the
lobby refreshes readiness when the transfer completes. This preserves the
game's checksum checks and does not acknowledge missing checkpoint data.
Ubisoft Connect/Uplay and Steam use separately guarded native addresses.

The RC3.10 logistics crash correction, lobby-alliance correction, multiplayer
castles and standalone startup fixes remain included. Steam still requires
its running client and each player's own supported installation. Install
RC3.11 on every participant's PC. Network revision remains 7.

## Verification

The tests used two actual isolated game processes and two AI opponents:

| Setup | Actual host autosaves at world turn | Common native replay checksums |
| --- | --- | --- |
| Fresh Ubisoft pair | 3001, 6001 | 6830, all matching |
| Ubisoft pair after full restart, loading turn 6001 | 9002, 12002 | 7112, all matching |
| Fresh Steam host with Ubisoft guest | 3001 | 3860, all matching |
| Steam host with Ubisoft guest after full restart, loading turn 3001 | 6002 | 4291, all matching |

All four runs closed both clients normally with exit code 0. Actual save files
contained the human/AI roles and teams; guest autosave files were unchanged.
The loaded worlds advanced with the original human roles and both AI players,
and the guest's interface remained usable. Early Ready/Start attempts were
held during transfer; readiness refreshed automatically afterwards, including
with Steam hosting.

The two-process trials used laboratory executable identities and the matching
checkpoint implementation. Original-executable release-profile guards and
native ABI/rollback behavior were checked separately with synthetic fixtures.
Native aggregate checksums cover selected simulation state, not every byte.
One memory monitor stopped during a Ubisoft lobby/loading phase; uninterrupted
memory monitoring across all test runs is not established.

These are local two-process tests. Separate physical PCs, Internet play,
arbitrary old checkpoints and extended/endgame stability require additional
qualification. Intermittent problems with an older checkpoint remain under
investigation; this release does not claim to eliminate every desync.

## Package

Version `2.0.0-rc3.11`, preview sequence 10, network revision 7. The ZIP contains
Setup, checksums, the signed update manifest, these notes and the corresponding
EasyTier source. Setup installs or upgrades the normal WePlay application.
No original game files, personal profiles, saves or diagnostic recordings are
included. Standalone exports remain a separate operation using the player's
own prepared game copy.
