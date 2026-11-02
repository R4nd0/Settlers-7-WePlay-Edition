# Logistics priority crash correction (RC3.10)

The longer RC3.9 verification match ended with both Ubisoft clients reading
through a null pointer in a native logistics query. Their replays contained
17,987 matching native end-of-turn checksums before the crash. The crash dumps
identify the same instruction and rejected priority identifier on both clients;
this was a separate failure from the corrected lobby-alliance desync.

## Native failure and correction

The query looks up a transfer's priority using a slot and generation. When
either the slot is outside the table or the generation no longer matches,
the original code sets the result pointer to null and then calls through it.
The preserved register value identifies this rejection path in the dumps.
A neighbouring native query already skips missing priorities.

The correction redirects those two existing rejection branches to the query's
existing next-entry loop. Valid priorities, the original comparisons, output
flags, list ownership and cleanup stay under native handling. It adds no
hook, allocation or game-thread callback and changes no clock or checksum
handling. It does not modify transfer records or serialized saves/replays.

The two branch displacements are independently mapped for the supported
Ubisoft Connect/Uplay and Steam History Edition executables. Installation
requires the existing executable fingerprint and original instruction guards,
plus an additional ten-byte guard covering the next-entry destination before
any patch is written. The correction is restricted to local network-pump
startup. Original installations are preserved.

## Verification

The unchanged original recording was replayed from its beginning with the
correction. Native playback ended at turn 17,988 and ordinary simulation
continued beyond 18,300. The resulting native recording contains 18,704
end-of-turn checksums; all 17,987 shared with the original recording match.
Early and later read-only RNG observations also match the original match.
The game closed normally with exit code 0, without a new desync save. Its
memory sentinel completed without a monitor error or pressure flag.

A new live match used a Ubisoft host and an actual Steam guest, after a
natural lobby/team reset, on the original test map with the same two AI
opponents. Their recordings contain 21,482 and 20,525 native end-of-turn
checksums: **all 20,525 common values match**. Both passed the earlier crash
region and closed normally with exit code 0, without a new desync save.
The loaded correction bytes were independently verified in both processes.

That live match is a regression test, not an exact replay of the old match.
Its recorded interturn events first differ at zero-based ordinal 12,769,
followed by a different reference checksum at ordinal 12,770. The two live peers agree
throughout. The unchanged replay above provides the comparison with the
original recording.

Both original live memory sentinels failed during the run, without a preceding
pressure flag. An additional read-only probe subsequently worked, but cannot
fill the monitoring gap or establish its cause. This run therefore does not
qualify continuous memory monitoring. The replay sentinel completed normally.
The correction is included in [RC3.10](RC3.10-LOGISTICS-CRASH.md).

Synthetic verification executes the actual original and corrected x86
conditional branches for both profiles, including valid IDs, expired
generations and out-of-range slots. Each network-pump build passes 112 branch
checks. Bootstrap checks pass 2,209 in the pair laboratory build and 2,178 in
the normal network-pump build; Steam profile checks pass 1,584 and 1,565.
Corrupting any byte of the destination guard rejects installation before
writes. The build without the network pump passes 701 bootstrap and 603 Steam
profile checks, with the correction disabled. All three variants build with
warnings treated as errors.

Loading an autosave created during an earlier replay did not reproduce the
original match's random state. That attempt is excluded from faithful replay
verification; subsequent verification uses the unchanged original recording
from its beginning.

Two physical PCs, Internet sessions, multiplayer save/reload and arbitrary
endgame stability require separate qualification. Native aggregate checksums
cover selected simulation state rather than every byte of the world. RC3.9's
published packages retain the limitations in its release notes.
