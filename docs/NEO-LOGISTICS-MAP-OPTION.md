# NEO-Logistics map option — candidate, 2026-11-05

Runtime qualification is incomplete. This is an explicit candidate build, not a
replacement for the currently distributed adapters. Real single-player starts
and save/load cycles now pass with the mode both enabled and disabled. Two-client
qualification has a successful fresh-session loopback run with matching native
checksums; resume and broader compatibility checks are still in progress.

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
are otherwise unchanged.

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
and a bounded ASCII payload containing the enabled flag, saved turn and explicit
task intents. Full task handles and birth turns are serialized; process addresses
are rebound only after validation against the loaded world. Invalid, stale or
incomplete state is rejected. Saves without this field load with NEO disabled.

For multiplayer resume, the existing validated slot metadata also records the
NEO choice, bound to the existing slot/date/map checks. That choice is staged
before creating the resumed session. Intent restoration still requires the
state embedded in the transferred native save; metadata alone cannot enable it.

The candidate extends the existing Storm join and player-data serialization.
Wire revision NEO2 carries a fixed protocol version and the host's lobby choice.
The original `receivedStartGameMessage` engine-event branch then locks that
choice before recreating the local Lua state. Both host and guest must have this
start lock, and the selected choice must still match it when a world starts.
A guest cannot use Ready without the lobby configuration. Original readiness,
start packets and world-transfer checks remain in use; no native Ready bit,
acknowledgement or checksum is fabricated.
Transport callbacks modify only atomics; world/intent changes remain on the
guarded owner thread. No sockets, service ports or chat messages are added.
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
- 65,558 save-codec checks, including a full 65,536-entry round trip.
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
unqualified. Cold multiplayer resume remains unsuccessful.

Cold-resume pair-19 loaded the actual host checkpoint at native turn 600 on both
peers, but `pickleJar.isPickled("weplay_neo_logistics_v1")` was false on each.
The Lua guard rejected the missing enabled-state payload before attempting native
ledger rebinding. Pair-20 deferred that check to the original `game.unpickle`
step after GD-state creation; the key was still absent. That timing experiment
was not adopted into production. These observations do not yet distinguish
missing serialization from transfer/cache restoration loss. The enabled-state
guard must remain: resuming with a reconstructed empty intent ledger would lose
explicit individual priorities. A complete multiplayer saved-state carrier needs
further work. Both failure runners stopped promptly and restored their test
copies; no test clients or local services remain running.

Outstanding qualification: check populated individual priority controls in game,
finish two-client checksum, mismatch and resume checks, and repeat the fixed-cohort
delivery comparison on this final policy. No new delivery-speed improvement is
claimed for this map-option build yet.
