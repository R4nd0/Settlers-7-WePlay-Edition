# Gold offline cloud-upload wait

Development candidate, 2026-11-10. Manual saving, autosave, reload and clean
exit passed an actual local check. This is not full Gold release qualification.

With Gold's legacy Orbit SDK unavailable, local saves finish writing but the
game then waits for a cloud callback that cannot arrive. The actual isolated
client was observed in the upload method's polling loop at saved turns 5,667
and 11,667. It later recovered without termination. The second autosave is a
complete archive containing `LogicTurn=11667`; a subsequent manual save
contains turn 11,982. These are separate from the earlier turn-zero/logistics
corrections.

The independently mapped `OrbitClient` method at Gold VA `0x449F30` returns
void and removes two stack arguments. It requests the SDK context/callback,
pumps SDK updates and polls up to 5,001 times with `Sleep(2)`. With the
validated unavailable Orbit record and null facade context, no SDK request or
completion callback exists. The timeout remains reachable after the local
file has been written.

`native/gold_startup.cpp` replaces only its two direct calls at Gold RVAs
`0x185B` and `0x40F58`. The replacement checks the exact return address,
initializing thread, facade vtable, null context, owning Orbit record, its
unavailable flag and all 22 null function pointers. It returns from the void
upload operation without creating a callback or cloud success result. The
local writer, save archive and simulation remain with the original game.
Unexpected or SDK-connected contexts terminate the owned candidate.

The executable hash, both call instructions, original method entry, polling
instruction and `RET 8` epilogue are guarded. Both native build variants pass
stack/nonvolatile preservation and argument nonmutation fixtures for both
callers, 28 changed-byte rejections and six invalid runtime contexts. Their
existing graphics, local-simulation and network guards also pass. An
independent read of the original pinned Gold image matches all five sites.

The candidate client loaded the previous turn-11,982 building checkpoint with
Maximum/DXVK graphics and advanced through turn 12,482. An actual manual save
then returned to the interactive world within the 3,134 ms click/capture
interval. This is an observed upper bound including automation/capture time,
not a writer-only benchmark. Its archive contains `LogicTurn=13760`; all three
members pass decompression/CRC checks, and simulation subsequently reaches
turn 14,055 with empty controller queues and a clear native error flag.

The next autosave contains turn 16,760 and all three members pass CRC checks.
The client subsequently reaches turn 16,897 and remains responsive. Loading
the new manual save restores turn 13,760 and advances to 14,530. The client
then exits normally; all 16 shared profile/save files retain their file set,
hashes and timestamps. Both the earlier building checkpoint and the newer
autosave are retained privately, without replacing older user saves.

This qualifies one local map and profile, not every save or economic scenario.
Maximum menu rendering remains independently unqualified; successful saving
does not qualify those effects or crossplay. See the
[verification receipt](verification/gold-offline-saving-20261110.json).
