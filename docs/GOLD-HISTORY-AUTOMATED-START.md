# Gold/History automated pair-start candidate

The opt-in laboratory driver stages automatic session creation and original
Ready/Start controls for Gold and both History data sources. Ordinary launchers
do not enable it; only explicit laboratory archive staging installs its callbacks.
Automatic browser entry, native session creation and normal timed exit have
been observed in original Steam History under DXVK without human input.
The corrected driver also created both native lobbies, admitted the History
guest, activated its original Ready control and Gold's original Start control
without input. Shared world entry and mixed gameplay remain unqualified.

`tools/gold/native_pair_driver.lua` waits for the real login and original
session browser, resolves a unique unlocked Monhaim map through engine metadata,
loads it and checks the actual native map before creating a session. Gold keeps
its original seven creation arguments; History keeps its eight. A real boolean
success selects the original host-lobby action. A suspended job cannot create
another session through reentrant updates.

Login alone does not authorize automatic creation. The staged browser exposes
its existing `lastDiscoveryFinished` and idle-result state. The driver waits
until the original native `notifyDiscoveryEnded` callback has fired and the
original browser has consumed the discovery result. Empty completed lists are
valid; a newly created or still-waiting browser cannot trigger creation. This
retains the native create-readiness guard and does not retry rejected requests.

Map discovery also runs inside that reentry guard. It retains the original
map browser's literal `GetSkirmishMaps(true)` call in all three editions,
without reading a separate global release flag. A missing global is covered by
the Lua test. This corrects a difference from the earlier experimental driver;
The corrected candidate subsequently completed native Steam History creation;
the individual contributions of the frame owner and discovery corrections have
not been isolated in separate native experiments.

Unlike the older unqualified `native_lobby_driver.lua`, this candidate does
not wrap native jobs in `pcall` or `xpcall`. The Lua 5.1 suspension test exercises
both map loading and creation across yields. This removes a concrete protected
call boundary; it does not yet prove the cause of the older real-engine stall.

History's Ready action requires the actual guest page, guest role, local slot 2,
an unready native slot and its existing enabled button. Gold's Start action
requires the actual host page and role, another human, different teams and the
original all-humans-ready/enabled-button conditions. Both activate the original
button callbacks. Native admission, preparation, world-start and event-method
guards remain responsible for accepting their results. The driver never writes
Ready results, progress, turns, acknowledgements or checksums.

`tools/gold/pair_watchdog.lua` runs from the original local frame entry point,
independently of menu/world state changes. Once per second it logs the original
`Game.GetCurrentTurn` and `Game.GetGameTime` results when in map mode. Its bounded
deadline requests the game's normal `system.exitApi`; it does not terminate an
OS process. A stalled engine can still prevent frame callbacks from running.

`tools/gold/pair_frame_loop.lua` owns the registered driver independently of
individual menu updates. The archive declares its registration function before
script protection and preserves the original frame callback. Original menu
updates run first; the registered driver runs afterward without a protected
Lua call boundary. A normal exit request prevents new native work. The frame
owner was exercised by the successful native Steam History creation run below.

`tools/gold/pair_interface.py` only stages new archives from three exact input
fingerprints: the quiet Gold interface and the separately prepared Steam and
Ubisoft native guest/map-reload interfaces. It changes the browser, three lobby scripts
and local frame entry point; Gold also receives the existing cumulative-turn
camera correction from `graphics_assets.focalizer_script`. It verifies every
other archive payload and writes the candidate/source/script hashes to a private
receipt. It never deploys an archive or edits an installation. New pair runners
must bind the actual candidate archive fingerprints in their diagnostic content
contract; these fingerprints do not establish compatible resource contents.

The candidate passes 23 Lua 5.1 tests covering creation/actions, the watchdog
and frame ownership: suspended and resumed jobs, reentry, native failure,
wrong loaded maps, role/slot guards, menu callback replacement and once-only
normal exit. All 16 changed scripts across the three staged archives compile.
The three complete original core scripts also execute with a mock engine:
changing the menu callback retains the driver, and normal exit runs once.
Gold's cumulative-turn rotation expression is absent. These are headless code
checks, not successful native shared turns, NEO or mixed save/reload evidence.

Three original Steam History runs required no human inputs. The first reached
the real session browser, then exited normally at its deadline without creating
a session. Two shorter follow-ups also exited normally with clean memory
observations and byte-exact restoration of the executable directory, launcher,
local profile/runtime files and interface; App and shared saves/profiles were
unchanged. Gold was never launched. Native metadata confirmed the global frame
watchdog was running, but the older menu-owned driver's last phase remained
`browser-requested`. The independent frame owner above addresses that update
dependency.

A fourth original Steam History run with the independent frame owner and
corrected discovery call completed `map-discovery`, `map-loading`,
`session-creating` and `created`, and wrote the native preparation receipt.
Its original creation call returned success; the driver did not supply that
result. The game exited normally with code zero and all restoration checks
passed. Gold was not launched. The History ownership receipt appeared about
207 seconds after the pair controller's startup checkpoint, beyond its old
180-second prelaunch budget. The controller's original exception was not retained,
so this timing identifies an exceeded budget without proving the lost exception.
The next private runner allows 600 seconds for ownership and records durable
controller/launcher phases, including preflight. The in-game 180-second normal
exit watchdog and native acceptance guards remain unchanged.

The fifth original Steam History run reached the native create call, which
returned false; Gold was therefore not launched. The native status log records
`local-session-create-not-ready` before the later search-ready transition,
whereas the successful fourth run records `local-session-create-ready`.
This demonstrates that the driver could request creation before native discovery
readiness. The browser-completion guard above addresses that observed ordering;
the sixth run exercised it successfully. The fifth game exited normally
with code zero; all restore checks passed and no game process remained.

The sixth run created both original Steam History and Gold sessions under DXVK.
History first reported `waiting-discovery`, then completed native creation.
Both adapters observed the two-member roster, with Gold hosting in slot 1 and
History joining in slot 2. The original History Ready callback ran, Gold consumed
the real ready event, and its original fresh-start callback reached the guarded
start hold. The authenticated broker relayed 17 records with zero rejections.
The run then stopped at History's `native-closed-slot-original-factory-excluded`
guard after the closed configuration and slot events arrived. Neither world
entry nor simulation turns were observed. Both games exited normally with code
zero and all restoration checks passed. No human input was needed.

A seventh run repeated that automatic sequence using a private adapter which
waits up to ten seconds for the unchanged closed-slot context predicate.
Its byte/method, owner, roster and configuration-field checks passed, while both
human entries in History's session cache still had their native Ready field
cleared. Before the slot-context deadline, the adapter rejected
`native-history-ready-original-projection-excluded`. That existing condition
combines guest-presentation readiness and Ready snapshot application, so this
run does not identify which of those checks failed. A further private diagnostic
separates them and records the Ready helper's original failure stage without
changing its checks or native call order. Both diagnostic builds pass compiler
and ABI checks; their next native test remains pending. The seventh run again
ended normally on both clients with complete restoration and no shared turns.

Real acceptance requires both DXVK pairs to create their own native lobbies,
perform original Ready/Start without human input, leave the shared loading
barrier and execute actual advancing simulation turns. Matching delivered EOT
batch fingerprints alone do not establish gameplay synchronization. Existing
executable, adapter, native ABI, loopback, profile/save backup and byte-exact
restoration requirements continue to apply.
