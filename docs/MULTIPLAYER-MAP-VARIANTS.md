# Map Forge template names and multiplayer checkpoints

The 2026-11-06 source correction fixes a native template-name lifetime error
and enables the multiplayer checkpoint browser to reload Map Forge variants.
The reported map is Grassland Mesa; the acceptance case uses a variant with
25 victory points and a cold restart of both multiplayer processes.

## Native cause and correction

The original template getter copies a session std::string, obtains c_str(),
destroys the copy, and returns that pointer. The Lua-result wrapper then
constructs its qString from already-invalid storage. Names of up to 15 bytes
use inline storage in the destroyed stack object. Longer names use freed heap
storage, which can happen to retain the name. Neither case has a valid lifetime.

In the local short-name control, both native session descriptors contained
`WePlay QA Max25`, but the Lua getter returned nil on both clients. The host
world required 25 victory points; the guest silently loaded the standard
6-point rules. The host's checkpoint metadata also recorded an empty template.
Renaming the identical configuration to a 24-byte name made the Lua getter
return the expected name. This isolates the lifetime defect from missing
files or late session-name delivery. Earlier successful tests with an 18-byte
name did not expose this defect.

The bridge replaces only the call consumed by that original qString wrapper:
Ubisoft ED7B97, independently mapped Steam ED7287. It copies the live session
name into bounded thread-local adapter storage, which remains valid while the
original constructor copies it. The original constructor, allocator and
caller-owned result destruction remain in use. The bridge validates the
owning thread, active local connection, singleton identities, native object
types, std::string layout, bounds and terminator. It preserves registers,
flags, x87/SSE state and LastError. Executable hashes, callsite bytes and
transactional patch rollback remain mandatory. No victory-point logic,
simulation, RNG or native desync detection is overridden.

The checkpoint browser also previously rejected every nonempty saved template.
It now checks that the host has the saved template for the saved map, loads
that setup, and forwards the name to native resume-lobby creation. The original
save transfer, deferred world load, roster and readiness gates remain active.
Missing templates fail before lobby creation. Standard saves retain nil setup
template and an empty native template argument.

To reproduce, save a Grassland Mesa variant with additional sector victory
points and a name of at most 15 UTF-8 bytes. Put the identical template on both
participants, start multiplayer, and compare the required points on both
worlds. Save, close both clients, reload through the multiplayer save browser,
and repeat that comparison while continuing past the next save. The corrected
test uses 25 required points throughout; a checksum-only comparison can miss
the original unequal-rules failure.

## Runtime verification

Final corrected short-name fresh and cold-load measurements are recorded in
[the verification receipt](verification/map-variants-20261106.json).

| Run | Both worlds | Actual host save | Common native checksums / mismatches |
| --- | --- | --- | --- |
| Fresh | Turn 0 to 3600 | Slot 1002 at turn 3000 | 600 / 0 |
| Both processes restarted, saved checkpoint selected | Turn 3000 to 6600 | Slot 1003 at turn 6000 | 600 / 0 |

The cold-loaded match continued for six game minutes. Both peers retained the
correct name and 25 required points. Both saves contain the encoded template
in their native save cache. Both runs exited normally, produced complete replay
streams and no new desync saves, and restored their source copies/profiles,
hashes, timestamps, adapters, interfaces and runtime configuration. Both memory
monitors remained healthy, with no allocation/address-space pressure.

The tests use two isolated local Ubisoft clients, two human slots, two hard AI
opponents and NEO disabled. The real template was created through native Map
Forge load/edit/save operations: nine added sector points, 25 total points and
25 required points. Both clients have the same template archive. Test fixtures
invoke the original lobby Ready/Start callbacks and the actual checkpoint
browser callback; the simulation and native save files are real game output.

Acceptance requires the correct template name and 25 required points on both
native worlds, successful real save/reload, continued simulation, complete
native replay streams with matching common checksum records, normal process
exits and successful restoration of both isolated test copies. Checksum
agreement alone is insufficient: the faulty short-name control completed with
matching checksums despite different static victory rules. It is not a pass.
The received-world guest reports `UsesMapForgeTemplate=false` after loading,
while the host reports true. The required-point field is checked independently;
that flag is not forced to a different value.

## Scope and checks

Each production test build runs 238 new lifetime/ABI/guard checks covering
both Ubisoft and Steam layouts: short/long boundaries, invalid native pointers, source poisoning,
UTF-8, machine-state preservation and patch/rollback. Production directory,
NEO and NEO/directory builds also pass existing bootstrap, Steam profile,
workshop, terrain, FIFO/state, lobby and applicable directory/NEO suites.
The related Python/Lua source suites pass 106 checks in HEAD plus only this
change. Thirty-eight Lua compilation checks cover the four archive stages.
Installer adapter and UI archive fingerprints are advanced together.

All peers need the matching corrected native adapter and UI recipe. Templates
still need to be identical on participating machines. Same names do
not establish equal contents; automatic distribution/content verification is
not implemented by this correction. Deliberately inconsistent templates were
observed producing unequal native rules or a stuck guest. Older metadata that
already lost a name cannot reconstruct that name automatically.

These are local two-client Ubisoft observations. Steam has independent mapping
and synthetic patch/ABI coverage; live Steam, separate-PC/Internet, extended
sessions and populated human-command scenarios are not qualified by this run.
RC3.15b downloads remain unchanged; the correction is newer source. Private
game binaries, templates, saves, profiles and raw diagnostics are not shipped.
