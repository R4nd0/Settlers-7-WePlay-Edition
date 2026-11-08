# Launcher review: responsiveness and resilience

Reviewed on 2026-11-08 against commit
`22f20abf720bca0c5a5106f93d383899aef821d9` (RC3.23 application source).
This is a review, not a set of implemented fixes. Published release assets and
product code were not changed.

The subsequent implementation is documented in [Launcher hardening](LAUNCHER-HARDENING.md).

Three parallel reviews covered performance, stability/update handling and UI.
Findings were checked against current source bytes. The TokenSave index was
approximately 97 minutes old and was used for navigation, not as authoritative
runtime evidence. UI evidence comes from eight existing RC3.23 application
self-test renders, rather than a new interactive session. No game was launched.

## Evidence and priorities

P1 means a potentially blocking availability problem. P2 affects reliability,
responsiveness or normal user workflows. P3 is a smaller maintenance concern.
A code-proven failure path is distinguished from a reproduced fixture failure
and from a measured performance result below.

### F01 - P1: no bounded recovery from a hung backend

[Backend.Run](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Program.cs#L107) awaits `ReadLineAsync()` and
`WaitForExitAsync()` without a deadline or cancellation token. Its `Process`
disposal does not terminate the child. [Cancel](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Program.cs#L153)
only writes a cancellation file; the backend checks it at cooperative
[progress checkpoints](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/desktop_backend.py#L75).
[FormClosing](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Program.cs#L328) refuses to close while busy.

If an operation hangs in file I/O, a driver or another helper outside those
checkpoints, the UI has no effective bounded recovery route. This is a
source-proven missing limit, not an observed spontaneous RC3.23 hang. The
stdout line-size check also happens after the entire line has been read.

Introduce per-action deadlines and explicit cancellation acknowledgement.
Separate the lifetime of an intentionally running game from short requests.
After a grace period, recovery may terminate only verified owned helpers at
safe boundaries; it must not interrupt an installation commit or kill a game
indiscriminately. Bound IPC input while reading, not only after allocation.

Acceptance test: a controlled child reads stdin and then never responds or
honours cancellation. The UI must recover within a documented bound, preserve
transaction recovery data and leave no unintended child processes.

### F02 - P1/P2: full package hashing in repeated UI paths

The public lobby timer runs every 15 seconds through
[Guard](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/MainForm.Directory.cs#L64).
[SetBusy](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Program.cs#L998) disables navigation and the other
pages. [directory_list](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/desktop_backend.py#L701) calls full
`install.package_manifest()` on each refresh. The list is also cleared before
the request succeeds, causing visible empty/repopulate cycles.

Measured against the final RC3.23 package on this workspace's Z: drive:

| Quantity | Result |
| --- | --- |
| Inventory files | 346 |
| Actual digest calls per full verification | 356 |
| Bytes read by those digests | 186,541,556 (177.90 MiB) |
| First / second verification | 22.807 s / 25.064 s |
| Second run: digest time / path-validation time | 23.732 s / 1.017 s |

These are two successive measurements without cache clearing of package
verification, not whole launcher startup benchmarks or predictions for players'
local SSDs. Cache state was not independently measured. The
extra digests include repeated EasyTier validation. Busy prevents overlapping
refreshes, but a refresh can exceed its own timer interval and block the UI
again on subsequent ticks.

The same design creates additional repeated work:

- Saving a small graphics change calls
  [probe](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/desktop_backend.py#L548), whose manifest check is at
  [line 181](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/desktop_backend.py#L181), and then
  [record_ownership](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/desktop_backend.py#L525), which checks it
  again at line 529. Those two package scans alone account for approximately
  355.80 MiB with this package, before other reads. Complete save latency was
  not measured.
- A portable installation's
  [snapshot dispatch](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/desktop_backend.py#L1010) invokes
  `standalone.bind()` repeatedly. Its
  [full package scan](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/standalone.py#L117) occurs before the
  already-bound fast path. This applies at startup and after game sessions.

Use a local `directoryRefreshing` state and a read-only listing path that does
not lock the whole application. Keep the last successful list visible, mark it
stale on failure and disable unsafe admission. Deduplicate verification within
one operation using an explicitly scoped verified context. Separate portable
adoption/relocation from ordinary snapshots. Preserve full integrity checks at
execution/admission/mutation boundaries; do not replace them with an indefinitely
trusted timestamp cache.

Acceptance tests: slow digest and HTTP fixtures while navigating, no overlapping
refresh, stale results cannot enable joining, and unchanged package files are
not repeatedly hashed within the same operation. Existing tampering and
ownership regressions must continue to fail closed.

### F03 - P2: automatic cleanup delays the usable startup screen

[Initialise](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Program.cs#L574) awaits `cleanup-automatic` before
`ShowMain()`. When previous copies exist,
[retire_old_copies](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/desktop_backend.py#L917) verifies the
package and the entire active game. Then
[cleanup_instance](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/desktop_backend.py#L905) hashes each owned
old file before deleting it.

The amount of I/O can include several game-copy sizes after updates. This is a
code-derived cost; no new full-game timing was run. Move maintenance out of the
critical startup path, expose its status and defer it safely while playing or
updating. Preserve the deletion ownership/hash checks and conflict locks.

Acceptance test: an old instance with deliberately slow reads must not delay
basic launcher navigation, and cleanup must never race an active game/update.

### F04 - P2: saving graphics is not a single recoverable transaction

[save_settings](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/desktop_backend.py#L541) switches the renderer,
then writes choice, quality and player settings, followed by installation
policy and ownership. Individual atomic writes and the session lock do not
make this multi-file operation atomic.

Reproduced with the existing synthetic DesktopTests fixture: inject an I/O
failure when writing quality after the choice write. The save reports an
error, but the new `simple_menu` choice remains persisted while quality and
installation metadata retain their old bytes. Renderer/GPU/display boundaries
are mocked in this fixture; this does not establish an actual game corruption.

Stage all values, retain before-images and commit or recover the group through
a settings journal, including the renderer-policy boundary. Distinguish an
unsuccessful commit from a successful save followed by a failed UI refresh.

Acceptance test: inject failure at every persistence step, restart, and verify
that one complete old or new settings generation is recovered, with unknown
user files preserved.

### F05 - P2: an older release failure hides a valid new update

[update_client.check](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/update_client.py#L209) fetches and
verifies up to eight candidate manifests in one unisolated loop. Any later
failure aborts the check before caching an already verified newest candidate.

Synthetically reproduced using the real signed RC3.23 manifest: its signature
passes, fetching the older candidate fails, and the operation ends with
`update_download_failed` without a cached verified envelope.

Isolate ordinary candidate transport failures and retain verified candidates,
with explicit incomplete-check status where needed. Keep signature validation,
same-sequence conflict detection and high-water/antirollback protection. Do not
silently treat a signature or equivocation failure as harmless network noise.

Acceptance test: a valid newer signed candidate plus a transient failure for an
older candidate does not erase the valid update; tampering and downgrade cases
still fail their security checks.

### F06 - P2: completed update downloads accumulate

[prepare](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/update_client.py#L268) creates a new UUID job each
time. After `Setup.partial` becomes `Setup.exe`, its
[exception cleanup](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/update_client.py#L280) still removes only
the partial file. No completed-job reaper was found in the reviewed update
client, helper or setup path.

Reproduced with a 23-byte synthetic installer and a handoff-preparation fault:
the completed installer remains. Cryptographic validation was mocked for this
cleanup-only test. A real retained RC3.23 installer occupies 106,012,487 bytes;
repeated attempts/releases can accumulate these files.

Reclaim a failed newly-owned job safely, and add age/count/size limits for
inactive owned jobs on a later startup. Exclude active handoffs, reject linked
paths and preserve unknown files. Do not recursively remove arbitrary state.

Acceptance tests: faults before and after rename, helper failure, next-start
cleanup, active jobs, unknown files and symlink/reparse-point substitution.

### F07 - P2: some UI file writes bypass the normal error boundary

The [language change handler](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Program.cs#L324) calls
[Lang.Set](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Program.cs#L82) directly; its state writes can throw
I/O/access errors. Direct cancellation handlers likewise call
`Backend.Cancel()` and its file write outside `Guard`.

A read-only/unavailable state directory can therefore escape the launcher's
normal translated-error handling and reach framework exception handling.
This is source inspection, not a reproduced production crash.

Give these operations an explicit error boundary. Report persistence failure,
keep a coherent in-memory language selection, and show cancellation as pending
only after the request was successfully delivered.

Acceptance tests: read-only state, a directory at the temporary filename,
disappearing storage and cancellation-write failure.

### F08 - P2: graphics drafts can disappear without a decision

[Probe](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Program.cs#L809) reloads a snapshot with
[`replaceFields=true`](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Program.cs#L588). All graphics form
values are replaced by saved values. Closing the application also has no
graphics-draft/dirty decision.

Trigger: change a graphics option, open Help, run the graphics check, return to
Graphics. The draft is replaced. This is a UI-state loss, not damaged saved
preferences. Keep saved state, hardware state and an editable draft separate;
show unsaved changes and offer Save/Discard when an action would replace them.

Acceptance test: edit, probe, navigate and refresh capabilities; the draft
survives unless explicitly saved or discarded.

### F09 - P2: primary actions compete with long forms

The existing `de-minimum-2.png` render places the private-Internet primary start
action below the visible minimum-size viewport. The graphics form contains many
long explanations and independent controls. Its fixed save action is useful
and should remain.

Use a visible action row for Create/Join/Leave, a concise connection summary and
expandable details. Group graphics into Display, Image Quality and Additional
Effects. Preserve native keyboard/screen-reader controls. These are UX
improvements, not evidence of broken multiplayer.

Acceptance tests should cover primary actions on each page at 880x620 and
representative DPI scaling, not only the four home cards.

### F10 - P3: help and diagnostics have drifted from the implementation

[extendedDetailInfo](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Strings.de.json#L80), also present in
English, still says Modern graphics adds finer textures. The current
[sampling configuration](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/graphics_profile.py#L380) explicitly
preserves native mip selection after the RC3.23 correction. The old explanation
is visible in the existing detail-page render.

[diagnostics](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/installer/desktop_backend.py#L970) also reports the
hardcoded release `RC2.1` instead of the actual application release. Correct the
description and derive support version/source metadata from the release record.
Keep application version, directory protocol, content contract and NEO wire
revision as separate named fields.

Acceptance tests: exported diagnostics match desktop-release metadata, and the
visibility description no longer promises a removed texture override.

### F11 - P3: shortcut maintenance performs unnecessary UI-thread I/O

[Initialise](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Program.cs#L576) invokes
[Shortcuts.Create](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/22f20abf720bca0c5a5106f93d383899aef821d9/tools/desktop/Program.cs#L170) on every ordinary start.
Existing owned desktop/start-menu links are saved again through `WScript.Shell`
even when unchanged. Redirected or unavailable desktop storage can cause a UI
pause. This is an unmeasured risk, lower priority than the measured full scans.

Create/repair links on installation, relocation or an explicit maintenance
action. Avoid rewriting an unchanged link on ordinary launch.

## Implementation order

1. Add controlled hung-child and slow-list regression fixtures. Bound backend
   lifetimes and make lobby refresh independent of global navigation.
2. Introduce per-operation verification context; remove redundant UI-path scans
   while preserving execution and mutation guards. Instrument time-to-usable,
   operation durations and bytes hashed without private payloads.
3. Journal settings saves; isolate update-candidate transport errors; reclaim
   inactive update jobs. Add disk-full, access-denied and restart recovery tests.
4. Preserve graphics drafts, keep primary actions visible and shorten/group the
   graphics form. Correct stale descriptions and support metadata.
5. Move cleanup and shortcut maintenance away from the startup critical path.

Use explicit operation states and derived button capabilities as these paths
are refactored. The current distributed boolean state is a maintenance pressure,
not itself proof of an additional defect. A framework rewrite or a persistent
backend daemon has not been justified by these measurements.

## Foundations to preserve and review limits

Update signatures, high-water protection and exact installer hashes are already
present. The compiled updater holds the checked installer against writes until
execution. Private invitations use current-user-only named pipes with bounded
frames. Native buttons, explicit network consent, file ownership and native
ABI/hash guards are useful foundations. No authentication bypass or remote code
execution defect was established in this review; that is not a comprehensive
security certification.

Existing layout/fixture tests do not establish bounded behaviour under hung
children, disk-full writes, disappearing network storage or slow refreshes.
The new probes reproduce the settings partial-save and updater failure/retention
cases; package measurements cover repeated verification I/O. Hung-child and
unavailable-storage recovery remain proposed acceptance tests. None of this
constitutes new native game qualification.

Private reproduction evidence is retained under
`work/launcher-review-20261108/`: performance measurements, updater probes and
the settings failure probe. No game assets, account data, credentials or private
runtime state are included in this report.
