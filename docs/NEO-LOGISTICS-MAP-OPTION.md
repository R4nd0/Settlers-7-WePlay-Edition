# NEO-Logistics map option — candidate, 2026-11-05

Runtime qualification is incomplete. This is an explicit candidate build, not a
replacement for the currently distributed adapters. A real fresh world has
reported `neo-logistics-started`; save/load and two-client behavior have not yet
passed their native end-to-end checks. Do not interpret the synthetic protocol
tests as successful multiplayer.

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

The candidate extends the existing Storm join, player-data and game-setup
serialization. It adds a fixed protocol version, the host's lobby choice and a
matching start confirmation. A guest cannot use the Ready action without the
lobby configuration, and cannot activate a fresh world without its matching
start confirmation. Original readiness and world-transfer checks remain in use.
Transport callbacks modify only atomics; world/intent changes remain on the
guarded owner thread. No sockets, service ports or chat messages are added.
All participants require the matching candidate protocol, including for a match
with NEO disabled; interoperability with older adapters is not promised.

## Building and verifying

```powershell
./native/build.ps1 -OutputDirectory work/neo-candidate -NeoLogistics -MaximumPhysicalCores 2
# Add -GameDirectory for the current directory-capable interface.
# Add -LocalPairLab only for the isolated laboratory executables.
python tools/neo-logistics/test_runtime.py
python tools/neo-logistics/verify_interface.py PRIVATE_WEPLAY_INTERFACE_BBA
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

- 134 native map-option checks for both executable layouts, including original
  comparator fallback, individual intent, group-command suppression and native
  initialization, save-state rebinding, protocol mismatch and start confirmation.
- 65,558 save-codec checks, including a full 65,536-entry round trip.
- Full modified Lua 5.1 scripts compile for both fingerprinted input interfaces;
  unrelated archive members are byte-for-byte preserved.
- Lua lifecycle/UI tests and installer pipeline/capability tests pass. Bootstrap,
  Steam-profile and directory-console guard/rollback tests pass.

Private runs and fixtures are under `work/neo-map-option/`. Visible smoke tests
confirmed the map checkbox. Native activation succeeded after moving selection
out of the recreated Lua state. The automatic save/load scenario has not yet
reached its save checkpoint, despite native turns advancing. Error capture was
added around native game creation/update for the next diagnostic run. A briefly
suspended/resumed owned-thread snapshot located the subsequent wait in DXVK's
`d3d9.dll`, before the logistics world existed. It did not establish a FIFO
deadlock. A separate-desktop run with presentation synchronization disabled is
tested using [DXVK 3.1.1's documented configuration](https://raw.githubusercontent.com/doitsujin/dxvk/v3.1.1/dxvk.conf).
That run passed memory preflight, but stopped at the same DXVK wait, at native
turn 1 with no logistics world. The earlier hidden-window configuration also
failed to reach the save checkpoint. A hidden-window run on the normal desktop
showed the same native turn, absent world and DXVK stack. These observations identify the current
test blocker; they do not prove that the complete candidate works once rendering
continues.
Some runs were safely stopped by the memory sentinel; subsequent starts were
refused below its unchanged 4 GiB host-memory requirement. All completed test
wrappers restored the source profile, adapter and interface from private backups.

Outstanding qualification: finish the native start/save/load scenario, check
the active logistics panel and individual priority controls in game, run a real
two-client session (including mismatches and resume), and repeat the fixed-cohort
delivery comparison on this final policy. No new delivery-speed improvement is
claimed for this map-option build yet.
