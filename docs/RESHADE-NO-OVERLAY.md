# ReShade without in-game UI

Gold, Steam History and Ubisoft History share the pinned x86 ReShade 6.8 runtime
compiled without `RESHADE_GUI`. Its menu, startup splash and statistics UI are
excluded. Upstream `open_overlay` refuses requests; four built-in add-on overlay
registration entry points discard callbacks. Effect compilation, rendering,
depth handling and the managed atmosphere bridge remain available. Generated
profiles also disable overlay and statistics shortcuts and messages.

This changes ReShade's UI only. The game's HUD, menus and graphics choices
remain available. Gold's separate restriction on the defective Maximum option
remains in place.

## Build and packaging

`tools/gold/build_reshade_hooks.ps1 -BuildRuntime -DisableOverlay` applies the
existing guarded Vulkan hook fixes to the supported upstream source. It uses
fresh private per-project intermediates after upstream property imports, and
explicit compiler `/U` metadata. GUI and no-GUI objects have different runtime
layouts and must never share a cache. An earlier mixed-cache candidate crashed
before world start and was excluded; the pinned replacement was rebuilt from
fresh objects and tested in running worlds.

`tools/gold/bundle_quiet_reshade.py` packages the qualified DLL and Vulkan
manifest into a deterministic, hash-checked ZIP. The main package builder takes
it through `--quiet-reshade` and requires it for the effects package. The same
bundle is included in the nested Gold payload and copied through Gold's
separate game-preparation path. Each new generated profile uses the new DLL;
older profiles and their recipes are retained.

Runtime preparation accepts the exact bundled or already cached no-GUI runtime.
A missing or modified bundle fails visibly. It does not download a replacement
or fall back to the old GUI DLL. A complete private main package and the nested
Gold inventory were built and verified; bundling an already updated Gold
package also passed. An updated public installer has not been published.

## Observed games

Both pairs used original standalone executables, DXVK, High 1920×1080, two human
clients and two native AI players on Monhaim Valley. NEO's experimental FIFO and
individual-priority feature mask was 19. No original game checksum, clock, RNG
or stock value was synthesized.

| Pair | Common original CRCs | Mismatches | Original saves |
| --- | ---: | ---: | --- |
| Gold / Steam History, case 243 | 1,971, turns 0–1970 | 0 | Both at turn 300; world continued |
| Gold / Ubisoft History, case 244 | 1,695, turns 0–1694 | 0 | Both at turn 300; world continued |

The logs confirm successful effect compilation on all three editions. Live
Gold and Ubisoft screenshots showed the running world and ordinary HUD without
ReShade UI. A Home-key check during an original save timed out and is excluded
from the acceptance result. Both pairs exited normally and passed strict
profile, archive, adapter and shared-state restoration checks.

These results do not establish shared reloading, full NEO5, effective x20 speed
or EasyTier play across separate computers. The machine-independent pins and
observations are recorded in
[the graphics receipt](receipts/reshade-no-overlay-20261115.json).
