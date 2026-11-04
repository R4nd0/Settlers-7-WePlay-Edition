# Recoverable installer and managed-game updates

RC3.14 includes this follow-up to the
[update/patch audit](UPDATE-PATCH-AUDIT.md). Published RC3.13 assets and existing
private ZIPs have not been replaced. No game executable, native adapter, service
binary, account check or ABI guard was changed by this work.

## Complete packages instead of overlays

Setup first extracts its own package into temporary storage and verifies it
against the exact manifest compiled into that installer. Every Payload, Runtime
and Tools file must match before package code is imported. It then copies the
verified files, preserving their original filename spelling, into a new slot on
the destination volume and verifies that slot too. No system Python is needed.

Only after preflight succeeds does `ssInstall` rename the old Package aside and
publish the complete new one. A bounded `State/package-upgrade.ini` journal
identifies the exact stage and previous-package slots. Failure restores the old
package; the next Setup can also recover an interrupted swap. A committed journal
keeps the new package. Normal Inno file registration still uses the final paths,
so its rollback and uninstaller know those files.

Publication does not run in a per-file `BeforeInstall` callback: an exception
there is not a reliable installer abort. Preflight errors return through
`PrepareToInstall`; publication failure uses `Abort` from `ssInstall`, one of
the documented termination points. See the official
[Inno event documentation](https://jrsoftware.org/ishelp/topic_scriptevents.htm)
and [Abort semantics](https://jrsoftware.org/ishelp/topic_isxfunc_abort.htm).

`SetupMutex` serializes setup runs before they can touch the swap journal.
The existing launcher mutex and game-operation locks remain enabled. A closed,
valid standalone export is adopted with its old bundled runtime before launcher
replacement, including an export that has never been started on that PC.

Old packages and unpublished slots are first moved into `State/PackageBackups`.
Retirement removes only unchanged files declared by their manifests, within
those exact backup slots. Unknown, modified or busy files remain. No recursive
delete of the installed Package or an arbitrary backup directory is used.

## Paired standalone metadata

The active game selection and `standalone.json` form one recoverable operation.
`State/installation-transaction.json` records their validated before/after
states before either is published. A partial pair rolls back; a complete pair
can be accepted only after full verification of the owned target and package.
Unexpected edits stop recovery and retain the evidence.

Cleanup of the old game starts after this commit. A failure updating the
portable descriptor therefore cannot leave a new active selection paired with
an old descriptor and then remove its recovery source.

## Interrupted preparations and space

Each new managed preparation has a bounded record under `State/Preparations`.
An ownership marker is written at allocation. As game, repair, archive and
runtime files are written and verified, their hashes are recorded. Normal
cleanup and later retries can reclaim those exact unchanged files even if
patching failed before publication. Preserved files have a receipt under
`State/Preparations/Retained`.

Unknown files are not adopted merely because they appear in the new root,
ArchiveStage or RepairSource. Personal LocalData is never in the deletion
inventory. Legacy untracked directories are not retroactively claimed or
removed. Modified, unknown or busy data can legitimately leave a directory for
inspection; successful cleanup does not promise to erase every byte there.

The game-copy reservation includes pristine repair inputs, archive staging and
growth allowance, runtime/package bytes and the measured preferences/save copy.
Save migration rechecks capacity immediately before copying. The active
installation stays selected until the replacement is verified and published.
Package staging separately checks space on its actual destination volume.

## Relocation with deferred cleanup

A complete standalone export remains movable when cleanup of an old contained
instance is deferred. Rebinding resolves only declared instance IDs under the
new export, validates their existing ownership records, preserves their file
hashes and rebases the contained history. It does not follow old absolute paths,
invent ownership for external directories or repin modified old game files.
The old instances can subsequently be cleaned with the normal guards.

## Qualification

The [verification record](verification/update-recovery-20261103.json) distinguishes
invented-file regressions, actual installer fault injection, real standalone
update/relocation checks and GUI rendering tests. It also identifies intentionally
omitted checks. These tests qualify the update paths; they do not establish
long-match stability, freedom from desync or a new multiplayer release.
