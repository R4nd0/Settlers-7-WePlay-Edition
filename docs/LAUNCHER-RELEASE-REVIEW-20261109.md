# Fresh release review and corrections

Three fresh independent Astra agents received the same broad launcher quality,
performance, appearance and resilience question against `ac3fbf7` (product
`e8b251a`). They read current source and the previous audit/fix reports, with
isolated synthetic probes. Root prepared RC3.24 versioning in parallel.

The new round confirmed three additional defects. All three are addressed for
RC3.24; the previous [launcher findings](LAUNCHER-FIXES-20261109.md) remain fixed.

| Finding | Reproduction | Correction |
| --- | --- | --- |
| B1 / P2: malformed settings journal bypasses the recovery screen | Actual backend dispatch with a non-JSON journal produces a generic IPC error; the old UI's narrow error-code filter hides available recovery. | Normalize journal parse/schema errors to `preferences_invalid` and consult read-only recovery status for every backend error. Keep the original error when no recovery is pending. |
| B2 / P2: abruptly interrupted download leaves an unreclaimable job | An owned synthetic child writes `Setup.partial`, then exits abruptly; the initial empty inventory prevents later retention cleanup. | Durable bounded download receipts authenticate the prefix and pending write. Interrupted preparation remains reclaimable without adopting foreign bytes. Receipt transitions also cover later verified-file/helper publication. |
| B3 / P2: late update cancellation still launches the handoff | A synthetic `prepare` creates cancellation after its last progress report; the old backend nevertheless calls `start_handoff`. | Carry cancellation through preparation, check before the authoritative handoff and safely clean up the owned job. Protect the handoff phase once it starts. |

## Boundaries and tradeoffs

The download receipt contains a hash/length and at most one MiB pending block in
private job state. It permits authentication after a partial final write. The
running SHA256 state avoids repeatedly hashing the entire prefix. Receipt I/O is
linear and adds roughly 1.33 times the download size in encoded block writes,
plus bounded publication records. Receipt size is bounded; it is not exported in public
diagnostics or verification files. This is recovery work, not a claim of faster
download throughput. Legacy unauthenticated, modified, linked, unknown or active
jobs remain preserved.
Unknown temporary files left by an interrupted atomic receipt write also remain
preserved; this does not promise automatic deletion after every possible disk or
power failure. An owned-connection watchdog observes cancellation while HTTP
headers or body reads are blocked and closes only that connection.

The review did not justify changing the game protocol or replacing the launcher
framework. A visible dedicated cancel control during graphics saving remains a
possible UX refinement; the existing cooperative cancellation route is available.
It was not classified as a release defect.

## Verification

Regression coverage includes real backend JSON-IPC serialization, actual
`Initialise()` error/fallback paths, conflict-to-detach-to-setup, early/late update
cancellation, cleanup failures and protected handoff. Download crash tests use
an actual owned synthetic child and invented bytes, including partial writes,
retention limits and foreign-file preservation. Another agent independently
reviewed the final receipt/cancellation changes and approved them: eight targeted
receipt/handoff cases and three header/streaming cases passed. The integrated
updater/backend run passed 41 tests; the real signed-updater suite passed another
19. The built launcher passed 37 UI checks with a real private IPC roundtrip.

The [RC3.24 release guide](RC3.24-LAUNCHER.md) and its publication receipt record
the final source, CI and artifact checks. Private probes remain under
`work/release-review-20261109/`; no game, account data or production services were
used by the reviewers. Source-only observations are distinguished from executed
probes; no gameplay, FPS or network-latency improvement is inferred.
