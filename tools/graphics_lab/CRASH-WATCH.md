# Owned crash capture

`crash_watch.py` attaches only to a live graphics-lab case admitted by its
original owner. It never launches or stops a game and makes no registry,
AeDebug, WER, shader, simulation-speed or global debugger changes.

After loading through the original UI and making a new private save, the owner
calls `admit(game, case, load_and_new_save_confirmed=True)`. The retained
`OwnedProcess` handle records the child and parent's actual creation times,
PID, parent, path and SHA256. The active journal, controller checkpoint,
initialized DLL and source-bound build must agree. Admission is refused for a
closed or recovered case. The original case must explicitly checkpoint
`crash_watch_allowed: true` and this exact watcher source hash. The CLI has no PID, process-name, export or arbitrary
command selector.

Run from the repository root:

```text
python -B -m tools.graphics_lab.crash_watch --case work/OWN-ACTIVE-CASE --output work/FRESH-CRASH-DIRECTORY --seconds 300
```

The debugger is the pinned, individually Microsoft-signed x86 CDB10.0.29680.1004
extracted from Microsoft's WinDbg1.2611.4001.0 package under
`work/debugger-acquisition-20261124/portable/x86`. All18PE hashes are pinned and
checked with offline WinVerifyTrust before execution and under deny-write
leases. The complete MSIX container signature was not verified; per-file
Authenticode signatures and official HTTPS acquisition were verified.

The first three first-chance AVs receive unique basic `/m /u` dumps with exception
context, registers, stack and loaded-module logging. `gn` passes each exception
unchanged to the application's handler. A second-chance AV receives one further
dump if the four-dump budget permits, then `qd` detaches. The command file uses
only fixed relative filenames in the fresh private directory. Symbols and
sources use empty local directories; inherited debugger paths and remote symbol
servers are disabled. These are diagnostic timings, never benchmark evidence.

For the exact pinned Steam EXE only, the known original allocation-unlink fault
at module RVA `0x9350e0` additionally prints small fixed read-only windows:256
bytes around the candidate allocation header,128 bytes of the allocation
manager,4 bytes of the PerfCounter registry pointer, and256 bytes of a possible
containing allocation. The latter is explicitly labelled a candidate; no owner
is inferred from its offset alone. Other profiles and the generic authored
fixture have no such reads. No pointer-following expression can interrupt dump
capture when the requested memory is unavailable. Original exception handling
remains `gn` followed by second-chance capture/`qd`.

A300–600second wall-time limit and64MiB output monitor request `qd`; after a
two-second grace period only the retained, reverified own CDB handle can be
terminated. `-pd` disables debugger-exit termination of the target. The original
target handle is checked before and after; a natural game exit remains possible.
The64MiB monitor is a polling cutoff, not a filesystem quota: one in-progress
minidump can overshoot, which is explicitly recorded rather than hidden or
truncated. No raw logs or dumps belong in git or distributable packages.

The authored x86 `crash_fixture.cpp` was tested with the actual CDB arguments and
command file: a local event held the invalid write until CDB's READY marker;
first-chance capture/`gn`, second-chance capture/`qd`, registers, stack and module
output all passed. Both unique basic minidumps were11,611bytes and contained
exceptionC0000005. The debugger exited0; the authored fixture naturally exited
C0000005. This establishes the diagnostic command path, not an S7 crash cause.
The private receipt is `work/crash-watch-smoke-20261124-01/receipt.private.json`.

Primary command documentation:
[CDB options](https://learn.microsoft.com/en-us/windows-hardware/drivers/debugger/cdb-command-line-options),
[exception filters](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/sx--sxd--sxe--sxi--sxn--sxr--sx---set-exceptions-),
[gn](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/gn--gn--go-with-exception-not-handled-),
[qd](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/qd--quit-and-detach-),
[minidumps](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/-dump--create-dump-file-).

## Fixed SDK baseline pre/post profile

The explicit `--baseline-metric-profile` owner flag requires baseline crash-watch
mode and a fresh eleven-source checkpoint. It observes only the pinned original
Steam destructor callsites at RVAs `0xa6db80` and `0xa6db85`. Code bytes, original
owner identity and module absence are checked before admission and again under
leases. It records at most three same-thread/user header pairs, then removes its
two debugger breakpoints. It never skips the destructor/free or rewrites a return.

The first c49 attempt produced no accepted profile: this CDB build raises an
error when an undefined `$bp20` pseudo-register is queried. The corrected helper
uses `bl /L` for empty/defined breakpoint queries, explicit ownership bits for
cleanup, MASM `and`/`or`, and a final readiness gate. Generic READY alone is not
sufficient; require the emitted `S7_METRIC_CODE_GUARDS_ACCEPTED` marker too.

Actual pinned CDB tests against an independently authored CPU fixture exercised
the nested command strings, three pairs across four fixture cycles, removal and
repeated cleanup at an owned first-chance AV. Both fixture and debugger exited0,
without parser/register errors. The fixture deliberately used its own exports
and data; it did not pass or imitate S7 admission. These receipts are under
`work/metric-cdb-syntax-20261126-01`. Actual S7 phase evidence remains separate.

b46 independently reproduced the original unlink AV in the owned SDK/DXVK route
with the NativeGraphics module absent. That is a useful boundary, not a pristine
retail baseline or complete causal exoneration. The phase profile asks whether
the header is bad before or after the original destructor; it does not supply
an automatic cause verdict.

Primary syntax references: [breakpoint listing](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/bl--breakpoint-list-)
and [MASM operators](https://learn.microsoft.com/en-us/windows-hardware/drivers/debuggercmds/masm-numbers-and-operators).

## Module coverage lost during exit

An ongoing baseline poll can lose a mapped filename while a module unloads.
The provider records the exact failed row and bound original identity privately,
and returns no partial verified inventory. The watcher ends with
`baseline_mapping_coverage_lost`, marks current module absence and continuous
Metric coverage unproven, and requests detach from its verified own debugger.
It never terminates the game. Admission and pre-attach failures still reject;
unknown modules, NT mismatches and other query failures retain their strict checks.
There is no retry or unresolved-row omission.

In b51 the code-guard and readiness markers were accepted, but this query failed
while the child was still alive. No Metric pairs or AV records were captured.
The game independently completed the original menu exit with code0 and preserved
profiles, binaries and options. Coverage ended before the game did, so the empty
profile does not establish whether the destructor was reached. It supplies no
corruption interval or cause-fix proof.
