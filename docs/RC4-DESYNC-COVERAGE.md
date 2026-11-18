# Desync coverage for RC4.00

This audit records the correction or equivalent original behavior for Gold,
Ubisoft History and Steam History. Previous runs qualify only the builds and
scenarios they exercised. The final installed package still needs the
[three-edition reload qualification](RC4-RELOAD-RELEASE-GATE.md) in both host
directions and the [History save regression](RC4-ADAPTER-MIGRATION.md).

| Historical regression | Gold | Ubisoft History | Steam History | Observed evidence |
| --- | --- | --- | --- | --- |
| Animal waypoint timing, mixed turn 510 (`3204b18`, `f56cfe3`) | Guarded bridge uses the History quotient before Gold's original integer conversion. | Original quotient is the reference. | Original quotient matches Ubisoft. | The repeated arithmetic oracle used 8,197 inputs per edition. Gold originally differed on 1,810. Corrected fresh pairs matched 1,566 CRCs with Ubisoft and 1,565 with Steam. |
| Animal path smoothing, mixed turn 2792 (`7304b35`) | Guarded bridge rounds temporary path points at the original append calls. | Original path arithmetic is the reference. | Independently mapped original arithmetic matches Ubisoft. | 18,458 points per History image matched the compiled correction. Corrected fresh pairs matched 4,539 CRCs with Ubisoft and 4,949 with Steam. |
| Unit pathfinding and movement, mixed turn 1278 (`ca422ce`, `69db102`) | Verified Gold movement-precision and pathfinding bridges are part of the shared world initialization. | Original arithmetic is the comparison behavior. | Original Steam is covered by the mixed pair measurements. | The precision component passed 3,334 Gold/Ubisoft CRCs and up to 4,862 Gold/Steam CRCs in fresh pair tests. |
| Saved logistics request reset triggered a different builder animation sequence (`6018bf0`) | Authenticated postload guard preserves the native saved request before scheduling. It selects by local edition at saved start. | Original postload rule excludes `CRequestTask` from the reset. | Original postload rule has the same exclusion. | Active NEO reload with Gold hosting matched 477 Ubisoft and 440 Steam CRCs on the integrated build. Gold receiving a Steam-hosted save still needs an installed-package run. |
| Uninitialized workshop search rotations (`9710c39`) | Gold's original constructor omits the same invariant. The Gold bridge initializes the seven rotations at Gold's own verified offset. | Original constructor hook initializes the same rotations. | Independent addresses and call bytes use the same rotations. | Earlier Ubisoft reloads matched 600, 550 and 600 CRCs. Steam has original-layout and x86 ABI coverage. A private Gold build observed 58 initializations. The current port has a compiled ABI fixture and needs a Gold cold reload. |
| Extreme AI footprint angles stalled native terrain traversal (`f259052`) | The mapped footprint constructor calls `MSVCR90` sine and cosine. The inspected x86 runtime reduces out-of-range x87 arguments with `FPREM1` before retrying. Verify the runtime selected by the final Gold install. | The guarded footprint call reduces the demonstrated extreme finite angle. | An independently mapped call uses the same bounded reduction. | The Ubisoft guard fired during an actual resumed match with 3,924 matching CRCs. Steam has arithmetic, original-layout and x86 ABI checks. Gold's equivalent path was mapped from the original code. |
| Restored team relationships and map/priority state (`d4c687e`, `babe708`, `6ae6e58`) | The mixed configuration and logistics use separate native paths. The three-edition saved lobby and world tests must confirm the corresponding invariants. | Native team setter, map-template identity and missing-priority guard are installed in the guarded History path. | Corresponding callsites are mapped separately. | History pair runs cover team/AI agreement and the priority crash boundary. Ubisoft exercised an actual variant reload. Steam's variant path has synthetic coverage. |
| Warehouse distance handoff and the AI `Storehouse` split (History turn 30,195) | Uses its native handoff. | Dedicated NEO reads usable routes from the native road graph without altering shared search state. | The read-only path uses Steam's original mapped graph and access checks. | The read-only candidate passed the earlier autosave failure in repeated Ubisoft loopback runs. The x86 tests exercise both History layouts, inaccessible sectors and unchanged native state. |
| Equal FIFO keys in the temporary consumer sort (History turn 35,585) | The mixed sort identifies Gold's temporary list from its original merge and selector frames. | Dedicated and mixed NEO identify the corresponding original History frames. | The same policy uses Steam's independently verified frame and callsites. | Three Ubisoft clients resolved different consumers from identical requests before the fix. With the correction the original autosave crossed both failure turns with 10,760 shared matching CRCs. The three-edition x86 fixtures verify the scoped tie rule and the original call argument and register contracts. |

The previous animal and unit regressions show why an animation or random-stream
difference needs a trace back to its first differing input. The current
three-Ubisoft report is a separate reproduction. At turn 35,607 the watched
boar enters the same spawn call on all three clients after the first guest
skips a weighted animation draw on a `CCarrier`. The first guest's different
route follows the random-stream split. The call maps independently to
`0x7eaf33` in Ubisoft, `0x7ead03` in Steam and `0x8f29d6` in Gold. The
History builds use an animation component at object offset `0x198`. Gold uses
`0x1a4` and a different dispatcher ABI.
[Historical movement tests](GOLD-HISTORY-AUTOMATED-START.md#animal-movement-time-correction-2026-11-14) · [Current History report](SUPPORT-HISTORY-CRASH-20261117.md)

Carrier 65,562 first receives a different consumer at turn 35,585, before
any measured change in its route or animation. The independently mapped
destination writes are `0x79198d / 0x791993` in Ubisoft, `0x79175d /
0x791763` in Steam and `0x91caaa / 0x91cab2` in Gold. The History sort
comparison and the comparison for a lower-priority early stop use the same
native function. A change to sort ties must preserve or explicitly verify the
behavior of that second caller, plus Gold's independent selection ABI.
The three-Ubisoft trace finds identical main storehouse queues at the split.
The separate consumer preselection has already picked different requests.
The original sort and first-eligible checks for that list are independently
mapped in Ubisoft (`0x78ff25`, `0x78ff8e`), Steam (`0x78fcf5`, `0x78fd5e`)
and Gold (`0x91b531`, `0x91b564`).
[History trajectory and remaining test](SUPPORT-HISTORY-CRASH-20261117.md)

The History road-distance handoff runs under the dedicated History NEO mode
on Ubisoft and Steam. The negotiated mixed mode enables FIFO, request intent
and priority commands (mask `19`). The History startup uses the verified
standalone launch context to leave the dedicated History NEO hooks inactive
in mixed rooms, even with the local platform active. The mixed History adapter
then checks the original comparator before installing its own hooks. Gold,
Ubisoft and Steam retain their native handoff in the mixed mode. The final mixed world test
still needs a shipment with an eligible intermediate warehouse to exercise
this difference before and after reload. [Mixed feature selection](../native/crossplay_neo_selection.h) · [History startup](../native/offline_bootstrap.cpp) · [Mixed History hook guard](../native/crossplay_logistics_active.h)

Fresh and saved mixed worlds share a local-edition check for the Gold native
bridges, independent of the machine hosting the match. The ordinary adapter
still rejects mixed admission, and the current WPX2 laboratory work reaches
lobby configuration. A successful build, lobby or previous pair cannot
establish that the final three-edition path executes these hooks. The normal
installed builds and both native save directions remain the release boundary.
[World setup](../native/crossplay_production_world.inc) · [Saved setup](../native/crossplay_production_resume.inc) · [Release gate](RC4-RELOAD-RELEASE-GATE.md)

Gold's workshop creates the same type of AI search and uses a different
footprint constructor ABI. Its native terrain-search call at `0x69b7f4`
passes the angle on the stack to `0x9c6aa0`. The corresponding Ubisoft
call passes the angle in XMM2 to a different trigonometric implementation.
The shared workshop invariant and the History footprint defense address
separate stages. [Workshop regression](MULTIPLAYER-RESUME-WORKSHOP.md) · [Terrain regression](MULTIPLAYER-RESUME-TERRAIN.md)
