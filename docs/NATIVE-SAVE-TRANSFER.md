# Native saved-session transfer: qualified stage

The experimental Gold-host adapter now transfers the original saved-session
configuration, native file request, saved-file information and file blocks to
both **original Ubisoft History and original Steam History** receivers.
These primitives are opt-in; ordinary startup adapters do not enable them.

## Actual DXVK evidence

Two isolated standalone pairs received the same retained native checkpoint:

| Receiver | Original file blocks | Bytes in original receive stream | Full SHA-256 matches |
|---|---:|---:|---|
| Ubisoft History | 3,018 | 24,722,247 | Yes |
| Steam History | 3,018 | 24,722,247 | Yes |

The stream is the uncompressed `SaveGame.s7` member, not its containing save
archive. After consuming the transfer, each original History configuration
selected the expected map and its own `Savegames/MPSave` token. Read-only
inspection of that original native stream independently confirmed its digest.
Both successful pairs exited normally and restored test profiles, adapters,
archives and shared user state. Neither successful test launched a resumed world.

The sender verifies the whole original stream before sending any file blocks.
The receiver verifies sequence, explicit envelope offset, total, unchanged native
checksum and final SHA-256 before enqueueing the final original block. Only
routing CIDs are projected. Checkpoint, content, setup, NEO and peer identities
are bound by the envelope and the authenticated channel.

Gold sends 8-KiB blocks. A bounded queue and a maximum of 32 uncredited blocks
avoid overflowing the channel. Transport credit never becomes a native event
and does not substitute for native Ready or a saved-world admission.

## Native ownership correction

An earlier candidate failed with a History heap error. Original clone and
destructor execution showed that `CFilePackageEvent` shares its raw payload
pointer when cloned. Destroying both objects freed that buffer twice.

The corrected local receive path transfers the allocation to the original
queued clone and clears only the factory object's payload pointer before its
destructor. Complete destructor and clone guards, plus the unchanged local
routing/queue/locking/return regions, protect that operation. Instrumented
outgoing branches are separate from this local-only operation. Gold as a file
receiver is deliberately excluded from this qualified direction.

## Reproduction and limits

`native/build_crossplay_resume.ps1 -OutputDirectory <fresh directory under work>`
builds and runs seven protocol/hash fixtures and builds the fixed-address x86
ownership guard fixture. They pass 15,057 checks. These are synthetic transport
and hashing checks, separate from the actual game evidence above.

With locally owned pinned analysis images, pefile and Unicorn,
`tools/gold/verify_resume_ownership.py --help` describes the original-code oracle.
It runs six original clone/destructor ownership cases with allocation/free calls
modeled, plus 22 compiled integrity-guard checks for the two History receivers.
Keep images and generated output under ignored `work/`.

## Saved NEO metadata and admission receipts

The separate `crossplay_resume_neo.h` primitive transfers bounded fragments of
the serialized NEO save field. It verifies the exact serialized SHA-256 before
parsing the existing restricted pickle grammar as data. It never executes Lua.
The decoded NEO2 logic turn, next replay turn, enabled choice and all intent
marks must match the admitted checkpoint. No partial payload is exposed.

`crossplay_resume_admission.h` supplies distinct adapter Prepared and Queued
receipts. They bind the checkpoint and both peer identities; neither becomes a
native event, Ready flag or native file acknowledgement. Its saved cache policy
allows Gold's observed loaded AI state and preserves the opaque local human
field which is absent from the original configuration wire. The original
enabled host Start action remains a separate required authorization. Fresh
lobby Ready validation is unchanged.

A further isolated Gold/Ubisoft DXVK test verified native file consumption and
armed NEO metadata, then queued the original held start on both clients. Both
original game observations reached the retained world's LogicTurn 300. Gold's
loading screen remained visible, and a saved-session priority command was
rejected by a guard still requiring a fresh-session agreement. No continued
shared turns or restored active NEO are qualified by that observation. The
separate original Steam saved-world start still requires its own actual test.
Both clients exited normally, and strict restoration of profiles, archives,
adapters and shared state passed. See the
[separate metadata/admission receipt](receipts/native-saved-neo-admission-20261115.json).

Later standalone DXVK tests reached the retained world and rebound all saved
NEO intent marks to live tasks on **both original History editions**. The
original games reached LogicTurn 301, but the first native replay CRC differed.
Disabling new diagnostic build, priority and save actions during loading did
not remove the difference. All three follow-up pairs exited normally and passed
strict restoration.

The first CRC input difference is the total number of registered logistics
tasks: Gold has 100, History 126. A read-only Steam History comparison found
100 shared task handles with equal class, priority, group, bucket, parent,
birth and player fields. History additionally contains 26 `CRequestTask`
objects, 13 per AI, under two saved `CConstructionTask` parents. These are
group-9 requests; subclass state has not been exhaustively compared. Their
creation or lifecycle difference during loading still needs diagnosis.

This is evidence of native saved-world entry and live metadata binding. It
does not establish synchronized continuation, stable resumed logistics or a
coordinated new checkpoint and reload.

This stage does **not** qualify synchronized saved-world continuation, sustained
resumed NEO deliveries, continued matching native turn CRCs, coordinated saving,
full NEO5, or EasyTier connectivity. The saved-start envelope
retains Gold's actual empty native start field in a separate checkpoint-bound
namespace; it does not relax the fresh-start cache guards or grant start rights.

Machine-readable evidence: [transfer receipt](receipts/native-save-transfer-20261115.json).

## Loaded task wait counters: further diagnosis

A subsequent original Steam History DXVK pair read identical saved wait counters
for all 15 construction tasks in both native archive readers. At the first
original construction update, all 15 Gold counters were zero, while 13 History
counters remained positive. Twelve other observed construction fields matched,
including state, birth, last update, worker, building and child count. Each of
the two affected AI parents still had 13 children at that observation. Gold's
later removal of those children therefore follows a difference already present
in the loaded scheduling state. The exact subsequent zeroing callsite remains
unresolved. This observation run exited normally and passed strict restoration.

A private candidate captured 126 original saved task records and attempted to
preserve their waits before the first native scheduler call. Its full-set
comparison rejected the actual import before any simulation writes; Gold took
the adapter's guard-failure exit, and History exited normally. Full profile,
archive, adapter and shared-state restoration passed. The 171 synthetic import
checks and compiled ABI/integrity checks do not establish a working game fix.
No production adapter was changed. See the
[separate wait-counter receipt](receipts/native-saved-task-waits-20261115.json).

## Original request postload policy: bounded Steam continuation

Further native observations explain the earlier task loss. Gold reset 93 saved
`CRequestTask` objects while loading; original Steam History reset none. The
verified original caller chain leads to the two passes of the logistics
manager's postload routine. History explicitly excludes the exact request class
in both passes and its request predicate returns false. Gold instead uses a
type-dependent predicate, admitting those requests to registration and reset.
The reset can cancel reservations and notify parent tasks and workers; restoring
only the base state and wait fields cannot undo those effects.

A private diagnostic import of the original base fields initially produced two
matching native CRC blocks, followed by a mismatch at block 2. Only the RNG
inputs differed there; additional Gold construction-worker animation draws were
observed. That result remained a failed continuation.

A subsequent private candidate applies History's request exclusion only at the
two verified Gold postload predicate callers. It requires the authenticated
saved-world interval, simulation owner, exact native class, complete saved
identity and matching registered object. The independently observed saved
request states are 0, 1 and 6. Other callers and later simulation ticks retain
the original Gold predicate. The 99 synthetic policy checks, both native ABI
routes and 14 original method/vtable integrity checks passed before launching.

The actual Gold/original-Steam History standalone DXVK pair then retained all
97 requests in both postload passes. All 126 captured base records matched at
the first scheduler call: the earlier import wrote **zero states and zero wait
fields**. The games produced **1,279 common original native CRCs, blocks
0–1278, with no mismatch**, starting from the retained world with active NEO
and all saved intent marks rebound. Both clients exited normally and strict
profile, archive, adapter and shared-state restoration passed.

Gold's loading overlay nevertheless remained visible during this run. Successful
simulation continuation therefore does not yet qualify a usable resumed UI or
the complete requested workflow. The original Ubisoft History comparison with
this candidate, sustained resumed delivery checks, a new coordinated checkpoint
and its next mixed reload, and production integration remain open. No production
adapter or release package was changed. See the
[bounded postload-policy receipt](receipts/native-saved-request-postload-20261115.json).

## Gold loading overlay: missing save metadata

The subsequent UI diagnosis found that Gold's eager weather initializer reads
`MapPath` from an absent `GetMapDataBySavegame` result. This aborts the tutorial
module before it defines the `head` dialogue UI. Later, `hud.update` calls
`head.videoIsRunning`; the exception prevents the normal loading-screen update
from consuming its existing close signal, even while the world simulates.

The shared Gold profile builder now guards that optional metadata result and
retains the original fallback to `MapSetup.GetMapPath()` from the active setup.
Existing saved metadata keeps its original path. The fix neither hides the
loading frame directly nor changes native Ready, map-mode or simulation state.
The menu, graphics and installer-release compositions all inherit this guard;
all 284 Lua members compile in each of the four generated archive profiles.

In the actual Gold/original-Steam History standalone DXVK test, the missing
metadata branch was observed, the full dialogue module became available, and
the original HUD and loading-screen updates completed. Gold's loading frame
closed normally, the world and HUD were visible, and Escape opened and closed
the original in-game menu. The retained active-NEO world produced **1,566 common
original native CRCs, blocks 0 through 1565, with no mismatch**. Both clients
exited normally and strict restoration passed. The runtime candidate differs
from the public guard only by a branch-observation message; other temporary
inline UI probes were also present in this diagnostic run.

This fixes the diagnosed Gold UI failure and extends the bounded Steam result.
The same resumed run against original Ubisoft History, sustained delivery
verification, a new coordinated checkpoint and its next mixed reload, and
production native-adapter/package integration remain open. See the
[loading-UI receipt](receipts/native-saved-loading-ui-20261115.json).
