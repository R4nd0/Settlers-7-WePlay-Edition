# RC4.00 final installed mixed reload gate

The user's updated release requirement (2026-11-17) is one simultaneous mixed
session containing **original Gold, original Ubisoft History and original Steam
History, three human clients plus one AI opponent**, with genuine saving, loading
and stable continued play. Separate two-client pairs do not satisfy it, even when
both History editions have independently passed. The previously requested Gold
and Steam History host directions remain required; each now uses this same
three-edition roster. All clients must come from the exact final installed package.

Existing native pair results, authored fixtures, a visible lobby and a successful
installer build remain intermediate evidence, not release qualification.

RC4.00 also includes the verified fix for the reported three-client Ubisoft
History desync. Verify its regression using the final production payload,
including the existing History mode 3 resume correction, before publication.
The package inputs and current status are recorded in
[RC4 adapter migration](RC4-ADAPTER-MIGRATION.md).
The [edition-by-edition desync audit](RC4-DESYNC-COVERAGE.md) records earlier
failure mechanisms, the installed correction or equivalent original behavior,
and which cases still require evidence from this package.

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

After installing this exact Setup and completing both three-client host cases, retain
these reviewed, sanitized receipts beside it:

| Receipt | Purpose |
| --- | --- |
| `Setup.mixed-reload.json` | Exact final Setup hash/size, source commit, package ID, approved catalog hash and both case-receipt hashes |
| `Setup.mixed-reload-gold-host.json` | Gold host, original Ubisoft History and Steam History guests, one AI opponent |
| `Setup.mixed-reload-steam-host.json` | Original Steam History host, Gold and original Ubisoft History guests, one AI opponent |

The aggregate and case formats are version 3. Both records and cases carry strict
integer `host_edition` fields, ordered Gold (1), Steam History (3). Every case has
exactly three `participants`, ordered by edition 1/2/3, with one host and two
guests, `human_players=3` and `ai_opponents=1`. Version-1 and version-2 pair schemas
are rejected. The Steam-host case must use that host's actual new save; repeating
a Gold-host run cannot substitute for it.

The exact schemas and checks are in
[`mixed_release_gate.py`](../tools/updates/mixed_release_gate.py). Each case binds
each installed client back to that Setup/package ID, its exact original EXE and
admitted ordinary production adapter. Each participant must identify the same
initial session, a distinct shared reload session and the actual host-save source
digest. This save digest identifies source provenance; it does not assume that
edition-specific local save containers have byte-identical wrappers.

All three must actually load through their original loader, observe continued
logistics and the AI opponent, retain NEO state and exit normally. Common original
native checksum blocks must cover all three editions both before saving and
after reload, with no mismatches or synthetic turns. The measured post-reload
duration and reviewed stability result are retained. Normal speed is mandatory;
accelerated x20 runs cannot qualify. A positive counter or declared duration
alone does not prove stability: the actual continued game diagnostics require
review. Laboratory features, forced termination and failed restoration disqualify
a case.

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
package and these observed cases. Show all three editions together with three
human players plus one AI opponent, and actual save/reload continuation. Old
two-human/two-AI posters and pair-only feature text are superseded. Describe only the admitted player/map scope,
actual save/reload behavior and NEO features. Keep mixed NEO19 separate from
History-only NEO5. The existing single-player draft poster is not a final RC4.00
asset. No release tag, signed update metadata or publication is permitted while
any required final case is missing. Internal gate, test and development text does
not belong on the customer-facing posters.

## Gate verification

Eleven authored gate tests cover missing/duplicate clients, separate session/save
identities, wrong AI/human counts, old pair schemas, missing shared three-client
CRCs, unreviewed continuation, wrong installed payloads, abnormal exits and the
Steam-host case. They also retain rejection before signing-key access or signing
subprocesses. These are invented fixtures, not three-client gameplay evidence.
The actual earlier single-player candidate remains ineligible. No production key
or release metadata is accessed to perform these gate tests.
