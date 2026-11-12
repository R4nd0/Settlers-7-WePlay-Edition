# Native symbolic registry capacity correction

The active-player capture candidate now validates the actual native color
registry layout. All three original analysis images use four words: entry-array
pointer at `+0x00`, opaque allocator/pool pointer at `+0x04`, count at `+0x08`
and capacity at `+0x0c`. The previous candidate incorrectly treated `+0x04`
as capacity and would reject an ordinary pointer-valued registry before calling
the row writers. Its earlier 66 checks used the same incorrect synthetic layout;
they did not establish a positive live capture.

The corrected fixture has a real pointer in the allocator field and a separate
capacity. It fails the unmodified candidate with `FAIL valid 25-row dependencies`
and passes after the correction. The guard retains all 16 header bytes in its
local dependency fingerprint, validates capacity/count against the actual field,
and preserves the opaque allocator identity without following data the original
write visitor does not read. Additional checks confirm that changing that pointer
changes the local fingerprint and that its numeric value cannot substitute for
an invalid capacity. The corrected native suite passes 78 checks with `/W4 /WX`.

`tools/gold/emulate_symbol_registry.py` independently executes original reserve
routines in Gold, Ubisoft History and the Steam analysis image. Five cases per
profile cover no growth and three allocation sizes. They verify that original
code writes capacity at `+0x0c`, preserves count and allocator identity, copies
existing entries and passes the actual pool pointer/capacity to its helper.
Stack/nonvolatile registers and pool data remain unchanged. CRT allocation,
copy/free and the pool helper are modeled; this does not qualify the complete
native pool allocator. All 15 original reserve calls pass.

The active-player emulator's registry fixture also uses the corrected four-word
header. Its 108 original writer calls and 24 compiled guard checks still pass
against the existing reviewed field schemas and binary-output hashes.

Both new private diagnostic adapters compile from an isolated source tree based
on the retained `584e046` checkpoint plus this reviewed correction. Existing
standalone worktree changes are excluded. Absolute includes are relocated into
that captured tree and the runner checks all source and DLL hashes. The original
game executable, native method/object/ABI guards, profile backups/restoration,
4-GiB launch preflights, runtime memory watcher and held Start event remain.
The separate old outgoing-wrapper emulator is a historical regression fixture;
the newly compiled adapters have their own observer ABI checks.

This correction does not establish a shared world, deterministic turns, Gold
NEO or mixed saving/reloading. A positive actual 25-row capture and complete
setup/bootstrap/content agreement remain necessary before native common start.

See the [sanitized verification receipt](verification/gold-history-symbol-registry-20261112.json).
