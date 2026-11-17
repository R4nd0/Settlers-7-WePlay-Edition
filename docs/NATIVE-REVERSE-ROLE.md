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

`crossplay_gold_guest_identity.h` binds that already claimed row to the actually
admitted native local participant. It uses Gold's separately pinned original
CString copy assignment for the name and projects only the observed opaque
preparation identity field, separately from the routing CID. It validates the
entire claimed CString capacity before calling the original allocator-backed
copy, rejects shared row-name storage and preserves every other byte of all
25 rows, including Ready, network slot, controller and kind. This helper grants
no configuration-consumption, Ready, start, NEO or save authority.

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
- 24 bounded executions of Gold's original CString assignment, assignment
  helper and reserve routine cover empty and 64-character names, reused storage
  and original allocation growth. Only malloc/free/memcpy are modeled. Original
  code produces an independent owned copy and preserves the source and ABI.
- Seventeen compiled integrity-guard checks cover all five original row/string
  method regions; each complete region matches the actual prepared executable.
- Thirty authored identity checks cover register/stack ABI, native-owned string
  layout, full allocation accessibility, row preservation and fail-closed scope.
  A readable current name with claimed growth into a no-access page is rejected.

Run `native/build_crossplay_reverse.ps1` with a fresh directory under `work/`.
Fixed-address guard images are built only, never executed natively by that
script. These are component and headless-code results. No reverse game, common
world start, NEO logistics, original History-host save or coordinated reload is
claimed by them. No adapter or catalog pin is promoted.

## Actual native admission diagnostic

A separate private DXVK pair now captured the original Steam host bootstrap
and completed the native roster transaction with Steam as host/local slot1 and
Gold as guest/local slot2. Gold's original preparation getter/writer agreed;
row2 was still unassigned (network slot unavailable, controller1, human kind1).
The independent Gold SDK service kept its existing source2 guard, separately
from the role-derived common-channel source.

Both original clients exited0 and their guarded files, portable profiles,
auxiliary files and services were restored. The diagnostic deliberately stopped
before host-configuration consumption, guest row mutation, Ready, start, NEO or
save/load. It does not qualify a reverse world or the third final-package case.
The ordinary parser and installer still reject this direction.

## Actual claimed Gold guest and identity readback

A subsequent private original-game pair invoked Gold's original row prepare and
claim routines after native admission. It then copied the actual local native
participant's name through the original CString assignment and projected that
participant's observed preparation ID. After an original Confirmed consumer
returned, row2 had network slot2, controller3 and human kind1; profile ID, name
and originally captured castle all matched the admitted local participant.
All other bytes of the 25 preparation rows were preserved. The successful
repeat used the complete-capacity storage guard described above.

Both clients again exited0, with complete guarded-file, portable-profile,
auxiliary-file and service restoration. The full candidate contents were hashed
before launch: 123 Gold and 57 Steam History asset files, plus the catalog's
remaining required assets. A separate attempt stopped before game launch at the
unchanged RAM guard and is not counted as a passing pair. The planned guest
close's channel rejection is retained in the measured evidence.

See the [sanitized identity-only receipt](receipts/native-reverse-guest-identity-20261117.json).
This is native admission and identity readback, not consumed host configuration,
Ready, a shared world, active NEO, an original History-host save or a reload.
Ordinary parser/runtime/installer admission and final-package gates remain closed.

## Remaining native path

The runtime still needs complete History host configuration/start capture,
Gold guest configuration-consumer receipts and readback, original human
Ready exchange, host/guest setup-root agreement and full NEO lifecycle. Its save
checkpoint must come from a new original Steam History host save, followed by
a fresh-owner transfer/load, matching original turns, continued logistics and
resave. The local Gold SDK service remains separate from common host authority.

The final installed package must pass all three required role pairs. The
forward diagnostic receipts and these headless component checks cannot replace
that qualification. Normal speed and DXVK remain mandatory.
