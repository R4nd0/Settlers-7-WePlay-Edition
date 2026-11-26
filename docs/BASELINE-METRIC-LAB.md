# Owned SDK baseline lifetime diagnostic

The Steam SDK/DXVK baseline reproduced the original-menu exit access violation
without the modern graphics module. Physical module identities were checked
against the retained process, including mapped-file proofs for missing Windows
driver path aliases. This is not a pristine retail baseline and does not identify
the corruption's cause.

The optional `--baseline-metric-profile` laboratory mode asks whether the
allocation header changes across the original performance-counter destructor.
It requires `--baseline-crash-watch --crash-watch`, no graphics experiments, a
fresh exact eleven-source checkpoint, the original owned Steam process and a
new owner admission after loading and creating a new private save. Existing
baseline module-absence, binary, recovery, identity and memory guards remain.
Ordinary baseline mode retains its ten-source contract.

The helper verifies two fixed original code windows through a retained read-only
process binding. The watcher repeats this proof under file leases before CDB.
It installs two bounded software breakpoints, records at most three paired
before/after observations on the same thread and object, and removes its own
breakpoints after the limit or an exception. The ordinary access-violation
handler remains the diagnostic tail. Software breakpoints change timing; this
is not a performance measurement.

No destructor, free, reference count, return value or allocation is replaced.
Missing code-acceptance markers, incomplete pairs or unreadable memory remain
missing evidence. A changed header narrows the interval; it does not by itself
prove which component owns the allocation or caused the corruption. The helper
has CPU guard/parser coverage; actual debugger execution must be reported
separately in the private case records. Nothing in this diagnostic qualifies
graphics, normal exit or a release.
