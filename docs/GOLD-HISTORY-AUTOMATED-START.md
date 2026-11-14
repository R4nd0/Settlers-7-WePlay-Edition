# Gold/History automated pair-start candidate

The opt-in laboratory driver stages automatic session creation and original
Ready/Start controls for Gold and both History data sources. Ordinary launchers
do not enable it; only explicit laboratory archive staging installs its callbacks.
Automatic browser entry, native session creation and normal timed exit have
been observed in original Steam History under DXVK without human input.
The corrected driver also created both native lobbies, admitted the History
guest, activated its original Ready control and Gold's original Start control
without input. Both local world-start calls and initial EOT delivery have now
been observed with each History source. Both pairs now advance over 1,000 consecutive
simulation turns with matching original type-1 CRCs. Normal storehouse commands
from both players and normal street commands in both directions have also been
delivered. The animal movement-time correction below removes the reproduced
finished-turn-510 desync. With the curated native module and public construction
probe, Gold/Ubisoft matched 1,561 consecutive original CRCs and Gold/Steam matched
1,550; both new storehouses completed in both clients of each pair. Other
gameplay, longer construction stability, NEO and mixed save/reload remain
unqualified. See the [bounded verification receipt](verification/gold-history-movement-time-20261114.json).

A second animal path-point correction subsequently removed the reproduced
finished-turn-2792 divergence: Gold/Ubisoft matched 4,539 consecutive original
CRCs and Gold/Steam matched 4,949, with normal commands and completed storehouses.
Gold's loading overlay remained visible in the Steam run despite the advancing
world; these results do not establish complete Gold UI entry. See
[path-point arithmetic and limits](GOLD-HISTORY-PATH-POINTS.md) and its
[bounded receipt](verification/gold-history-path-points-20261114.json).

The later [Gold UI correction](GOLD-HISTORY-LOADING-UI.md) fixes the old quiet-chat
adapter that interrupted UI callback registration. Clean follow-up pairs close
the original loading overlay and retain normal payable construction: 1,571 equal
CRCs with Steam data and 1,488 with Ubisoft History. This does not qualify NEO,
four-player AI maps, mixed save/reload or the mixed bridge over EasyTier.

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
changing its checks or native call order. Both diagnostic builds passed compiler
and ABI checks before the following tests. The seventh run again
ended normally on both clients with complete restoration and no shared turns.

Two subsequent runs isolated that rejection to the guest-presentation gate,
before Ready application. Source inspection identified a circular dependency:
receiving the final closed configuration cleared `config_received` until all
slot and Ready fields matched, while Ready application required that same flag
to be set. The private correction admits Ready application during that one
authenticated pending closed-configuration transaction, using the original
owner/session/cache/map/deadline checks and original guest-preparation predicate.
It never marks the final configuration complete. Original Ready constructors,
consumers, closed-slot checks and full final configuration agreement remain in
place. Sixteen extracted-method lifecycle checks pass.

The next original Steam History/Gold run verified this correction: both native
Ready/cache consumers, both closed-slot consumers and the final configuration
postcondition completed. Both original local world-start calls returned. Gold's
20 load-progress events and History's 27 were received with matching sequences
and scalar bits, ending at the native 1.0 value. One original Gold EOT batch and
two original History EOT batches were delivered in both directions with matching
opaque-scalar fingerprints. These fingerprints do not establish equal native
simulation checksums. History's original counter stayed at turn 1 / 200 ms;
a PID/path/hash-guarded read of Gold's native clock found turn 0, unpaused.
Thus advancing common turns remain unqualified. The broker finished with 86
relays and one rejection whose cause has not yet been isolated. Both games exited
normally with code zero and all restoration checks passed.

The first corresponding Ubisoft History run exposed an ordering race: an
authenticated native EOT reached Gold before History's original local-start
queue receipt. Gold rejected it before calling its own local start. The public
fresh-start barrier now exposes `CanBufferWorldEvents` for that original,
bounded start interval. It only authorizes buffering: it neither advances the
barrier nor manufactures the queue receipt. Unbound, pre-start, expired and
backward-time calls are rejected. Native tests cover those conditions and both
original queue phases. The private receiver retains its payload limits,
authentication, sequence checks and original native dispatch predicates.

With that candidate, the next Ubisoft History run returned both original local
world-start calls. Both directions delivered native load progress ending at 1.0
and the same initial EOT scalar fingerprints seen with Steam History. The race
did not recur in this ordinary run, so it alone does not validate early buffering.
Gold remained at turn 0 and History at turn 1 / 200 ms. Gold's sampled original
graphics/logic state had its explicit desync flag clear; this is a single state
observation, not checksum compatibility evidence. The broker finished with 85
relays and one rejection whose cause remains unisolated. Both clients exited
normally with code zero and all restoration checks passed.

A controlled Steam History run then held one real queue receipt for 307 ms and
forwarded the real EOT first. The broker preserved both original bodies and
released the receipt once; it generated no game event or acknowledgement.
Gold reported that the early EOT had been buffered, then returned from its
original local-start call. Subsequent native delivery failed the combined
owner/receiver/enqueue guard. This verifies the buffering path under actual
reordered delivery, but not successful delayed dispatch. Both clients again
exited normally with code zero, all restoration checks passed and the privileged
process check found no remaining game. The scheduling harness also passes
separate headless body-preservation, once-only and bounded-release checks.

The next private adapter waits up to ten seconds for the unchanged original
started-session predicate before removing buffered progress or EOT frames.
Owner mismatches, lost contexts and timeouts remain fatal. All three adapter
builds and their native observer ABI checks pass; fifteen extracted-method
checks and six queue-before-pop assertions pass with modeled session predicates.
After sufficient RAM became available, the controlled Steam pair repeated the
early EOT ordering with a 308 ms queue-receipt delay. Gold initially reported
that its started-session capture was unavailable, then observed the unchanged
valid native context. Both directions subsequently enqueued the original EOT
batches, preserving their sequences and scalar fields. Gold's 15 progress events
and History's 11 matched their received counterparts and reached native 1.0.
This verifies deferred dispatch in that actual ordering case. Gold still stayed
at turn 0 and History at turn 1 / 200 ms. Both exited normally with code zero,
all restoration checks passed and no game process remained. The broker finished
with 65 relays and one rejection whose cause remains unisolated.

A guarded read of Gold's original logic facade found its original launcher
Ready method and a null launcher queue. The crossplay laboratory build excludes
the separate local-simulation bridge used by the standalone Gold path. This is
the next investigated dependency, rather than evidence of a missing EOT scalar
translation. A new private candidate connects the existing native local-core,
advance and transport-search wrappers only after the exact authenticated
two-player started-session predicate succeeds. Outside that context it forwards
the original launcher Ready method. Native readiness results, turns and checksum
computations remain original. Its build and observer ABI checks pass, as do
fourteen extracted context-gate checks. A test of the actual compiled wrapper
also verifies original forwarding of arguments, output, return, stack, preserved
registers and LastError when no world context exists.

The candidate subsequently entered the guarded local core in an actual Steam
History pair. Gold advanced from turn 0 to turn 1 and emitted a second original
EOT batch, which History received. Both clients then stayed at turn 1. A further
Ubisoft History pair reproduced that result. Each pair exited normally on both
clients with code zero, complete restoration and no remaining game process.
The broker totals were respectively 72 and 67 relays, each with one rejection
whose cause remains unisolated.

Direct field-tag inspection of all three original EOT serializers corrected an
earlier diagnostic labeling error: native object offsets 0x10, 0x14 and 0x18
are `CRCTypes`, `CRCValues` and `TurnFinished`, respectively. Wire bytes and their
fingerprints were always preserved; only the new diagnostic names had been
assigned in the wrong order. Schema 2 fixes those labels. It reveals different
native type-1 checksums for finished turn 0: Gold produced 3041482119 and each
History source produced 2998908545. A clear sampled desync flag therefore must
not be read as checksum equality. Both clients also emitted the original
`CDesyncDetectionTurnEvent`, which the earlier bridge did not relay.

`native/crossplay_desync_turn.h` and its native codec add that specific unsigned
detection-turn event. They preserve its native sentinel and asynchronous flag,
project only the admitted header connection, and retain original factory,
reader/writer, object, owner, started-session and local-clone checks. They neither
generate checksum acknowledgements nor replace a computed checksum. The private
receiver uses the same bounded early buffer and original World dispatch gate.
Ordinary launchers do not enable this laboratory runtime.

The new protocol tests cover identity/header mutations, replay, sequence limits,
source/type guards, size bounds and scalar/sentinel preservation. Unsupported
and unowned native calls are rejected. Offline tests execute 18 original writer
cases and 54 cross-profile reader cases, reject 108 malformed streams, and pass
29 compiled method-guard checks per edition, including the complete serializer
return/stack-cleanup bytes. They execute no native heap, factory, sender or game.
Both original DXVK pairs subsequently delivered the original detection-turn
event in both directions with sequence 1, native turn 1 and asynchronous flag 1.
Gold reached turn 2 and its original graphics/logic state's desync flag became
set; each History client left world mode. The channel therefore exposes the
native mismatch instead of leaving the check undelivered. It does not correct
the unequal world checksums. The broker totals were 73 relays for Steam and 72
for Ubisoft, each with one rejection whose cause remains unisolated.

Both games exited normally with code zero in each test. The first History
runner's shared-profile check correctly failed because the native Gold desync
handler created a new `DESYNCSAVE.s7s` in the owned diagnostic profile. That
specific new file was preserved with its hash and timestamp under `work/`, then
quarantined from the profile after confirming both games had exited. The complete
shared snapshot subsequently matched its original state. The failed original
receipt remains retained alongside the separate reconciliation proof. A pinned
private cleanup helper now excludes preexisting diagnostic saves before launch,
waits for the verified owned Gold process to exit, retains only that new owned
diagnostic file and leaves the full shared-profile comparison in place. The
Ubisoft repeat passed all restoration checks with this cleanup and no remaining
game process. No preexisting save was replaced.

The paired native desync saves contain equal-length world payloads of 23,964,766
bytes, differing at 1,518 byte positions. Their saved cache and info members are
byte-identical. Initial differing values include adjacent floating-point
representations. The building-point correction below resolves one group of
these differences. This comparison does not establish compatible save/reload semantics. Advancing
common turns and equal world checksums were still unqualified at that stage;
the later mode correction below establishes bounded advancing runs. NEO and
mixed save/reload remain unqualified.

Passive observations subsequently captured the same sampled owner-thread x87
53-bit and SSE control modes on both editions. Original archive-filter method
inspection and a guarded, read-only scalar-writer probe then mapped eleven
selected differences to building approach/waiting points and activity entry or
constructor points. Gold computes their intermediate products with x87; History
uses separately rounded SSE multiplication and addition, including its degree
conversion. Gold also uses CRT trigonometric helpers where History uses hardware
`fcos`/`fsin`. Changing the process-wide FPU precision would affect unrelated code.

`native/crossplay_building_point_math.h` reproduces that History arithmetic with
explicit SSE operations and hardware trigonometry. Its compiled x86 oracle is
bit-identical to 36,864 outputs from 2,048 bounded original Steam History
initializer cases, including cardinal angles, signed zero and point fallbacks.
`tools/gold/verify_building_point_math.py` fingerprints the private analysis image,
allows only the original initializer and four geometry helpers in emulation,
and compares against `native/crossplay_building_point_math_oracle.cpp` compiled
with MSVC x86. All generated inputs and receipts remain under `work/`; no game,
import, allocation, account or network is executed by this oracle.

The opt-in `native/crossplay_gold_building_math.h` candidate retains Gold's
original initializer and container allocation, then recomputes bounded point
fields in the original objects. Seven original class-table slots, full initializer
and helper bodies, method prefixes and RTTI are guarded. The caller must retain
the executable and authenticated fresh-world binding. Owner-thread, object,
property-chain, finite-input, matching vector-length and writable-memory checks
run before any point writes. Invalid active inputs are exposed to the laboratory
runtime. Neither ordinary adapters nor release packages enable this header.
The compiled bridge tests preserve original arguments, return registers, flags,
stack, x87/XMM results, FPU controls and LastError, and qualify disabled,
foreign-owner and malformed-input paths. The original-image guard fixture passes
464 positive, mutation and unreadable-region checks.

The resulting Gold/Steam History DXVK pair recomputed 52 native building objects.
All eleven selected values became bit-identical to History in the actual native
save. Full saved-state differences decreased from 1,518 to 1,442 bytes; the first
remaining difference moved to offset 400,286. Later differing records include
fishing grounds and trees. The finished-turn-0 checksums remained 3041482119 for
Gold and 2998908545 for History, so the original desync still occurred. Both games
and runners exited normally with code zero, 72 broker relays and no rejection.
Complete profile/archive restoration and an empty host game-process list were
verified. This is a measured geometry correction, not synchronized crossplay;
the remaining checksum cause, Ubisoft repeat, NEO and mixed reload are outstanding.

A second passive probe mapped 32 later differences to resource approach points.
Gold's resource initializer uses a separate object/property layout and six class
tables, covering trees, piles, mines, fields and spawns. The opt-in
`native/crossplay_gold_resource_math.h` retains that original initialization and
allocation, then applies the same History arithmetic to bounded native points.
The compiled kernel matches another 10,240 original History resource outputs
across 2,048 cases (`verify_building_point_math.py --kind resource`). Its actual
compiled bridge ABI tests pass, alongside 299 original method/table/RTTI guard
checks. Ordinary adapters and release packages still exclude the candidate.

The paired DXVK run recomputed 35,195 resource objects and 52 buildings. All 32
selected resource values matched History. The full equal-length saved world
then differed at only 65 bytes, with the first remaining differences near animal
records; this is a snapshot comparison, not a determinism guarantee. The original
finished-turn-0 checksums remained unequal. Both games/runners exited normally
with code zero, full restoration and no remaining host game process; the broker
recorded 72 relays and one rejection.

A subsequent read-only probe captured all 42 direct inputs and incoming
accumulator states from each edition's original type-1 CRC calculation. In both
first passes, the inputs differ only at index 39: Gold hashes a native boolean
as 1 and History as 0. The accumulator states match through the input preceding
that difference. The original diagnostic names this state `evm`; it is distinct
from the map setup's cooperative-mode enum, which already matches at zero.
The probe leaves the original calculation, input values and EOT bytes unchanged.
The opt-in `native/crossplay_shared_engine_mode.h` corrects this initialization
difference before the original fresh-world start. The caller must first validate
the executable, owner thread and authenticated, ready two-human session. Full
original getter/setter bodies and the writable flag are guarded. Gold invokes
its original zero setter once when needed; History must already report zero and
is never modified. The fresh session is revalidated afterward, and later unexpected
mode changes are rejected. Original source validation is retained, as are the
original CRC calculation and transmitted values. Ordinary adapters do not enable
this helper.

Compiled x86 helper tests reject wrong owners, unknown flag values and mutated
methods, and preserve LastError and floating-point controls. The offline
`tools/gold/verify_shared_engine_mode.py` verifier passes 24 original accessor
guard checks and six bounded original getter/setter cases across all three
fingerprinted images. It starts no game and executes original code only in
emulation.

With that correction, the actual Gold/Steam History DXVK pair matched original
finished-turn CRCs for turns 0–1020; Gold/Ubisoft History matched turns 0–1022.
All 168 captured original CRC input/accumulator records matched in each pair.
The sampled native Gold desync flag remained clear, and each History client
remained in world mode through its final sample. The separate private laboratory
limit of 1,024 sent batches then stopped the bridge. Both games and controllers
exited normally with code zero in each run. Complete profile, archive and adapter
restoration and an empty host game-process list were verified.

These are actual advancing simulation runs with equal original type-1 checksums;
they do not yet qualify player gameplay commands, full-state determinism, longer
stability, NEO or mixed save/reload. The earlier remaining 65 saved-state bytes
near animal records have not been fully mapped. Further laboratory runs must
increase the private cumulative test budget while retaining frame-size, queue,
authentication, sequence and memory limits. The subsequent bounded 65,536-batch
candidate passed its complete synthetic sequence/replay/overflow stream test.
Its actual Gold/Steam run matched 1,184 consecutive finished turns and the
Gold/Ubisoft run matched 1,210 before their normal timed exits. Neither reached
the former cumulative limit or emitted a native desync event.

A one-shot laboratory action now uses the original local building placement
helper in normal, payable build mode. It searches at most 128 candidate positions,
submits at most one host storehouse placement, and clears its helper afterward.
No stock, completion or immediate-build result is injected. Before command
transport was implemented, this produced an original `CLogicStateEvent` and the
first native CRC mismatch at finished turn 39. A guarded metadata observation
then identified its nested position/orientation/type/category event, sending
player, original SourceSlot and IsSync fields. These are separate from EOT turns.

`native/crossplay_build_command.h` and `crossplay_build_command_codec.h` implement
only that normal storehouse-placement schema. The 47-byte original command keeps
its sending player, source slot, position/orientation bits, entity type/category
and flags. Only the outer connection identifier changes between native namespaces.
The codec checks full original table/accessor/serializer regions, the original
parent serializers, owner/session/source/player binding and bounded geometry.
It uses the original writer, reader/factory, byte-identical re-encoding and local
native enqueue. The laboratory runtime shares one bounded receive FIFO with EOT
batches to retain their original command/turn arrival order. Other gameplay
schemas remain excluded; ordinary launchers do not enable this component.

Compiled framing and modeled object tests reject foreign players, mutated types,
malformed geometry, replay, overflow and every truncation. The actual compiled
guard fixture passes 62 checks per original edition. The offline
`tools/gold/verify_build_command.py` oracle matches all 12 original writer cases
(two players and both async flags in all three editions). Its optional
`--guard-fixture` also checks the compiled guards against those images. The
existing 36 native event-writer fixtures remain unchanged.

In actual DXVK pairs the original Gold host command was sent once and enqueued
once by each History edition with the same normalized command fingerprint.
Gold/Steam then matched 1,184 consecutive finished-turn CRCs; Gold/Ubisoft matched
1,067. Neither emitted a native desync event. The sampled Gold counter in the
Steam run was 947 with its desync flag clear. Both pairs exited normally with
code zero and passed complete restoration with no game process remaining.
Their broker each recorded one connection reset during shutdown, with empty
input/output queues; that is distinct from a native desync during simulation.

The Ubisoft native statistics samples still reported one storehouse per player.
The new site's completion and material delivery are therefore unqualified, as
were the connecting road command and History-originated gameplay commands at that
stage. The correction below covers those specific commands; other schemas, NEO,
mixed save/reload and longer stability remain unqualified. Equal type-1 CRCs
and a delivered command do not establish those remaining acceptance criteria.

A subsequent automatic action placed a normal storehouse for the History player
as well. Its original command was delivered to Gold with the same normalized
fingerprint. A single original History street-helper placement then returned 2
and emitted `CPlaceStreetEvent`; the previous storehouse-only codec rejected that
unknown schema explicitly. Both games exited normally and restored all state.
This separated successful reverse-direction construction delivery from the
missing road transport without supplying a native acknowledgement or completion.

`native/crossplay_road_command.h` and `crossplay_road_command_codec.h` now accept
only the observed normal-street schema. Its original 47-byte event preserves
street type, four endpoint coordinates, player, SourceSlot and IsSync; only the
outer connection namespace changes. The native codec retains the original
serializer, factory/reader, re-encoding comparison, destructor and local enqueue.
Roads share the bounded FIFO with storehouse and EOT events. Unknown commands
remain excluded. The compiled original-image guards pass 89 checks per edition;
`verify_build_command.py --kind road` matches 12 original writer cases, while
the existing storehouse and 36 baseline event cases still match.

The resulting actual Gold/Steam pair delivered one storehouse command in each
direction and the History road to Gold, then matched 496 consecutive finished
turns. Gold/Ubisoft delivered the same three original commands and matched 1,129
consecutive turns. Neither emitted a native desync event. Both pairs exited
normally with complete restoration and an empty host game-process list. These
laboratory adapters use the curated public engine-mode and command codec modules.

Native Gold initialization counts also increased from 52 to 54 buildings after
the two storehouse commands. Later corrected History observations find two
storehouses per player, one unfinished and marked in progress, but still report
zero construction progress and an aggregate native count of one per player.
Material arrival and completed construction are therefore not established. The
Gold road-helper attempt had not yet produced an outgoing road event at that
stage. The camera and coordinate correction below resolves that missing command.
No NEO or mixed reload claim follows from this command milestone.

The subsequent Gold console observation identified an offscreen road target.
The original camera's `setLookAt` followed by `immediateUpdate`, without a manual
zoom-level override, allowed Gold's original street helper to submit its command.
An actual Gold/Ubisoft History pair delivered both normal storehouse and both
normal street commands, matched 1,222 consecutive finished-turn CRCs and completed
Gold's new storehouse. History's new site still waited for construction.

The remaining test-action error was using the building placement's old screen
coordinates as the road's start after the camera could move. The corrected action
frames both endpoints on both players, waits 250 ms and reprojects both original
world positions immediately before the normal street preview. In the following
Gold/Steam History pair, both new storehouses completed and both clients reported
two finished storehouses per player. Native original CRCs matched turns 0–509,
then first differed at turn 510; both engines sent original desync events. Both
clients exited normally with code zero, all profiles, archives and adapters were
restored, and no game process remained. This proves normal construction can run
through the bridge, while exposing a later synchronization defect. It does not
qualify stable logistics or separately measure each material delivery.

`tools/gold/pair_construction_probe.lua` preserves that normal action as an
explicit laboratory option in `pair_interface.py --construction-probe`. It sets
normal payable build mode, searches at most 128 positions with four candidates
per frame, submits one storehouse per player and attempts one connecting road.
Both original preview validity checks remain authoritative. No stock, worker,
building progress or completion result is written. Missing preview coordinates,
wrong local players and failed endpoint projections prevent submission. The
normal watchdog stops further construction callbacks after requesting exit.
Ordinary staging and launchers leave this action disabled.

Seven Lua 5.1 modeled tests cover both players, camera movement with changed
screen coordinates, the 250 ms wait, native reentry, search limits, missing world
positions and invalid road previews. Six private archive stages cover the
default and construction-enabled variants of all three editions; every changed
script parses and all other payload checks pass. The exact staged wrappers also
stop new construction after normal exit. These source/staging checks are distinct
from the native runs above. The later movement-time runs below qualify this
curated action in new frozen adapter/archive pairs.

## Animal movement-time correction, 2026-11-14

The finished-turn-510 mismatch was reproduced with both History editions after
both normal constructions completed. A read-only original-CRC input ring found
the first difference in the simulation RNG seed and count. An instruction-site
observer then identified a Gold-only animation selection for a particular animal.
Typed native registry snapshots showed that Gold finished its movement one turn
before History; changing or advancing the RNG would conceal that earlier cause.

The earlier divergence occurs while crossing a waypoint. For the same distance
of 9 and effective speed stored as float `0.2`, Gold divides in x87 precision and
truncates the result to 44 milliseconds. History rounds the division to a float
before truncating, giving 45 milliseconds. Gold therefore carries one additional
millisecond into the next segment, eventually triggering its next animation
selection early. Both clients have identical positions immediately before this
step; the differing time conversion is independently reproduced from their
original instructions.

`native/crossplay_movement_time_math.h` implements History's single-precision
division. The opt-in `native/crossplay_gold_animal_movement_time.h` replaces only
the quotient passed to Gold's original integer-conversion helper. It retains the
original movement control flow, collision checks, waypoint updates, RNG and CRC.
It requires the enclosing executable identity and authenticated fresh-world
predicate, the owner thread, exact original return/stack layout, typed movement
and animal objects, and the matching generational entry in the native animal
manager. Other movement objects and rejected contexts forward unchanged. Its
17 immutable regions cover original movement/conversion methods, class tables,
getters and the constructors that establish the native manager path. Optional
caller-owned diagnostics are limited to 32 differing-cost scalar records.
Ordinary adapters do not include this module.

The compiled bridge tests preserve original forwarding, register and stack
contracts, the remaining x87 stack, XMM result, floating-point controls and
LastError, including original conversion status effects. They cover disabled and
foreign-owner calls, wrong classes and registry identities, invalid arithmetic
inputs and bounded diagnostics. The compiled guard fixture rejects mutations
and unreadable regions in 53 checks. `tools/gold/verify_movement_time.py` executes
8,197 bounded original-instruction cases per Gold, Ubisoft History and Steam
History image. The compiled kernel matches both History editions in every case;
the original Gold result differs in 1,810. Each History arithmetic block is
resolved from its own typed native movement table. These checks start no game.

The first corrected native pairs passed the former failure point and matched
1,566 original CRCs with Ubisoft History and 1,565 with Steam History. The
subsequent frozen pairs using the curated module and public construction probe
matched turns 0–1560 with Ubisoft and 0–1549 with Steam. Each delivered exactly one
original storehouse and one original street command in each direction, with
matching sent/enqueued fingerprints. Both clients observed two finished
storehouses per player through their final samples. No stock, building progress
or completion was supplied by the adapter, and neither pair sent a native desync
event. Both used DXVK at High quality, 1920×1080, standalone without a Steam
client/SDK dependency. Both games exited normally with code zero in each pair;
profiles, shared saves, archives and adapters were restored, and no host game
process remained.

This establishes bounded shared gameplay and completed construction for these
two loopback fresh-world pairs. Original type-1 CRC agreement is not full-state
determinism, and individual material deliveries were not separately measured.
Longer runs, other gameplay commands, the Gold NEO port, adapted multiplayer
maps, mixed save/reload and separate-PC transport still require implementation
or qualification. The existing 65-byte saved-state differences have not been
fully mapped. The [verification receipt](verification/gold-history-movement-time-20261114.json)
binds the source and laboratory adapters without publishing game data or local
identities.

Real acceptance requires both DXVK pairs to create their own native lobbies,
perform original Ready/Start without human input, leave the shared loading
barrier and execute actual advancing simulation turns. Matching delivered EOT
batch fingerprints alone do not establish gameplay synchronization. Existing
executable, adapter, native ABI, loopback, profile/save backup and byte-exact
restoration requirements continue to apply.
