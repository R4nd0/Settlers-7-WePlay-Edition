# Launcher audit fixes

Implemented after the [three-agent audit](LAUNCHER-ASTRA-REVIEW-20261109.md).
These changes address every finding from that audit, including the additional
settings-recovery blocker. They do not replace the published RC3.23 assets.

| Finding | Result |
| --- | --- |
| A1: public Join invalidates its own selection | The real click handler validates and captures the fresh selection before entering the exclusive operation. Backend admission checks remain in place. |
| A2: public End does nothing while busy | The cancellation-only handler reaches cooperative cancellation during the active operation. |
| R1: recovery conflict blocks recovery tools | Read-only status and diagnostics bypass automatic recovery. The launcher offers explicit restore or new setup, with confirmation, verified durable backups and stale-input rejection. |
| A3: interrupted ReShade preparation blocks retry | Receipt-backed staging resumes an unchanged owned subset. Foreign, changed, linked and legacy staging survives. Publication requires a complete inventory. |
| A4: Discard restores an obsolete saved generation | Save returns authoritative committed settings separately from hardware refresh. The UI replaces the persisted baseline even when refresh fails. |
| A5: partial update deletion cannot resume | A durable deleting receipt accepts already-missing owned files while checking all remaining contents. Cleanup happens at preparation, outside the stoppable update-check path. |
| A6: candidate requests exceed the frontend budget | Candidate discovery shares a 120-second transfer budget, cancellation and progress. Verified candidates survive later transport/budget failures as incomplete results; signature, conflict and highwater checks remain strict. |
| A7: ReShade download cannot be cancelled | Bounded chunks, a 120-second overall deadline and cancellation precede pinned-hash validation. A read-only network worker cannot publish late results or write files. |
| Unsupported Modern presets | Recommended/Maximum are disabled with an explanation until a supported adapter is confirmed. |
| Repeated display enumeration | Save shares one coherent display snapshot across validation, selection and the response. Launch-time hotplug checks remain. |

## Recovery behaviour

Restore backs up current evidence before writing the previous managed
generation. New setup preserves game copies and saves and removes their current
launcher registration, allowing setup to run again. It does not delete the
installation. A durable detach marker supports retry after interruption;
changed registrations, markers or backups prevent unsafe resumption.

Recovery status exposes fixed logical field names instead of raw paths or
settings. Unreadable, oversized, linked or insufficiently authenticated recovery
evidence remains available for manual recovery. The launcher explains that case
instead of offering a destructive blind reset. Restore retains full package
verification; detach remains available independently of a damaged package.

## Verification

Product source `e8b251a652379be47185f96dbaf85f50cb76e8c6` passed
[CI run 42595684297](https://github.com/R4nd0/Settlers-7-WePlay-Edition/actions/runs/42595684297):
Windows launcher, Windows native adapter, Ubuntu 22.04 and Ubuntu 24.04.
This includes the built UI/child-process tests and actual HTTP/UDP service tests.

The source regression runs cover desktop settings and integrity, updater
selection/deletion/recovery, ReShade preparation and download interruption,
installer/standalone boundaries, public multiplayer startup and directory
behaviour. Final defensive changes have dedicated delta tests. Three optional
Midsummer Lua tests require a Lua 5.1 runtime and were skipped locally.
Windows runs also skip Linux-only cases and opt-in compiled-handoff fixtures;
these skips are recorded separately from the executed signed-helper validation.

The built Windows launcher passes 37 UI checks, including actual Join/End button
clicks, save/refresh-failure/edit/Discard/save, supported and unsupported presets,
and conflict/diagnostics/rejected confirmation/stale token/restore/new setup.
Minimum-window action geometry and text scaling at 125/150 percent are checked;
the recovery rendering was inspected. The private pipe roundtrip uses an actual
child backend. Another 67 checks cover owned child lifetimes, cancellation,
bounded hangs and protected commit/game phases.

A private development package is inventoried against the current source and
checked with its bundled isolated Python. Existing signed RC3.23 metadata and
installer bytes also pass both the Python verifier and the compiled Windows
update helper; the installer is not executed.

Independent final review of recovery, staging and download found one additional
missing-completeness guard at staging publication; it was fixed and covered by
a remove-before-publication/retry regression. Independent updater review found
no further blocking issue. Evidence and source hashes are recorded in
[the verification receipt](verification/launcher-fixes-20261109.json).

No game, live multiplayer session or live download was used for these fixtures.
No measured FPS, gameplay latency or physical-monitor DPI improvement is claimed.
OS DNS/filesystem stalls remain outside a universal network-timeout guarantee;
the read-only updater retains its frontend watchdog. Public release assets,
native adapter, content format and network protocol versions remain unchanged.

The separate [UDP/QUIC assessment](UDP-QUIC-ASSESSMENT.md) concludes that S7
already uses UDP. A measured outer QUIC-DATAGRAM prototype is technically
possible, but a reliable stream tunnel is not a demonstrated improvement.
