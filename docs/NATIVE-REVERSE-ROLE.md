# Steam History host / Gold guest: native integration

This is a required direction, separately from the qualified Gold-host paths in
[the saved-world milestone](NATIVE-PRODUCTION-SAVED-RELOAD.md). It is not yet
admitted by the normal launcher parser, runtime, content catalog or installer.
The [production contract](GOLD-HISTORY-PRODUCTION-CONTRACT.md#required-reverse-role-extension)
defines the complete original-engine and final-package requirements.

## First native components

`crossplay_history_host_choices.h` observes the original Steam History host's
single-member session, configuration cache, actual host/local CIDs, descriptor
and template. Two unchanged observations and the same original local participant
are required. It distinguishes the host's selected numeric save slot from the
receiver's `Savegames/MPSave`. An observed save token is not permission to open
an archive. The helper grants no Gold guest, Ready, file or start authority.

`crossplay_gold_guest_row.h` has a separate original Gold preparation/claim ABI.
Preparation receives configuration in EDX and the row in EAX. Claim receives
configuration in ECX, the row in AL and one callee-cleaned stack argument. The
History claim function signature cannot be reused for these calls.

The row primitive requires an already admitted original two-player Gold guest
session, owner-thread execution, original method fingerprints and the same
guarded preparation getter/writer roots. It invokes the original routines for
row2 and checks that only that row's network-slot/controller fields changed.
It does not bind the guest's name/castle, consume host configuration, project a
backend or make that partial preparation a playable session.

## Component evidence, 2026-11-17

- 60 bounded offline executions of the actual original Gold preparation/claim
  routines cover five rows, four kinds and three initial controller states.
  Only the expected fields change, and original stack cleanup is checked.
  Both complete method regions match the actual prepared Gold executable.
- Eight compiled integrity-guard checks against the independently pinned Gold
  image cover accepted original code, changed endpoints and unreadable methods.
  The guard fixture executes only its own code in the offline emulator.
- The component build runs 36 History-host observation checks and 40 Gold-row
  scope/preservation/compiled register-ABI checks with authored fixtures.

Run `native/build_crossplay_reverse.ps1` with a fresh directory under `work/`.
Fixed-address guard images are built only, never executed natively by that
script. These are component and headless-code results. No reverse game, common
world start, NEO logistics, original History-host save or coordinated reload is
claimed by them. No adapter or catalog pin is promoted.

## Remaining native path

The runtime still needs the actual History host bootstrap/configuration/start
capture, complete Gold guest claim and consumed-state readback, original human
Ready exchange, host/guest setup-root agreement and full NEO lifecycle. Its save
checkpoint must come from a new original Steam History host save, followed by
a fresh-owner transfer/load, matching original turns, continued logistics and
resave. The local Gold SDK service remains separate from common host authority.

The final installed package must pass all three required role pairs. The
forward diagnostic receipts and these headless component checks cannot replace
that qualification. Normal speed and DXVK remain mandatory.
