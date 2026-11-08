# Launcher responsiveness and recovery

Implemented following the [2026-11-08 review](LAUNCHER-REVIEW-20261108.md).
The review links retain the original audited source; this document describes
the resulting implementation. These changes do not alter the game protocol,
native adapter, graphics pins or previously published RC3.23 assets.

## Responsive read paths

Public lobby refresh has its own cancellation and generation state. It no
longer takes the global busy state or clears the visible list before a request
succeeds. Failed results are marked stale and cannot enable joining. Results
arriving after another operation or after closing are discarded. The request
does not replace the foreground operation's cancellation target.

Listing uses bounded, schema-checked display metadata and the current canonical
content identity. It does not inventory the package, read renderer journals or
recover transactions. Those display hints are never execution authority.
Joining and starting still perform the full guarded checks.

Full package verification is reused only within one operation. Changes in file
identity/size/times invalidate that context, as does its short age limit. It is
discarded at the operation boundary. An already-bound portable UI snapshot no
longer repeats full adoption checks; relocation, repair and actual execution
retain their verification boundaries.

Measured with the final private package and bundled isolated Python: three
listing calls performed **zero payload digest calls**, rather than reading the
roughly 178 MiB package on every refresh. The measured calls were approximately
2-3 ms with HTTP and game-identity fixtures. This is a package-I/O regression
measurement, not an end-to-end Internet or player-storage benchmark.

Automatic old-copy cleanup is removed from the blocking startup path. A count
of pending copies exposes a maintenance link to the existing guarded explicit
cleanup. Normal startup only creates missing shortcuts, using a separate STA
worker; it does not rewrite existing shortcuts on the UI thread.

## Backend lifetimes and errors

The frontend now uses buffered bounded-frame IPC, per-action inactivity limits,
cooperative cancellation and a single foreground operation owner. Direct child
termination is restricted to `directory-list` and `update-check`; process handle,
PID, start identity, executable path and hash are checked. It never kills a
process tree. Failed cancellation-file writes are surfaced and do not silently
orphan a stoppable request.

Explicit critical-write and live-game states protect commits and intentionally
running games. Other mutating operations remain cooperative and report a
one-time unresponsive warning when they cannot safely stop. This is deliberate:
the implementation does not claim that an arbitrary OS I/O stall during a
critical commit can always be terminated safely.

Language persistence and cancellation UI handlers have error boundaries.
Unsaved graphics edits remain separate from hardware/saved snapshots, survive
hardware refresh and have explicit Save/Discard/Cancel decisions. A successful
settings commit followed by a failed refresh has a distinct message.

## Recoverable settings saves

`desktop_settings.py` journals the renderer generation, graphics choice,
quality, player name, installation policy and ownership record under the same
game-session lock. The native renderer transaction retains its original guards.
Failure or restart restores a complete previous generation. Recovery validates
all fixed targets and compares expected bytes before restoring; foreign edits
are retained and block unsafe recovery.

The journal is removed only after the new generation is consistent. A later
snapshot failure cannot roll the committed generation back. Immutable ReShade
components remain checked, and generated runtime caches are not treated as
personal saves.

## Updates and interface

Transient transport failures for another release no longer erase an already
verified update. Incomplete checks are visible. Signature errors, conflicting
same-sequence content and high-water/antirollback checks remain enforced.

New update jobs carry a bounded ownership inventory. Failed fresh jobs and
inactive retained jobs can be reclaimed using age/count/size limits. Active
handoffs, unknown files, modified files and linked paths are preserved. Legacy
jobs without an ownership receipt are conservatively retained; the cleanup does
not adopt arbitrary old folders as its own.

Primary actions have fixed visible rows on the LAN, private/public Internet and
graphics pages. Graphics are grouped into display, image quality and additional
effects, with expandable explanations. Stale texture-sampling help is corrected.
Diagnostics derive actual application/source metadata and name content,
directory and NEO protocol revisions separately.

## Verification

The [verification receipt](verification/launcher-hardening-20261108.json)
records exact product-source hashes and the observed checks. Evidence includes:

- Existing desktop, graphics, installer, portable and network regressions.
- Settings write-fault/restart tests with both renderer transitions, tampering,
  foreign-edit preservation and post-commit refresh failure.
- Ten updater resilience cases using a public signed manifest and synthetic
  owned-job fixtures; separate existing Windows signing tests.
- Sixty real-child lifecycle checks on Windows and WSL, including malformed and
  oversized output, cancellation-path failure, protected phases and direct-child
  versus descendant behaviour. Test startup tolerances are separate from
  production timing policy.
- Thirty-one bilingual UI checks and a real named-pipe round trip. Minimum-size
  tests include occlusion checks; 125/150% tests scale geometry and text
  deterministically, rather than qualifying physical multi-monitor DPI changes.
- A fresh private package with 24 resilience tests using its actual bundled
  modules/interpreter, plus compiled validation of the signed existing update.
- A fresh Linux package and Linux path/installer/graphics-profile/CLI checks,
  without starting a game. Publication-race fixtures cover Windows rename and
  POSIX link paths; reparse-point fixtures also cover Python 3.12 stat access.

The CI workflow now builds the Windows launcher, runs its UI and real-child
tests, and covers persistence/update regressions. No new native game,
separate-PC multiplayer, FPS or physical DPI qualification is implied.
