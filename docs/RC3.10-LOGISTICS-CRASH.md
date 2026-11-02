# WePlay RC3.10: logistics crash correction

RC3.10 corrects the native logistics crash found during the longer RC3.9
test. A transfer query could reject a missing priority and then dereference
the resulting null pointer. It now continues with the next transfer entry,
using the existing native loop. The correction supports the fingerprinted
Ubisoft Connect/Uplay and Steam History Edition builds.

Executable fingerprints, original instruction checks and a new guard on the
loop destination protect installation. The game's checksum detection remains
active. See [native analysis and verification](LOGISTICS-PRIORITY-CRASH.md).

## Included corrections

RC3.9's lobby-alliance desync correction, the standalone startup recovery for
Ubisoft/Uplay and Steam, complete-ZIP relocation recovery and multiplayer
castle corrections remain included. Steam requires its running client and
the player's own supported installation. Install RC3.10 on every player's PC.
Network revision remains 7; the castle message format is unchanged.

## Verification and limits

The unchanged original recording completed with the correction and continued
past the earlier crash region. Its new recording contains **18,704 native
end-of-turn checksums**, with **all 17,987 shared original values identical**.
The game closed normally with exit code 0, without a new desync save, and its
memory sentinel completed without an error or pressure flag.

A new live match used a Ubisoft host and an actual Steam guest with two AI
opponents. **All 20,525 common native checksums match**. Both clients passed
the earlier crash region and closed normally with exit code 0, without a new
desync save. Its recorded inputs differ from the old match, so this is a
separate regression test rather than an exact reproduction.

Both live memory sentinels failed during play. Subsequent read-only memory
samples worked without pressure flags, but cannot fill that monitoring gap
or establish its cause. Continuous live memory monitoring remains unqualified.
Two physical PCs, Internet sessions, multiplayer save/reload and arbitrary
extended/endgame stability require separate qualification. Native aggregate
checksums cover selected simulation state rather than every byte of the world.

## Package

Version `2.0.0-rc3.10`, preview sequence 9, network revision 7. The ZIP contains
Setup, checksums, the signed update manifest, these notes and the corresponding
EasyTier source. Setup installs or upgrades the normal WePlay application.
Full standalone exports use each player's own game files and remain separate.
