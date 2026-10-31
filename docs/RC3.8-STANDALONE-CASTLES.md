# WePlay RC3.8 — Standalone and castle consistency

RC3.8 integrates the standalone startup correction for the identified Ubisoft
Connect/Uplay and Steam History Edition executables. The obsolete Ubisoft
launcher API remains explicitly unavailable; the guarded startup path no longer
requires its old registration. Steam still uses its genuine SDK and normal
ownership check, with the Steam client running.

A complete prepared standalone copy can be moved without another source-game
installation. The relocation helper also recognizes a complete newer ZIP
extracted over its contained instance, verifies the supplied game/runtime and
refreshes the package binding. Known leftover DXVK files are recovered with a
preserved local backup. Supported saves and settings remain in place. Private
exports contain proprietary game data and must not be uploaded publicly.

The castle transfer now supplies the hooked local construction event through
the same bounded representation used for peers. Omitted castle names and
over-budget designs therefore have consistent event arguments. Each outgoing
join request retains its captured design through deferred sends and copies.
The existing wire format and network revision 7 remain unchanged; all players
still need the same RC3.8 package and its matching content fingerprint.

## Verification and limits

The synthetic castle regressions and real two-client qualification are recorded
in [MULTIPLAYER-CASTLES.md](MULTIPLAYER-CASTLES.md). The earlier reported LAN
desync at turn 461 is the motivation for this follow-up; a bounded local pair
test cannot prove the cause of that physical-PC failure or guarantee long-match
synchronization. No checksum or game-start check is suppressed.

Two freshly started local Ubisoft/Steam clients joined and played Feuerschlucht
with a normal AI, with Ubisoft hosting to turn 3489 and Steam hosting to turn
2858. Both clients held identical complete castle values for the distinct
106-part and 25-part human designs and the AI design. Both castles were visible
from the other client; host construction completed in both directions. All
four games exited normally. The memory sentinels recorded monitoring failures,
so the result qualifies the observed castle/gameplay paths, not clean memory
monitoring or long-run stability.

Original executable fingerprints, native callsite/ABI/object guards and
rollback checks remain mandatory. The production package excludes laboratory
executables, profile fixtures, proprietary game files and identities. Native
graphics, DXVK, two physical cores without SMT siblings, automatic LAN and
private Internet rooms retain their existing settings and qualification limits.

Version `2.0.0-rc3.8`, preview update sequence 7, network revision 7. Setup is a
normal WePlay application installer; a private full standalone export is a
separate user-owned artifact. This build does not publish a new GitHub release
or redirect the public update feed by itself.
