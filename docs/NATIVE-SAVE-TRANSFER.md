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

This stage does **not** qualify a joint saved-world start, restored active NEO
before the first logistics comparison, continued matching native turn CRCs,
coordinated saving, full NEO5, or EasyTier connectivity. The saved-start envelope
retains Gold's actual empty native start field in a separate checkpoint-bound
namespace; it does not relax the fresh-start cache guards or grant start rights.

Machine-readable evidence: [transfer receipt](receipts/native-save-transfer-20261115.json).
