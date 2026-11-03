# Update and patch audit — 2026-11-03

Reviewed source revision `0bbd3bc946b3f2409771c4533991ea2a3df1fa8e`.
This is an investigation, not an update release or an implementation of the
proposed remedies. Installed copies and published packages are unchanged.
The user's active session was not accessed.

Four failure/recovery cases were reproduced using only invented files and the
repository's installer, desktop and standalone fixtures. Real process starts,
sockets, DLL loading and registry entry points were blocked. There were zero
attempts to call those entry points. The actual installer and game were not run.
See the [sanitized observations](verification/update-patch-audit-20261103.json).

## Findings

| Priority | Trigger | Result | Evidence |
| --- | --- | --- | --- |
| P1 | A newer package removes or renames a previously shipped file | An overlay leaves that file behind; strict package verification rejects the result | Reproduced with a removed helper |
| P1 | Writing the portable descriptor fails after publishing the new active instance | Installation metadata advances while `standalone.json` still selects the old instance; relocation then fails | Injected descriptor write failure |
| P2 | Patching fails after the copied game files have been written | The incomplete directory remains outside the active/history records and cannot be reclaimed by normal old-version cleanup | Injected archive fingerprint mismatch |
| P2 | A successful update defers old-instance cleanup, then the export is moved | Standalone rebinding refuses the nonempty history even when the descriptor selects the correct new instance | Reproduced with one deferred old instance |
| P2 | Disk space is close to the preparation threshold, or saves are large | The reservation does not explicitly budget all retained archive copies and migrated saves | Static finding; no real disk exhaustion test |

### Package overlay can create an invalid installation

[Setup.iss](../tools/desktop/Setup.iss#L53) recursively copies the new stage over
the target. It has no obsolete-package reconciliation. Meanwhile,
[package_manifest](../tools/installer/install.py#L110) deliberately requires
exact equality between the package inventory and the manifest.

The fixture kept an old helper, overlaid an otherwise valid new package, and
confirmed that the new payload bytes were correct but the complete package was
rejected. This is conditional on a file being removed/renamed; it does not show
that every update fails. It applies to registered and portable destinations.

Remedy: validate a complete separate new package and publish it with recoverable
replacement/rollback. Preserve unknown files for inspection. Do not weaken the
inventory check or introduce a recursive deletion of the installed package.
Qualification must include removed tools, changed runtime inventories and
interruption before and after replacement.

### Portable metadata has two separate publication points

[install_game](../tools/installer/desktop_backend.py#L385) atomically publishes
`State/installation.json`, then independently writes the portable descriptor.
Each file is atomic individually, but the pair has no recovery journal.

The injected second-write failure left the new active instance and copied save
intact, the old descriptor unchanged, and cleanup unstarted. After moving the
invented export, [bind](../tools/installer/standalone.py#L145) rejected the split
instance selection. The game was not overwritten and this test did not lose a
save; it exposed a recoverability/portability problem. An ordinary restart at
the old path can still use the valid active record, so this is not universal
startup failure.

Remedy: a bounded recovery journal for the metadata pair, with crash tests at
both writes. Recovery must select only fully verified owned instances inside
the export; it must not follow obsolete absolute paths or repin foreign files.

### Failed preparations consume space outside normal cleanup history

[prepare](../tools/installer/install.py#L345) intentionally leaves an incomplete
marker and no published starter on failure. This is a useful safety behavior.
However, [install_game](../tools/installer/desktop_backend.py#L362) writes the
owner record only after preparation succeeds, and normal
[clean_old](../tools/installer/desktop_backend.py#L786) visits recorded previous
instances only.

The failed fixture retained copied Base files and repair inputs, had no owner
record, and appeared in neither the current nor previous installation records.
The old active copy stayed valid. Repeated failures can accumulate full or
partial copies. Unknown directories must not simply be adopted or deleted.

Remedy: record a private preparation transaction before allocation, retain a
bounded ownership/hash inventory of its writes, and expose safe recovery or
cleanup for that exact transaction. Test cancellation, copy/patch failure,
changed files and interrupted cleanup separately.

### Deferred cleanup limits portability after a successful update

The relocation branch in [bind](../tools/installer/standalone.py#L145) requires
`previous == []`. Existing relocation tests explicitly empty this history before
moving a repaired export. A busy or modified old copy can legitimately defer
cleanup, leaving a usable current installation that cannot yet be relocated.

The fixture reproduced this independently of the split-descriptor failure:
the descriptor was current, the save was present, and one previous reference
alone was enough to reject relocation. This is a current safety policy limit,
not evidence of lost personal data.

Remedy: preserve bounded relative ownership/history for copies contained inside
the export, or clearly require completed cleanup before transfer. Never rebase
an arbitrary external path or silently discard unresolved recovery references.

### Space accounting is incomplete

[prepare](../tools/installer/install.py#L340) reserves source file sizes, package
sizes and 512 MiB. It subsequently retains pristine executable/archive inputs
under RepairSource and another staged archive. Later,
[transfer_preferences](../tools/installer/desktop_backend.py#L325) copies saves
without a capacity check. The fixed reserve can cover small inputs, but it is
not a measured bound for these additional bytes. Large saves can exceed it.

Remedy: budget retained inputs, staging/patch growth, renderer additions and
save migration on the actual destination volume. Recheck before each large
phase while keeping the active instance intact. This finding is from source
inspection; no disk was filled and no real game/save size was read.

## Verified boundaries and next qualification

In the failure fixtures, the old active game was preserved. In the portable
metadata/relocation fixtures, the synthetic saves were still present. Integrity
checks rejected unexpected files rather than accepting a mixed package.
These are useful guarantees and must be retained by the fixes.

This audit did not re-run signature/key generation, Windows installer execution,
native patch ABI tests or a game. No new release, deployment, process operation
or network change was performed. Full installer migration/interruption and
portable-transfer qualification remain for a later explicitly authorized live
work window. The four observations are reproducible failures/limits, not four
successful update tests.
