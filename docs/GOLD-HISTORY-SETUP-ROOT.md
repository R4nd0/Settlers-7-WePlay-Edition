# Original enclosing setup writer

`crossplay_setup_root_writer.h` adds the guarded original enclosing writer for
Gold and both independently pinned History binaries. It serializes the global
rules, optional RNG seed and the active multiplayer preparation. This differs
from the earlier template-player signature, which never represented the full
active roster. The helper is excluded from ordinary adapters and is not wired
to native start delivery.

Gold receives the root in EAX, visitor in ESI and format version on the stack
(`ret 4`). Both History profiles use `thiscall(root, visitor, version)` with
`ret 8`. Integrity checks cover the enclosing method, rules, complete player
writer, individual row writer and nested Castle writer. Unsupported editions,
unreadable regions and changed code remain rejected.

The original root writer's nested player nodes select **preparation version 1**
in every tested root format. The earlier live version-8 diagnostics therefore
remain a separate compatibility probe. They do not establish the native world
setup format. Root format 2 serializes the RNG seed through a temporary and
writes the unchanged value back to the root. The offline audit permits only
that exact write and checks the complete root and pointed-to input afterward.

The compiled x86 fixture passes 24 enclosing writer calls, including 24 complete
preparation calls and 600 row calls. Both root formats (1 and 2), empty/populated
player rows, populated rule strings, all nested player scopes and both position
lists produce matching schemas and bytes across all three editions. The audit
checks the nested version and row order, exact binary-writer storage/canaries,
input preservation, stack and nonvolatile registers. All 51 integrity checks
pass, including code mutation and unreadable-region rejection. The existing
row regression also passes 108 calls and 24 integrity checks against its
unchanged reviewed fixtures.

Victory and sector rule vectors are empty in these fixtures. Their nonempty
bodies and dependencies still need independent verification before live root
capture. No remote setup reader, content closure, shared world, synchronized
turn, Gold NEO or mixed save/cold-reload is qualified by this offline ABI audit.

See the [sanitized verification receipt](verification/gold-history-setup-root-20261112.json).

## Nonempty rules and guarded live capture candidate

The extended audit now executes the original catalogue lookup, CString hash,
copy/destructor paths, Type-3 victory suppression and sector name/placement
writers. Two synthetic victory rules and two sector rules with two placements
each produce equal enclosing streams across all three profiles, for both root
formats and empty/populated player rows. All 24 enclosing calls, 600 row calls
and 105 integrity checks pass. The original empty-rule regression also passes
with the expanded 105 integrity checks. CRT heap/copy boundaries, catalogue
objects and streams are modeled; original lookup and hashing are executed.

Live method guards now also pin the nonempty rule bodies, catalogue lookup,
symbol search/hash and CString helper regions. Steam's catalogue lookup was
located independently; it does not follow the enclosing writer's offset shift.

`crossplay_setup_root_dependencies.h` adds a bounded read-only inventory of root
strings, victory records, catalogue arrays, hash buckets, sector records and
placements. It preserves unused sparse registry entries, rejects dereferenced
null entries, checks container bounds and requires an empty hash bucket before
the original probing loop can run. The sector registry offsets are separate:
Gold uses catalogue+0x1c, History catalogue+0x20. The inventory is a local
before/after guard; its pointer-containing digest is not compared across clients.

`crossplay_setup_root_capture.h` adds the owner-bound enclosing capture candidate.
It first captures the original version-1 preparation, verifies the enclosing
root points to that same preparation, checks rule dependencies and code, and
runs the original enclosing writer. It then checks owner/manager/profile
identity, complete raw player rows, dynamic dependencies, position lists, rules
and code again. Root formats 1/2 are separately labeled; format 2 permits only
the original unchanged seed writeback. Forty-one focused dependency/failure-path
checks and both diagnostic adapter builds pass.

The first live enclosing-capture attempt was rejected by the unchanged RAM
preflight before any game started. A later DXVK High pair captured Gold's actual
enclosing formats 1/2, but History stopped at an integrity guard before writing.
Cross-client enclosing equality is therefore **not yet live qualified**. The
separately observed identical version-1 preparation streams
are documented in [player setup transport](GOLD-HISTORY-PLAYER-SETUP.md). Complete
native setup reading, content agreement, shared world start, synchronized turns,
Gold NEO and mixed save/cold-reload still remain unqualified.

See the [extended sanitized receipt](verification/gold-history-enclosing-rules-20261113.json).

## Actual enclosing capture and laboratory observer correction

The subsequent isolated Ubisoft-data DXVK pair completed native admission,
the original Tower Island to Monhaim reload, actual Ready and the held host Start.
Gold's original enclosing writer produced 33,211 bytes in format 1 and 33,215 in
format 2. History rejected stage 410: the legacy laboratory RNG observer had
patched arithmetic inside the original CString hash at 0x4f22ab. A read-only
comparison found only those six patched bytes different in the guarded helper.
Both clients exited normally and every recorded profile/save/options/adapter
restoration check passed.

Independent original disassembly also identified 0x4f2206 inside the other
CString hash routine. Both callsites are removed from `LabResumeRngPatches`;
the remaining 54 trace sites keep their previous slot numbers. Original hash
integrity guards are unchanged. The focused LocalPairLab bootstrap fixture
passes 4,150 checks, including exclusion of both hash function regions. Ordinary
release builds exclude these laboratory observers.

A separate immutable diagnostic with all legacy RNG observers excluded also
builds and passes its ABI fixtures. Its next live attempt was rejected before
launch by the unchanged RAM guard. Neither that build nor the public two-site
correction establishes a successful History enclosing capture yet.

## Original reader baseline under headless emulation

`tools/gold/emulate_setup_root_reader.py` executes original enclosing, nested
preparation, all 25 row methods and binary visitors for Gold, Ubisoft History and
the pinned Steam analysis image. Both enclosing formats use preparation version
1. Six positive cases produce the same bytes before and after reading, including
restoration of deliberately different destination game type/player state and,
for format 2, RNG seed. The original reader creates 25 separate owned vectors of
25 zero bytes at Gold row+0x558 or History row+0x544. These legacy-format defaults
are checked independently of stream equality. Other object storage is preserved.

The reader uses its own 16-byte storage guard; writing uses the independently
verified 528-byte visitor storage. Original stack/nonvolatile-register/SEH checks,
bounded heap ownership and before/after writer input checks remain active.
Twelve negative cases reject truncated or trailing input at the stream boundary.

These fixtures use existing nonempty colour symbols, empty player containers,
empty victory/sector vectors and modeled CRT memory/streams. Their synthetic
objects are not constructed by a running game. They establish a bounded original
reader baseline, not complete populated setup conversion, a live remote reader,
actual Steam executable gameplay, content agreement, shared world start, NEO or
mixed save/cold-reload.

See the [reader and observer verification receipt](verification/gold-history-root-reader-20261113.json).
