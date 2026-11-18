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
| Extreme AI footprint angles stalled native terrain traversal (`f259052`) | The constructor parity correction addresses one source of invalid angles. The separate Gold terrain-call defense still needs an independently verified original site. | The guarded footprint call reduces the demonstrated extreme finite angle. | An independently mapped call uses the same bounded reduction. | The Ubisoft guard fired during an actual resumed match with 3,924 matching CRCs. Steam has arithmetic, original-layout and x86 ABI checks. |
| Restored team relationships and map/priority state (`d4c687e`, `babe708`, `6ae6e58`) | The mixed configuration and logistics use separate native paths. The three-edition saved lobby and world tests must confirm the corresponding invariants. | Native team setter, map-template identity and missing-priority guard are installed in the guarded History path. | Corresponding callsites are mapped separately. | History pair runs cover team/AI agreement and the priority crash boundary. Ubisoft exercised an actual variant reload. Steam's variant path has synthetic coverage. |

The previous animal and unit regressions show why an animation or random-stream
difference needs a trace back to its first differing input. The current
three-Ubisoft report is a separate reproduction. At turn 35,607 the watched
boar enters the same spawn call on all three clients after the first guest
skips a weighted animation draw on another controller. The first guest's
different route follows the random-stream split. [Historical movement tests](GOLD-HISTORY-AUTOMATED-START.md#animal-movement-time-correction-2026-11-14) · [Current History report](SUPPORT-HISTORY-CRASH-20261117.md)

Fresh and saved mixed worlds share a local-edition check for the Gold native
bridges, independent of the machine hosting the match. The ordinary adapter
still rejects mixed admission, and the current WPX2 laboratory work reaches
lobby configuration. A successful build, lobby or previous pair cannot
establish that the final three-edition path executes these hooks. The normal
installed builds and both native save directions remain the release boundary.
[World setup](../native/crossplay_production_world.inc) · [Saved setup](../native/crossplay_production_resume.inc) · [Release gate](RC4-RELOAD-RELEASE-GATE.md)
