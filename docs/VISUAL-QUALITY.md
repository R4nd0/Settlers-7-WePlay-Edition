# Optional edge smoothing, Ultra shadows and geometry contacts

Unreleased development, 2026-11-07. These changes extend the existing
[Mittsommer profile](MIDSUMMER-LIGHTING.md) and
[detail and shadow options](GRAPHICS-LOD-SHADOWS.md). Existing published RC3.18
assets and user installations have not been replaced.

## Controls

The launcher's Graphics page exposes three new independent opt-in controls:

- **Smoother edges / Ruhigere Kanten** applies spatial edge-directed ReShade
  filtering. A bounded subpixel footprint smooths high-contrast edges while
  retaining low-contrast texture detail. Fixed top, bottom and right HUD regions
  are excluded. This is not temporal AA, forced MSAA or supersampling.
- **Ultra shadows with more near detail / Ultra-Schatten mit mehr Nahdetails**
  selects 2048/4096/8192 shadow maps for low/medium/high native shadow quality.
  It overrides the older 1024/2048/4096 detail option when both are selected.
  The logarithmic/linear cascade distribution weight changes from 0.87 to 0.92
  to concentrate precision nearer the camera. Range remains independent.
  The native shadow sampling kernel and cascade overlap code are unchanged;
  screen-space edge smoothing also acts on sufficiently contrasted shadow edges.
- **Fine contact shading / Feine Kontaktschatten** adds restrained local
  screen-space contact darkening from the actual scene depth. Sixteen samples
  over a maximum six-pixel radius compare nearby depth against the local plane.
  Distant separation and steep silhouette derivatives are rejected. The maximum
  multiplicative darkening is 22%; most pixels receive much less. It complements
  native SSAO and does not replace the sun's native shadow maps.

Edge smoothing and contact shading require Modern graphics. Ultra shadows
also work with native Direct3D9. All new choices default off, including when
reading old preference files. They take effect on the next launch. Ultra uses
four times the shadow-map texels of the older 4096 high setting and needs more
GPU memory and rendering work. It is not enabled silently by a preset.

Mittsommer keeps its bright twilight/day cycle and 20-minute native period.
The revised light reduces the twilight's orange cast, raises its blue component,
and lowers exposure from 2.6–3.05 to **2.45–2.85**. Sun elevation remains 14–48
degrees, with no dark night. Ambient contribution and shadow strength are
adjusted together. The tone pass gently cools deep shadows, restrains saturation
and rolls off highlights. Choose Mittsommer in the game's Environment menu;
the separate launcher tone option remains independent of that selection.

Higher ordinary output resolutions remain available through the existing
display selector. These changes do not silently change resolution or introduce
a render-scale override. Spatial smoothing cannot guarantee removal of motion
shimmer, especially with the longer-detail option's negative mip bias.

## Runtime integrity and depth behavior

The authored effect contains independent tone, edge and contact strengths.
Selecting any one prepares only that combination. Runtime/config/shader/preset
profiles live under `LocalData/ReShade-Visual-v2`; their names include a content
fingerprint. Upgrades and different combinations therefore do not overwrite
an already loaded shader or the earlier `ReShade-Midsummer` folder. Pinned
standard legacy runtime bytes can be reused without another download.

Tone and smoothing use the pinned official standard ReShade 6.8.0 runtime.
Explicit contact selection uses the official **full add-on** variant, with
separate setup and x86 DLL hashes. The official DLL for that variant is unsigned;
integrity here is established by the exact pinned upstream download hashes.
The setup is only unpacked, never executed; no external add-on ships with the
feature. `AddonPath` is fixed to a checked empty `NoAddons` directory. Vulkan
layer and base-path variables apply only to the launched child, with no registry
or global layer installation.

Dynamic uniforms remain enabled (`PerformanceMode=0`). The depth-ready state
must be updated at runtime, rather than treated as the initial false constant.
The effect checks ReShade's `bufready_depth` signal and ignores missing, cleared
or sky depth. Unavailable depth leaves tone and smoothing usable. The production
preset always sets `DebugView=0`; the magenta/gray/mask views were private test
diagnostics and are never selected by the launcher.

Reference contracts: the official [ReShade API](https://github.com/crosire/reshade/blob/v6.8.0/include/reshade_api.hpp)
documents performance-mode uniform replacement; the
[generic depth implementation](https://github.com/crosire/reshade/blob/v6.8.0/examples/09-depth/generic_depth_addon.cpp)
defines the depth-ready signal, buffer selection and preservation settings.
Downloads remain from the [official ReShade site](https://reshade.me/).

The native changes retain executable identity, exact operand/opcode guards,
ASLR-aware pointers and transactional patch restoration. Ubisoft and Steam
addresses/constant operands were checked independently against the private
analysis images. No native SSAO shader, game EXE or original environment is
overwritten. Simulation commands, saves, NEO priorities and RNG are untouched.
All peers still require matching updated game resources, regardless of which
local graphics options each selects. No content-ID exclusion is introduced.

The installer retains the independently fingerprinted original shared archive
under `RepairSource/Base/BBA/shr.bba`, alongside the existing original executable
and interface repair inputs. Managed updates rebuild lighting from that pristine
copy. Updating an ordinary pre-Mittsommer installation can seed this input from
its verified unmodified shared archive. Earlier unpublished development copies
that already modified the shared archive without retaining it require preparation
from the original source once; unknown archive bytes are never accepted as stock.

## Verification

Private evidence is under `work/visual-quality-20261107`.

- Production native build: 131 visual operand/guard/selection/restoration checks,
  plus bootstrap, Steam profile, directory bridge, NEO map, distance, workshop
  pickup and priority regression suites pass. Windows launcher build and its
  bilingual UI self-test pass; Python preference/package/launcher tests pass.
- The existing late save loads in the isolated client. At turn **67549** and
  the same fixed 1920×1080 close camera, live reads show shadow dimension
  **8192**, matching allocation/estimate operands and model LOD scale 1.4988899.
  With extended range, split points are approximately
  `[100, 732.463, 2046.789, 5899.301, 19200, 76800]`.
- The official full runtime compiles the actual effect. A private diagnostic
  add-on reports a valid **1920×1080** geometry resource and dynamic depth-ready
  state. The gray depth view follows the castle, terrain and background in the
  expected near-to-far direction. The separate contact view is nonempty and
  follows visible geometry. Controlled contact-off and smoothing-off screenshots
  demonstrate that both options affect the actual rendered image.
- A five-second foreground sample of the final white balance with all four
  improvements enabled records 301 consecutive screen-update-loop intervals:
  median 16.667 ms, p95 17.018 ms, maximum 21.025 ms, no interval over 50 ms
  and no missed counter interval.
  This is **not** Vulkan presentation or GPU timing, a long-session benchmark,
  or a claim that the higher shadow allocation is free.
- A separate larger-window test requests 2560×1440. The native window is
  constrained on this desktop; the actual Vulkan swapchain is **2560×1421**,
  so this is not claimed as an exact 1440p output or a complete 1440p screenshot.
  The effect compiles and the 8192 map remains active. Its five-second foreground
  screen-loop sample has 300 intervals, median 16.667 ms, p95 17.096 ms,
  maximum 18.921 ms, none over 50 ms. The same measurement limits apply.
- Two isolated loopback clients load the late NEO save and run turns
  67349–67849. A enables Ultra shadows and all three ReShade components; B
  uses original local detail options and the revised Mittsommer environment.
  Both use the same updated archives. Both exit normally, both replay gzip
  streams complete, and **451/451 structured CRC records match**, with zero
  mismatches (`work/weavery-20261106/pair-10`). This is not separate-PC Internet
  qualification. Both source copies and profiles are restored.

Private diagnostic modules, game assets, raw captures and third-party runtime
binaries are not included in the source repository. Isolated test copies and
profiles are restored after use. Separate-PC Internet, Steam gameplay and
native Linux gameplay have not been requalified for these new effects.
