# RC4.00 final installed mixed reload gate

RC4.00 is not publishable until the final installed package has completed genuine
mixed reloads for Gold hosting each original History edition and for original
Steam History hosting Gold. Private Native162
results, authored fixtures, a visible lobby and a successful installer build do
not satisfy this condition.

The maintainer signer enforces this specific release as technical version
`2.0.0-rc4.0`, sequence `29`, tag `v2.0.0-rc4.00`. Its checks run before signing-key
selection or any signing subprocess. Existing earlier releases keep their
existing signing contract.

## Required final artifacts

The completed `Setup.exe` retains its normal `Setup.release.json` build receipt.
For RC4.00 that receipt must identify a clean source commit, its actual package
inventory ID and an enabled, verified `crossplay_mixed` package. The builder's
optional `--crossplay-catalog` input enables that capability only through the
existing approved catalog and shipped-adapter validation. It cannot promote a
development DLL or a self-generated compatibility digest.

After installing this exact Setup and completing all three real reload tests, retain
these reviewed, sanitized receipts beside it:

| Receipt | Purpose |
| --- | --- |
| `Setup.mixed-reload.json` | Exact final Setup hash/size, source commit, package ID, approved catalog hash and the three case-receipt hashes |
| `Setup.mixed-reload-ubisoft.json` | Gold host / original Ubisoft History guest, edition 2 |
| `Setup.mixed-reload-steam.json` | Gold host / original Steam History guest, edition 3 |
| `Setup.mixed-reload-steam-host.json` | Original Steam History host / Gold guest |

The aggregate and case formats are version 2. Each record and each case carries
strict integer `host_edition` and `guest_edition` fields. The required order is
`(1,2)`, `(1,3)`, `(3,1)`. The old forward-only version-1 schema cannot qualify
the expanded scope. The reverse case must record the actual Steam host's own
new save and mixed reload; repeating a Gold-host record cannot substitute for it.

The exact schemas and checks are in
[`mixed_release_gate.py`](../tools/updates/mixed_release_gate.py). Each case binds
the installed application back to that Setup and package ID, the exact original
executables and the admitted ordinary production adapters. It records the actual
host-written save, completed mixed reload, continued logistics, nonzero shared
original native checksum blocks without mismatch or synthetic turns, preserved
NEO selection/state, normal exits and verified restoration. Laboratory features
and forced termination disqualify a passing case.

`reviewed_evidence_sha256` identifies the reviewed evidence manifest for that real
run. Preserve its underlying diagnostics privately; public receipts contain no
game data, save content, identities, invitations, keys or local paths. The
manifest must come from the actual guarded controllers and native observations,
not from the authored fixtures used to test the gate.

Changing Setup, its source/package identity, any case receipt, catalog or
production adapter invalidates the binding. A new final package requires its own
installed qualification. The gate checks evidence identity and completeness; it
does not establish that the game ran or replace review of native diagnostics.

## Release text and assets

Generate the final bilingual notes and release graphics from the qualified
package and these observed cases. Describe only the admitted player/map scope,
actual save/reload behavior and NEO features. Keep mixed NEO19 separate from
History-only NEO5. The existing single-player draft poster is not a final RC4.00
asset. No release tag, signed update metadata or publication is permitted while
any required final case is missing. Internal gate, test and development text does
not belong on the customer-facing posters.

## Gate verification

Nine authored gate tests pass, including rejection of missing/duplicate History
cases, changed installer or evidence bytes, nonproduction adapters, synthetic
turns, failed reloads, dirty builds, abnormal exits and a missing or mislabelled
Steam-host case. All 21 updater/signing
regressions pass using a temporary fixture key. The actual earlier single-player
RC4.00 candidate was separately rejected before any signing process or production
key access. None of these checks is a new mixed-game result.
