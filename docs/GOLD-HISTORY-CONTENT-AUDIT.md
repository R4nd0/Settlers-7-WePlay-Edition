# Gold / History archive content audit

The standalone crossplay laboratory now has a reproducible, read-only archive
comparison in `tools/gold/content_audit.py`. It compares explicit archive scopes
by entry names, decoded sizes and SHA256 digests. A successful audit is not a
complete dependency manifest, native setup acknowledgement or world permission.
The existing network admission/content identities are unchanged.

## Input and memory use

The tool accepts a JSON specification containing two to eight named profiles.
Each profile selects the same named scopes, with an archive path, exact outer
SHA256 and optional directory prefix. For example, a Monhaim scope uses
`Maps/Skirmish/s04_m06_Monhaim_Valley/`. Prefixes must end at a directory boundary
and select at least one file. The output must be a fresh file under repository
`work/`; archives are never opened for writing.

Each input fingerprint is checked before parsing. The parser borrows a read-only
mapping rather than copying the entire archive. The optional `Archive(...,
copy=False)` API rejects writable or noncontiguous buffers, retains existing
directory and decoded payload CRC/size checks, and requires the caller to keep
the mapping open. Ordinary callers retain the copying default. Replacements
still create new bytes and leave the mapped input untouched.

The audit rejects unsafe or case-ambiguous entry paths and retains the original
v6 root-directory metadata row. It checks file identity/size/timestamps again
after reading. Comparison uses the union of entry names, so missing and extra
files cannot disappear through an intersection-only comparison. Container
compression or appended replacement history can differ while decoded entries
remain equal. Different scope prefixes cannot be compared as the same scope.

Run with:

```powershell
python tools/gold/content_audit.py work/audit-spec.private.json work/audit.private.json
```

Paths are input-only; the report contains entry metadata and digests, never
decoded game scripts or input filesystem paths. Parsing or comparison failures
do not publish an output file. Archive corruption still stops the operation.

## Observed comparison, 2026-11-12

The audit used the Gold graphics-port test data and the two History guest-map
Interface candidates used in the verified lobby experiments. The History data
sets use their respective shared and map archives.

| Scope | Union of decoded file names | Differing entries |
| --- | ---: | ---: |
| Selected skirmish Monhaim directory | 15 | 0 |
| Complete Interface archive | 284 | 58 |
| Complete shared archive | 733 | 13 |

The shared differences include the Gold graphics candidate's additional
environment definition, key bindings and translations. Interface differences
include menu/controller code and `Script/globalState.lua`; Gold additionally
loads `global/start.lua` there. No difference is automatically ignored or
normalized into a compatible content ID. Bootstrap behavior, actual mounted
dependency closure, engine behavior and complete native setup agreement still
need qualification before a common world can be released.

The actual three-profile audit completed with a measured peak process commit
of 239.0 MiB and peak working set of 262.4 MiB. These are measurements of that
Python audit process, not game memory requirements or gameplay performance.

Twenty-five synthetic filesystem/archive tests passed; two existing optional
tests were skipped (real installation integration and junction creation in
the restricted environment). They cover read-only mapping ownership, mutable
input rejection, corruption, root metadata, changed/missing/extra entries,
fingerprint-before-parse behavior, scope boundaries and invalid profile shapes.
The real archive comparison ran separately. See the
[sanitized receipt](verification/gold-history-content-audit-20261112.json).
