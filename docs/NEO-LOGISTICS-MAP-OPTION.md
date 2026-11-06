# NEO-Logistics map option — RC3.15b candidate, 2026-11-05

Runtime qualification is incomplete. This option ships disabled by default in
the experimental RC3.15b prerelease. Real single-player starts
and save/load cycles now pass with the mode both enabled and disabled. Two-client
qualification has a successful fresh-session loopback run with matching native
checksums. Restoring saved individual intents succeeds on both peers. The later
native AI desync is corrected in RC3.15b by the
[workshop rotation initialization](MULTIPLAYER-RESUME-WORKSHOP.md).
Separate-PC, Steam-runtime and extended-match qualification remain limited.

The RC3.15b correction passed three completed resumed-world runs (NEO on,
another cold reload, NEO off): 1750 common complete native checksums all match,
with normal exits. All 13 or 18 saved individual intents restore exactly on
both peers before simulation. Saved search work is retained. This does not
generally explain older PC2-host desync reports or qualify arbitrary long games.

## Behavior

The map setup screen offers **NEO-Logistics**, initially off. Changing the map
resets the choice. Multiplayer hosts choose it while creating the session;
the lobby displays the host's selection. It is fixed for that match.

Ordinary supported warehouse requests use their own creation turn for transport
selection. Equal creation turns retain the native stable order. Explicit player
and AI individual priorities remain effective. Group panels stay visible, but
their drag controls and displayed ranks are disabled. Deliberate native player
and AI group-reorder commands are suppressed in this mode; native default
initialization is retained. Group precedence is excluded from transport keys,
including the explicit-priority bands. Production scheduling and pathfinding
are otherwise unchanged in the published candidate. The subsequent
[distance handoff candidate](NEO-LOGISTICS-DISTANCE.md) adds shortest-total-distance
selection among native eligible intermediate warehouses while retaining this
request ordering. It is source-only and has not been released.

The native adapter retains the next-map selection across recreation of the local
Lua state. Selecting an option does not change the running world's transport
policy. Activation requires the guarded fresh native world, at turn 0 or 1.
Individual intent already observed during creation is retained for that world.
With the mode off, the comparison bridge resumes the original native comparator.

The previous [FIFO comparison](NEO-LOGISTICS-FIFO.md) motivated this candidate.
Its measured gains belong to that laboratory build. They do not qualify this
new map option, which also suppresses group commands and adds lifecycle handling.

## Saved state and multiplayer protocol

The existing save-game pickle cache carries `weplay_neo_logistics_v1`: a version
and a bounded ASCII payload containing the enabled flag and full individual
intents. NEO1 payloads retain the replay turn. NEO2 payloads additionally record
the persisted logic turn, exactly one below the next replay turn in multiplayer.
Task handles, birth turns and intent are preserved, including a task created for
that next turn. Native restoration checks the persisted turn and every loaded
handle/birth pair before rebinding any process address.

The multiplayer native load path leaves this Lua cache unavailable. The data was
not lost at save: the previous failing checkpoint contains it in the embedded
`SaveGameCache.s7` member. Resume now reads only that member and
`SavegameInfo.xml` from the selected numbered save in the active native save
filesystem. It never scans profiles. Slot/date/map validation remains in use.
The bounded BAF reader checks the directory CRC, complete gzip stream/CRC and
decoded member sizes/CRCs. It parses the serializer's small data grammar without
executing save-supplied Lua. It does not inflate the native world member.
As in the existing BBA reader, the native producer's stale compressed-CRC field
is not treated as authoritative. The decoder is upstream puff 2.3 (zlib 1.3.1);
its license and the S7Packer format-code license accompany NEO packages.

The archive's own `LogicTurn` binds legacy NEO1 state to the correct loaded turn;
only the verified replay-turn = LogicTurn + 1 relationship is accepted.
This conversion retains all marks. New multiplayer saves store both counters
explicitly. A cache missing enabled state is rejected; an empty replacement
intent ledger is never fabricated.

Wire revision NEO3 carries the host's choice and complete bounded saved state
through the existing reliable player-data serializer. Choice and payload are
staged atomically after the selected file is validated. The original
`receivedStartGameMessage` event locks them before local Lua-state recreation.
Native Ready, start packets, transfer checks and simulation checksums stay native.
Transport callbacks update owned immutable snapshots under a mutex and atomics;
only the guarded owner thread binds the intents to a world. No socket, service
port or chat message is added. Older NEO1/NEO2 wire adapters are rejected.
The earlier NEO1 prototype waited for a setup visitor that was not called on the
observed start path. The guest received the real start event but its additional
NEO gate rejected it. That unused visitor hook was removed. The new lock follows
the existing native event; global primitive-field hooks are not installed.
All participants require the matching candidate protocol, including for a match
with NEO disabled; interoperability with older adapters is not promised.

## Building and verifying

```powershell
./native/build.ps1 -OutputDirectory work/neo-candidate -NeoLogistics -MaximumPhysicalCores 2
# Add -GameDirectory for the current directory-capable interface.
# Add -LocalPairLab only for the isolated laboratory executables.
python tools/neo-logistics/test_runtime.py
python tools/neo-logistics/verify_interface.py PRIVATE_WEPLAY_INTERFACE_BBA
# Development only: create a NEW private archive for the owning bounded runner.
python tools/neo-logistics/probe_interface.py PRIVATE_WEPLAY_INTERFACE_BBA work/neo-probe.bba
# Add --off for the ordinary-mode lifecycle/control regression.
```

`-NeoLogistics` also includes the existing native network/resume pump, as both
single-player and multiplayer are in scope. It and the earlier `-NeoFifoLab`
are mutually exclusive. Ordinary
builds do not install the new hooks. Both Ubisoft and Steam History profiles
retain the full executable fingerprint, mapped callsite, object and ABI guards.
The group entry guard includes its build-specific EH prologue and `ret 8` ABI.

Package creation has an additional `--neo-logistics` option. It requires the
matching pinned adapter and all six integration files, after the existing
resume/directory transformations. Legacy package flags and pins are retained.
The original game installation is not modified by this work.

Verified checks include:

- Native map-option checks for both executable layouts, including original
  comparator fallback, individual intent, group-command suppression and native
  initialization, save-state rebinding, protocol mismatch, actual-start locking
  and rejection of a changed choice after that lock.
- 65,562 save-codec checks, including a full 65,536-entry round trip.
- Full modified Lua 5.1 scripts compile for both fingerprinted input interfaces;
  unrelated archive members are byte-for-byte preserved.
- Lua lifecycle/UI tests and installer pipeline/capability tests pass. Bootstrap,
  Steam-profile and directory-console guard/rollback tests pass.

Private runs and fixtures are under `work/neo-map-option/`. Visible smoke tests
confirmed the map checkbox. Native activation succeeded after moving selection
out of the recreated Lua state.

The apparent graphics blocker was reproduced by the development driver's extra
`pcall` wrappers around engine lifecycle calls. Removing those wrappers restored
world creation and progress; adding them again reproduced the DXVK wait even
after removing the driver's error-exit call. The test now calls the engine
through its regular Lua update path. This observation does not identify an
internal DXVK defect or prove the precise engine/Lua failure mechanism. No
graphics driver or production rendering workaround was required.

The checked-in development probe now exercises a real Grassland Mesa world with
three hard AIs, native x20 pacing, a newly allocated unused save slot, and the
original restart-with-save path. With NEO enabled, it saved at turn 2005 with
21 explicit AI intents and restored the exact native snapshot at the same turn.
All four real group widgets were disabled and group dragging had no effect,
before and after load. The equivalent off run saved/restored at turn 2004 and
retained the ordinary enabled group widgets. Both exited normally with healthy
memory sentinels. No human task buttons were present in these early human
settlements, so the zero task-button count is not evidence of their in-game
behavior. Synthetic individual-control and ABI checks remain separate evidence.

All completed owning runners restored the source profiles, timestamps, adapters
and interfaces from private backups. The NEO pair runner now accounts separately
for each owned game, the runner and the local service, and has a bounded lifetime.
For these experiments it uses `allocation_pressure`: system commit headroom and
the actual 32-bit process address space govern allocation stops. Low available
physical RAM remains recorded telemetry and does not by itself prevent a start
or stop a run. Default launcher policy is unchanged. In pair-12 the games peaked
at approximately 1.7 GiB private memory each; runner and service together stayed
below 50 MiB private memory. Commit headroom remained above 12 GiB.

Two-client loopback tests now deliver the real native start event to both peers,
and both initialize an enabled NEO world. This is a verified correction of the
previous additional start-gate rejection, not yet successful multiplayer play.
In pair-12 the host reached turn 1000 and saved an actual multiplayer checkpoint
at turn 600, while the guest stayed at native turn 3. The pre-existing guest
desync save was unchanged, so this is not proof of a detected simulation desync.
The original test exited the host too early; subsequent probes keep the host
alive for a bounded shared wall-time window.

The NEO-off control pair-14 also stalled early. A verified guest main-thread
sample waited inside DXVK D3D9 presentation. Thus the earlier lifecycle `pcall`
correction is not a complete explanation of all two-client graphics stalls.
Pair-15 explicitly selected immediate presentation instead of FIFO/VSync, as
confirmed by the DXVK log, but did not restore progress. These are laboratory
observations; neither FIFO policy nor Windows memory management is established
as the cause.

Pair-16 retained immediate presentation but launched non-activating test windows
on the normal Windows desktop instead of the separate inactive desktop. Both
clients then passed turn 1000 with NEO active; the host wrote an actual unused
multiplayer save at turn 600. Both exited normally. The complete replay streams
contained 600 common native simulation-checksum records with zero mismatches.
The owning runner restored both test copies and profiles. This supports using the normal desktop for the
next qualification runs. It does not isolate visibility from desktop composition
or establish a general DXVK defect. A fully invisible two-client runner remains
unqualified. The original cold-resume missing-state failure is corrected as described below.

Cold-resume pair-19/20 originally found the Lua NEO pickle key unavailable on
both peers, including after original game unpickling. Offline inspection proved
that the selected native save did contain all four saved individual intents.
The new reader and NEO3 state carrier restore those exact handle/birth/intent
triples on both clients before the first resumed simulation step (pair-25).
Both worlds passed turn 1600 from the turn-600 checkpoint. The host also wrote
a new native save at logic turn 1200; it contains NEO2 counters 1201/1200 and
13 individual intents. A second cold resume restored all 13 exactly on both
clients (pair-26). This fixes the demonstrated missing-priority-data failure.
Synthetic checks cover both preferred and deferred intent; the live snapshots
in these checkpoints contain AI preferred intent.

The final cold-resume repeat (pair-28) restored the original four intents exactly
on both clients, saved again at turn 1200, and reached turn 1600 on both. Both
then exited normally. Their complete native replay streams contain 600 common
checksum records with zero mismatches. This qualifies the observed load and
bounded continuation window, not arbitrary later gameplay or another reload.
The runner restored both source copies and profiles; memory sentinels were healthy.

The final adapter also passed fresh single-player start/save/restart/load
regressions on Grassland Mesa with three hard AIs at native x20. NEO-on saved
at turn 2000 and restored all 25 individual intents exactly; NEO-off saved and
loaded at turn 2002 with ordinary group controls enabled (live-24/25). Both
exited normally and restored their private source copies/profiles.

A later native desync was subsequently isolated. Pair-26 generated new desync saves after loading
the turn-1200 checkpoint. The controlled pair-27 loaded the exact same native
world and info bytes with NEO disabled on both peers; only its NEO cache record
and the private fixture choice were changed. Its complete native replay streams
contain 429 common checksum records: the first 427 match, the final two differ.
Both clients exited normally and the source copies/profiles were restored.
Thus this later failure also occurs without the FIFO/individual-intent policy.
Its cause was unknown in those runs; the newer
[workshop investigation and fix](MULTIPLAYER-RESUME-WORKSHOP.md) identifies
uninitialized AI rotation constants after load. These were two local Ubisoft
clients with two hard AIs; separate-PC or Steam resume is not qualified here.

Outstanding qualification: check
populated individual priority controls in game, and repeat the fixed-cohort
delivery comparison on the final policy. No new delivery-speed improvement is
claimed for this map-option build yet.
