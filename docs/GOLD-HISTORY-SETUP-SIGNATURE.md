# Original native rule signatures

The experimental adapter can now compare the original global-rule and template
player writers across Gold and History. Two isolated DXVK High runs on
2026-11-12 completed native admission, host-map initialization, an actual guest
Ready action and authenticated signature comparison. Gold and History both
serialized 2,152 rule bytes and 41 template-player bytes, producing equal SHA256
digests in both runs. The second run used the Steam History data set with the
same isolated Ubisoft History executable. Neither process loaded a Steam SDK.
This establishes that particular default Monhaim configuration; it does not
qualify the original Steam executable at runtime.

The [map initialization correction](GOLD-HISTORY-MAP-INITIALIZATION.md) remains
required. History initially selected Tower Island and then loaded the Gold
host's Monhaim map through its original Lua/native loader. Read-only process
checks found matching native participant IDs, assigned slots, Ready caches,
starting positions and teams. Gold's preparation byte at `+0x71` is padding and
is never treated as a Ready flag.

## Implementation

`native/crossplay_setup_signature_codec.h` is an opt-in diagnostic reader. Its
edition-specific thunks invoke the original rule and template-player writers
with their independently audited calling conventions. Each method has a
4-KiB code fingerprint in addition to the existing executable, stream, visitor,
native ownership, roster and cached-map guards. The reader requires both native
humans Ready, bounds each writer result to 64 KiB, checks the stream position and
visitor flags, and rechecks the preparation header and ownership after writing.
Only a successful capture publishes its result. No game event is dispatched.

The digest covers the actual map and template names and both length-delimited
native writer outputs. `crossplay_setup_signature.h` encodes only lengths and
digest in an exact 80-byte, reciprocal-peer-bound frame. The existing channel
provides authentication. It rejects malformed sizes, peer mismatches, empty
digests and writer lengths outside 1–65,536 bytes. A caller must retire a captured
signature when setup changes. This diagnostic codec supplies no freshness or
start authorization by itself. Ordinary adapters do not include either header.

## Verification and limits

- Both live pairs returned normally from all four original writer calls and
  reported equal authenticated signatures. No world was started.
- Both clients in both pairs exited normally with code zero. The runners
  verified restored History settings/adapter/Interface, unchanged saves/cloud,
  restored Gold entry point and unchanged shared profile files.
- 186 Windows x86 checks cover frame boundaries, reciprocal identities,
  differing signatures, failure-result clearing, SHA256's known `abc` vector
  and excluded native bindings. Builds use `/W4 /WX`; test executables embed an
  `asInvoker` manifest so Windows installer-name heuristics do not request
  elevation.
- `tools/gold/emulate_setup_signature.py` executes the compiled call thunks and
  original visitors for all three analysis profiles: six original calls and
  24 method-guard checks. It checks stack, nonvolatile registers, SEH and POD
  preservation against reviewed field metadata. Its visitor/reflection is
  modeled, nested nodes are skipped, containers are empty and player rows are
  neutral-only. Native binary streams were exercised by the separate live runs.

The template-player writer describes `HumanProfile` (CastleSlot/Team) and
`AIProfiles`; it is **not the active multiplayer roster**. Global rules contain
NeededVictoryPoints, StartResources, TechboardProfile, TradeboardProfile,
VictoryPointList and SectorInfoList. Other template/edited-map files, active
colors/options, initialization scripts and engine behavior need separate
agreement. Equal signatures cannot prove full setup/content compatibility,
determinism or [working common start](GOLD-HISTORY-START.md). Mixed turns, NEO
logistics and mixed save/cold reload remain unverified.

Sanitized evidence is recorded in
[`verification/gold-history-setup-signature-20261112.json`](verification/gold-history-setup-signature-20261112.json).
