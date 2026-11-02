# WePlay RC3.9: lobby desync correction

RC3.9 corrects a reproduced cause of LAN desyncs in the supported Ubisoft
Connect/Uplay and Steam History Edition profiles. After creating and leaving
a lobby, a guest could carry its previous AI alliances into the next host's
game even though the displayed team numbers had changed. The peers then
started with different diplomacy and eventually produced different simulation
checksums.

The incoming lobby roster now uses the game's existing reciprocal team update
to rebuild active-player relations. The correction is restricted to validated
preparation data. Original executable fingerprints, native callsite and object
guards, neutral relations and the game's desync detection remain in place.

The standalone startup recovery and multiplayer castle corrections from
RC3.8 remain included. Steam still requires its running client and the player's
own supported game installation. Upgrade every player to RC3.9; network
revision remains 7, and this release changes no castle message format.

## Verification and limits

The original failure was reproduced naturally with two local Ubisoft clients:
the guest retained the old mutual AI alliance, and both games generated desync
saves at turn 4778. With the correction, the same lobby transition produced
matching alliances and **17,987 common native turn checksums, all equal**.
The previously divergent AI camp upgrade also completed on both clients.

That longer run later ended with **both clients crashing at the same native
null-pointer access**, with no differing recorded checksum. Its relationship
to the adapter has not been established. This release does not fix that crash
or claim stable extended/endgame sessions. The final memory monitors reported
failure after the exits, so that run is not a clean-memory qualification.

An actual Steam-derived guest repeated the old-lobby setup and joined a
Ubisoft host. The stale alliance was removed, all observed player alliances
matched, and **6,586 common native turn checksums were equal**. Both games
closed normally with exit code 0. The Steam memory monitor failed during the
run, while the Ubisoft monitor completed; no pressure flag was recorded before
the monitor failure. This also is not a clean-memory qualification.

Two physical PCs, Internet sessions and multiplayer save/reload remain unqualified.
The native aggregate checks selected simulation state and is not a bytewise
comparison of the entire world.

## Package

Version `2.0.0-rc3.9`, preview sequence 8, network revision 7. The release ZIP
contains Setup, its checksum, the signed update manifest, these notes and the
corresponding EasyTier source. It contains no game data, saves, replays,
identities or laboratory binaries. Setup installs or upgrades the normal
WePlay application; user-owned full standalone exports remain separate.
