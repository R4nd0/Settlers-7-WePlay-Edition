# Complete player preparation diagnostic

The native diagnostic can now capture the original complete player preparation:
all 25 rows, the `Slots` byte and the ordered `ActivePlayers`/`InactivePlayers`
lists. This is still **incomplete crossplay**. It excludes the enclosing setup
root/rules, runtime bootstrap and complete content agreement; it supplies no
native world-start permission. Gold NEO and mixed save/cold reload remain
unqualified.

`crossplay_preparation_writer.h` keeps independent address/byte guards for the
whole writer and its existing row/Castle dependencies. Gold takes the visitor
in EAX and preparation/version on the stack; History uses
`thiscall(preparation, visitor, version)`. Both clean eight argument bytes.
The original full writers consume the low byte of each row slot even when
their callers retain unrelated upper bytes in that stack word.

The compiled x86 thunks execute original code offline with recording visitors
and original binary visitors. Six synthetic cases cover formats 1 and 8 with
empty, populated and repeated long ally lists. Each call visits rows 0 through
24 in order and both position lists; the populated inactive list includes row
24. All three original profiles produce the same schemas and binary hashes.
The audit passes 36 full calls, 900 row calls and 33 compiled method-guard
checks. CRT heap and bounded output stream are modeled. Row data, pointed-to
data, SEH, stack and nonvolatile registers are preserved. Format selection in
the actual fresh-world path is still unqualified.

`crossplay_preparation_capture.h` first obtains the owned 25-row diagnostic,
then validates and fingerprints both position lists before calling the full
format-8 writer. It rejects invalid counts, links, row indices, duplicate
position indices and aliased list heads. It checks owner, row dependencies,
position dependencies and method bytes again afterward, and clears every
result on failure. Its native position/dependency fixture passes 37 checks.
This is a point-in-time observation, without an atomic native lock or a
continuous mutation watch. The helper is excluded from ordinary adapters.

## Actual DXVK pair 78 and corrected ally-list limit

The isolated Gold and Steam History data clients ran standalone on DXVK High
1920x1080, with the fingerprinted Ubisoft runtime for History and no Steam SDK.
They reached the native two-human roster, original Tower-to-Monhaim reload,
genuine guest Ready and the held host Start. The original writer checkpoint
reached Gold's confirmed gate. Gold's active-row capture then rejected stage 4
(dependency validation), before any row writer ran. History subsequently lost
the diagnostic channel. No world started.

Read-only inspection checked each owned PID, parent, executable path and hash.
Both engines had a valid eight-entry symbol registry with capacity 64. Their
special rows 21 through 24 retained ally lists longer than the 25 row slots;
some lists contained 26 links, including repeated values. An independent
bounded read of the dependency rules rejected `row21:list2` at the old limit
25. The same rules with limit 128 accepted all dependencies: 13,089 bytes for
Gold and 13,100 for History. This does not establish why those repeated entries
exist or whether their gameplay semantics are correct.

The diagnostic now bounds ally-list storage/traversal to 128 links, like the
other row lists, and retains order and repeated values. The 128-KiB total
dependency budget, pointer/link/cycle checks and all native ownership/ABI guards
remain. A 26-link fixture fails the previous guard and passes the correction;
the 128/129 boundary and mutation of the last repeated entry are also tested.
The row-dependency suite now passes 86 checks. No live list was edited or
deduplicated. Positive native capture after this correction still needs a run.

The launch passed with 12,164 MiB free physical memory. No pressure was recorded
when the stage-4 rejection was diagnosed. Later, during read-only inspection
and cleanup, available memory fell to 907 MiB; the watcher requested recovery.
Both games exited normally with code zero. Options, adapter, Interface, saves,
cloud and shared-profile checks passed restoration, and the final window
inventory contained no S7 window. Unrelated processes were not changed.

Private runtime 14 integrates the new complete diagnostic from a captured
runtime-12 source tree plus the reviewed changes. Its 244 source files and
both DLLs are pinned independently of other standalone work. Both newly built
observer ABI fixtures pass. Runtime 13 source preparation was abandoned before
compilation; runtime 14 supersedes it. Start remains held, and no gameplay
content agreement is fabricated.

See the [sanitized verification receipt](verification/gold-history-preparation-capture-20261112.json).

## Runtime 14 follow-up, pair 79

The corrected guard passed Gold's actual 25-row capture. The complete writer
also returned a bounded output, but its combined final consistency gate rejected
stage 303. That gate covered local owner/state, row dependencies, positions and
native method guards; this run does not identify which comparison failed. No
partial hashes were published as a successful complete snapshot. History then
lost the diagnostic channel. Both clients exited normally with code zero and
passed restoration; the fresh window inventory was empty. The memory watcher
recorded no pressure in this run.

The completion checks now expose stages 303 through 309 separately, retaining
every check. A subsequent private diagnostic saves an independently successful
25-row report before attempting the complete capture, so a later full-capture
failure cannot hide that narrower evidence. Capture rejection remains a logged
diagnostic with Start held; the authenticated loopback channel stays available
for the other client to finish its own diagnostic. File-output errors and the
existing admission/start/memory guards still fail closed. This changes no world
permission and does not qualify a common world or synchronized gameplay.

## Pair 81 and binary-writer storage correction

The runtime-15 DXVK High pair retained successful reports from all 25 original
row writers on both clients. Twenty-two row outputs matched exactly; slots 1,
3 and 4 differed. This is serialization evidence, not an agreed game setup.
Both complete captures passed owner, dependency and position checks, then
rejected the final method guard (stage 309). Read-only inspection of the owned
processes confirmed that all six whole/row/Castle code regions still matched
their original fingerprints. Both games exited normally, and profile, saves,
cloud, adapters and shared-state restoration checks passed. No world started.

Offline reproduction identified a concrete memory error: the binary writer
stores 128 stream positions after its 16-byte visitor header. The diagnostic
had reserved only the header. Original scope entry in all three pinned images
writes at `+0x10 + 4*depth`, with a native depth limit of 128. The correct writer
storage is 528 bytes (`0x210`). Readers retain their separate 16-byte header
view. All four diagnostic writer call sites now use the complete writer.

The new storage audit rejects the old allocation for both a populated row and
the complete preparation in all three editions. It also executes each original
scope primitive at all 128 levels against actual compiled C++ stack storage,
checking guards on both sides and preserved registers. Exact-size storage with
canaries passes original row and complete serialization. The existing whole
writer regression still passes 36 calls, 900 nested row calls and 33 method
guard checks against the unchanged reviewed fixtures. The x86 public build and
the codec, setup-signature and dependency rejection suites pass.

Immutable runtime 16 contains the four corrected headers and is built for both
clients. Its live confirmation is pending. The overwrite explains a real ABI
defect; the stage-309 diagnosis still requires a corrected live run. Neither
the fix nor the 22 equal rows qualifies content, world start, turns, Gold NEO or
mixed-edition save/cold-reload.

See the [writer-storage verification receipt](verification/gold-history-writer-storage-20261112.json).
