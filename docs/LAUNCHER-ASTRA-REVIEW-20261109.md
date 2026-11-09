# Fresh launcher review by three Astra agents

Date: 2026-11-09. Audited source:
`77d4d9b826d15a3c60b2540523370a0a4b2d6c68`.

Three independent `gpt-6-astra` agents received the same broad question: what
can make the current launcher faster, clearer, more stable and more resilient,
and where are its weakest or least efficient paths? They were not assigned
exclusive specialisms. Each inspected the current code and the previous
hardening rather than reusing historical findings as current defects.

This review changes no product code or release assets. Its findings concern
the hardened source/private build on main, not a newly published binary release.
Source links below pin the audited commit. TokenSave was used for navigation;
current file reads were authoritative because the index was stale/rebuilding.

## Highest-priority findings

### A1 - P1: the normal public Join click rejects its own selection

Found by Astra 2; confirmed with the real button handler in the existing private
launcher assembly, using a synthetic list and `test=true`.

The [Join button](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/desktop/MainForm.Directory.cs#L40)
uses the exclusive handler. [Guard](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/desktop/Program.cs#L965)
calls `SetBusy(true)` before running the action. That
[invalidates the list](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/desktop/Program.cs#L945),
setting `directoryLoadFailed=true` whenever rows exist. Then
[JoinDirectory](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/desktop/MainForm.Directory.cs#L181)
refuses the operation because that flag is now true.

Observed with `Button.PerformClick`: an enabled button and a fresh compatible
row become the error "Diese Partie ist voll, geschlossen oder nicht mehr
erreichbar ...", with the stale flag set. No backend or network was started.

Fix direction: validate selection/freshness before entering the exclusive
operation, capture the selected ID and pass it to the guarded join operation.
Retain independent admission and integrity checks. Test the complete click
chain, not only `Enabled` or `JoinDirectoryGame` in isolation.

### A2 - P1: public Leave/Cancel is enabled but does nothing

Found and dynamically confirmed by Astra 2.

The [public end button](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/desktop/MainForm.Directory.cs#L51)
also uses the default exclusive handler. During an active public room,
`busy=true`; `Guard` returns immediately. Therefore the handler never reaches
`RequestCancellation`, even though the UI explicitly enables the button while
waiting outside the game.

Observed: `enabled=True`, `busy=True`, then `PerformClick` leaves both the UI
cancel flag and the cancellation file unset. As a positive control, direct
`RequestCancellation` against the same inert synthetic session sets both.

Fix direction: use a cancellation-only direct/nonexclusive handler, preserving
the existing checks that a room is active and no game is currently running.
Exercise this through the actual button event while an operation is busy.

### R1 - P1: a settings recovery conflict blocks the recovery tools themselves

Additional root verification, separate from the three agents' original reports.

Keeping foreign edits is correct, but
[dispatch](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/installer/desktop_backend.py#L1130)
attempts settings recovery before routing almost every command. When a retained
journal conflicts with a legitimate intervening file edit,
[Transaction.check](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/installer/desktop_settings.py#L89)
raises `preferences_invalid`. Diagnostics, repair and reset cannot reach their
own handlers because they encounter the same conflict first.

Reproduced using the existing invented DesktopResilienceTests fixture:
`snapshot`, `diagnostics`, `repair` and `reset-installation` all fail before their
handlers are called. Foreign bytes and the journal remain intact. No actual
game, user profile or account data was involved.

Fix direction: expose a non-mutating recovery status/diagnostic path outside
automatic recovery. Offer an explicit, backed-up conflict-resolution workflow
that can quarantine the journal or accept a chosen generation. Do not silently
delete the journal or overwrite the foreign edits. Test a full recovery journey
after the conflict, rather than only testing refusal to overwrite it.

## Further concrete defects

### A3 - P2: interrupted ReShade preparation poisons subsequent attempts

Independently found by Astra 2 and 3, with synthetic fault reproduction.

[prepare](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/installer/reshade_runtime.py#L298)
uses a deterministic `.preparing` directory. It refuses an existing staging
directory but has no recovery/cleanup path for failures after creation. A write
error or abrupt exit therefore blocks every retry of the same effect profile.
This happens [before the settings journal begins](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/installer/desktop_backend.py#L612).

Observed: injected `OSError` on the first attempt; the next attempt stops at
the retained staging check with "Eine unvollstaendige ReShade-Vorbereitung ist
vorhanden." It cannot reach the preparation writes. Staging remains and no
final profile exists. Only synthetic
runtime/setup bytes and local fixtures were used, with no download or game.

Fix direction: receipt-backed staging and safe resumption/quarantine, with
cleanup restricted to files demonstrably created by that operation. Test faults
and restart at each preparation step; unknown files must still survive.

### A4 - P2: saved-after-refresh-error has an obsolete Discard baseline

Independently found by Astra 1 and 3; source-proven state sequence, not dynamically
reproduced in this review.

[SaveGraphics](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/desktop/Program.cs#L741)
accepts the current draft on `settings_saved_refresh_failed`, but retains the
old `snapshot`. After another edit,
[DiscardGraphicsDraft](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/desktop/MainForm.GraphicsDraft.cs#L46)
loads that old snapshot and marks it saved. The backend has the new generation;
the UI displays the previous one as authoritative. A later save can overwrite
the successful generation unintentionally. Fallback has the same pattern.

Fix direction: return the committed settings generation independently from the
hardware snapshot, or mark the baseline unknown and require a successful reload
before Discard. Test save success, refresh failure, another edit, Discard, then
Save, with the backend generation checked at every step.

### A5 - P2: partial update cleanup becomes permanently uncollectable

Found and synthetically reproduced by Astra 1.

[remove_owned_job](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/installer/update_client.py#L248)
unlinks files one by one without updating the ownership inventory. If a later
unlink fails, the [next reaper pass](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/installer/update_client.py#L280)
rejects the incomplete file set permanently. The probe removed one owned file,
failed the second deletion, then removed the fault: the next pass still removed
zero jobs and retained `Setup.exe`.

There is also a policy mismatch: `update-check` is
[hard-stoppable as a read request](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/desktop/Backend.cs#L26),
but [check invokes this cleanup](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/installer/update_client.py#L297).
A watchdog stop can interrupt those mutations.

Fix direction: resumable deletion state that accepts already-missing owned
files while validating every remaining file. Move maintenance out of the
read-only request or protect its mutation boundary appropriately. Preserve
unknown, modified and active contents.

### A6 - P2: update candidate budgets exceed the frontend deadline

Found by Astra 1; source-proven budget mismatch, no slow-network run.

The frontend has a [three-minute idle limit](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/desktop/Backend.cs#L15).
The backend serially checks up to eight candidates, each with a 90-second
transfer budget; `check_updates()` does not supply progress. A valid candidate
is [cached only after the whole loop](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/installer/update_client.py#L336).
Slow legitimate later candidates can therefore cause the frontend to terminate
the operation before preserving an already verified newer offer.

Fix direction: a shared overall budget, cancellation-aware bounded candidate
fetches and defined partial completion. Retain signature, conflict and highwater
checks; heartbeats alone are not a substitute for an overall deadline.

### A7 - P2: first ReShade download is outside cooperative cancellation

Found by Astra 3; source-proven wait, no network probe.

[urlopen plus one large read](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/77d4d9b826d15a3c60b2540523370a0a4b2d6c68/tools/installer/reshade_runtime.py#L281)
has a per-socket timeout, not a total transfer deadline or cancellation callback.
A slow continuous stream can hold Save busy for an extremely long duration
without an application-defined overall deadline.
The watchdog intentionally cannot hard-kill Save, and the transfer does not
check its cancellation file. This wait occurs before the critical settings
commit, so it can be made safely interruptible.

Fix direction: bounded chunks, a total deadline, cancellation and progress,
followed by the same pinned hash verification before committing anything.

## Lower-priority UX and performance opportunities

Astra 1 identified that Recommended/Maximum presets remain available with no
confirmed DXVK adapter and explicitly select Modern. The backend correctly
rejects unsupported Modern. Disable unavailable presets with an explanation,
or derive an appropriate recommendation from confirmed support; keep the
backend guard. This is a UX issue, not a request to weaken validation.

Astra 3 noted repeated display/mode enumeration within one Save (validation,
automatic GPU resolution and snapshot). A consistent hardware snapshot per
operation could reduce work and avoid internally inconsistent hotplug reads.
Its latency was not measured, so it ranks below the reproduced workflow defects.

## Independent coverage and evidence

| Reviewer | Strongest findings | Additional execution |
| --- | --- | --- |
| Astra 1 | A4, A5, A6 and unsupported-preset UX | Partial-delete updater fault |
| Astra 2 | A1, A2, A3 | Actual Join/End `PerformClick`, direct-cancel positive control, ReShade write fault |
| Astra 3 | A3, A4, A7 and display-enumeration opportunity | Independent ReShade write fault |
| Root consolidation | Source cross-checks and R1 | Conflicted-journal routing across four recovery/diagnostic commands |

Private evidence is retained under `work/launcher-astra-review-20261109/`:
agent1 `probe_retention.py`/`retention-result.json`; agent2 `Program.cs`,
`probe-results.txt`, `reshade_probe.py`; agent3 `probe_reshade.py`; root
`probe_conflicted_recovery.py`/`recovery-result.json`.

No product code, released assets, game files, account data or production services
were modified. UI probes used the existing private assembly in test mode and
synthetic rows/session state, not a live multiplayer session. There was no new
native-game, hardware-performance, physical DPI, screen-reader or live-network
qualification. Source-only findings are labelled separately from executed ones.

The earlier removal of full-package lobby hashing, bounded IPC, protected game
lifetimes, integrity/ownership guards and visible action rows remain useful
improvements. The new results particularly expose missing end-to-end workflow
and interrupted-recovery tests. Fix A1/A2/R1 first; then cover ReShade staging,
generation consistency and updater cleanup/budgets before another release.
