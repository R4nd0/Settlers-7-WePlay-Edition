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
That attempt did not qualify cross-client enclosing equality; the corrected
pair described below does. The
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
correction alone established a successful History enclosing capture. The later
corrected pair below supplies that runtime evidence.

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

## Bounded transfer of the original serialized bytes

The guarded capture now retains an owned copy of the original enclosing stream,
as well as its length and SHA-256. Every existing owner, raw-row, dynamic-rule,
position and code check still runs before that copy becomes available. Failed
captures clear the output, including previously retained bytes.

`crossplay_setup_root_stream.h` adds an opt-in byte-transfer primitive for the
existing authenticated channel. It divides at most 131,072 bytes into canonical
16,384-byte chunks, each below the channel's 65,536-byte payload limit. Directional
peer IDs, checkpoint revision, enclosing format, total length, exact next offset
and expected SHA-256 bind every fragment. A receiver is bound to an independently
supplied checkpoint descriptor; incoming fragment headers cannot establish that
descriptor. Existing channel authentication and sequence checks remain required.

The receiver exposes no partial bytes. It rejects gaps, duplicate/reordered
fragments, altered headers, size mismatches and final hash mismatches, then
permits one consumption of a complete stream. A failed or consumed receiver
cannot be reused for another ownership interval. The focused fixture passes
579 framing/hash checks, including both formats and the one-byte, chunk-boundary,
actual-size-shaped, 65,536-byte and maximum-size cases. The amended capture and
dependency fixture passes its 41 checks.

A separately frozen diagnostic adapter now sends actual successful captures in
both directions. It binds each receive to its own original capture, requires the
original confirmed checkpoint and compares the received bytes with another fresh
owner-guarded native capture. Both adapters compile and retain their ABI checks.
The live result below exercises this diagnostic. It neither invokes a native
setup reader nor supplies content permission, a start event, turns, NEO commands
or save acknowledgements. Ordinary adapters do not include it.

## Live bidirectional enclosing transfer under DXVK

The corrected isolated Gold and Ubisoft History pair completed native two-human
admission, Tower Island to Monhaim reload, actual guest Ready and the held host
Start under DXVK High at 1920x1080. All original enclosing integrity guards now
pass in both clients. The two removed laboratory hash observers were the cause
of the earlier History rejection; no original guard was shortened or bypassed.

Both original engines produced identical format-1 streams of 33,211 bytes and
format-2 streams of 33,215 bytes, including the serialized seed. Each client sent
both actual streams through the authenticated channel. All four receives
completed and matched a fresh owner-bound original capture byte for byte.
The receiver descriptor came from its own independent capture, not a remote
fragment. Both processes exited normally with code zero and every recorded
settings/save/cloud/adapter/interface/shared restoration check passed.

A separate bounded read-only inspection verified that colour registry entry 0
is nonempty in both running engines: nine bytes with the same SHA-256. The
synthetic empty-symbol reader exception therefore must not be treated as an
actual engine incompatibility or suppressed to pass a fixture.

This is live Gold to **Ubisoft History** setup-byte transport. It does not
qualify a native Steam executable pair, remote native setup application, content
closure, a shared world, synchronized turns, Gold NEO or mixed save/cold-reload.
The diagnostic intentionally grants no world permission.

See the [live transfer receipt](verification/gold-history-root-live-transfer-20261113.json).

## Original reader with populated rule vectors

The optional `--nonempty-rules` fixture extends the headless reader audit with
two source victory rules and two sector rules, each containing two placements.
Original catalogue lookup, CString hashing, native construction, copy and
destruction execute; only the bounded CRT heap and stream edges are modeled.
The original Type-3 victory suppression leaves one decoded victory rule. Sector
placement quantities are checked as uint16 values: the other two History bytes
are native temporary padding and are absent from the serialized stream.

Both formats pass write/read/write equality in all three independently pinned
images. Each has twelve malformed-input rejections across the three images.
The unchanged empty-rule baseline also passes all eighteen cases. The populated
streams contain 24,156/24,160 bytes for formats 1/2, respectively. Heap checks
cover the ten replaced initial rule buffers, retained rule allocations, all 25
migration vectors, duplicate frees and access to freed storage, including the
modeled copy and stream boundaries. Catalogue storage remains unchanged.

Player containers remain empty in these reader fixtures. These results do not
qualify full populated-player conversion or live application of a remote setup.
See the [populated reader receipt](verification/gold-history-root-rules-reader-20261113.json).

## Original reader with populated player containers

The additional `--populated-players` fixture exercises a populated player row
while all 25 rows still pass through the original methods. It includes the
player name, three vectors, three linked lists, both Castle strings, two Castle
parts and their type/value vectors, plus both preparation position lists.
Independent post-read checks validate string contents, list values and links,
vector elements, part counts and ownership. Pointer headers are normalized only
after these semantic checks; scalar storage and catalogue input remain checked.
The original version-1 migration deliberately replaces the three-byte source
vector with its 25-byte zero default, as in the earlier baseline.

Original list-read finalization also rebuilds an item-state cache from the first
two lists. Native writes were observed independently in all three images; the
synthetic source now starts with a consistent cache rather than treating the
original reconstruction as corruption. The enclosing data round-trips exactly.
Thirteen initial player buffers are released and every retained allocation must
be reachable. This runs independently and together with the populated rules.

All four combinations (empty, populated rules, populated players, both) pass
for both formats and all three original images: 24 positive cases and 48
truncated/trailing rejections. The combined streams contain 24,325/24,329 bytes.
This is bounded headless evidence with modeled CRT/stream edges. It does not
cover every actual lobby container shape, native target-object construction or
destruction, or live remote setup application. Shared world, turns, Gold NEO and
mixed save/cold-reload remain unqualified.

See the [player reader matrix receipt](verification/gold-history-root-player-reader-20261113.json).

## Live original construction and destruction of an independent target

The next isolated DXVK High pair independently exercised the original root
constructors and destructors in Gold and Ubisoft History. Gold uses a stack
target argument with `ret 4`; History uses `thiscall` with no stack arguments.
Six original lifetime regions per profile retain complete bounded code hashes,
including preparation and row construction/destruction. The native preparation
constructor loads its colour configuration through the engine resource system.

After the repeated successful authenticated root transfer and fresh-capture
comparison, each engine constructed a new root with separate preparation rows
and independently owned configuration pointers. Its original destructor returned
normally. Guard bytes stayed intact, no exception occurred, and the active
session's fresh original serialization remained byte-identical afterward. Both
clients exited regularly with code zero; all recorded restoration checks passed.

This establishes the diagnostic target's native construction/destruction path
in those two running engines. It does not prove leak freedom or qualify reading
remote setup bytes, replacing active preparation, a native Steam executable pair,
shared world, synchronized turns, Gold NEO or mixed save/cold-reload. The subsequent
reader run below extends this evidence to the independently constructed target.

See the [owned lifetime receipt](verification/gold-history-root-owned-lifetime-20261113.json).

## Reader coverage for the observed long allied lists

`--populated-players --long-allies` adds four allied lists at slots 21 through
24. Each contains 26 entries, including repeated zero after IDs 0 through 24.
The original readers preserve every entry and linked-list relationship; the
fixture never truncates or deduplicates them. This covers the longer container
shape previously observed during actual enclosing capture, while using entirely
synthetic values. Populated rules can be combined with these lists.

Both new combinations and the four earlier regressions pass in all three
original images for both formats: 36 positive cases and 72 truncated/trailing
rejections. The combined long-list streams contain 24,429/24,433 bytes. Heap
ownership checks account for 117 replaced initial player buffers, plus ten rule
buffers when rules are enabled. The larger fixture has an explicit 450-allocation
bound; ordinary fixture bounds remain unchanged. No native runtime guard or
game memory threshold is relaxed by this offline fixture.

These results still do not qualify every actual container shape, live setup
application, world start, Gold NEO or mixed save/cold-reload. See the
[long-list reader receipt](verification/gold-history-root-long-allies-reader-20261113.json).

## Live original reader into an independently owned target

A further actual Gold/Ubisoft History pair under DXVK High, 1920×1080,
received authenticated enclosing-root format 2 bytes in both directions. Each
engine compared them against a fresh owner-bound local capture before calling
its original reader on a newly original-constructed independent target. The
reader consumed the entire input, preserved its bytes and owned configuration
pointers, and passed bounded dependency inspection. The original writer then
reproduced the exact received bytes, and the original destructor returned.
All storage canaries remained intact, no exception occurred, and a fresh capture
confirmed the active root unchanged. Both clients exited regularly with code
zero; all recorded restoration checks passed.

`crossplay_setup_root_reader.h` now provides this diagnostic sequence as
`VerifyReceived`. The caller must authenticate the message and confirm the
current native checkpoint on the owner thread. The public entry point rejects
input differing from a fresh local capture before invoking the native reader.
It destroys the independent target and returns no replacement active object.
Ordinary adapters do not enable this diagnostic automatically.

The compiled serializer bridge also passes with the original Gold, Ubisoft and
Steam analysis images, both formats, populated rules and players, and the four
26-entry allied lists: six positive round-trips and twelve malformed-input
rejections. Sixty compiled lifecycle guard checks cover original code, changed
first/last bytes and unreadable regions, including each profile's six original
lifetime functions. CRT and stream boundaries are modeled in these CPU tests;
they do not execute the native constructors or emulate native exception unwinding.

This qualifies actual native reading of matching received setup bytes into an
independent diagnostic target in Gold and Ubisoft History. Active-session
adoption, a pair using the original Steam executable, complete mounted-content
agreement, shared world, synchronized turns, Gold NEO and mixed save/cold-reload
remain unqualified. See the [actual native reader receipt](verification/gold-history-root-owned-reader-20261113.json)
and [compiled bridge receipt](verification/gold-history-root-compiled-reader-20261113.json).
