# Supersampling without ReShade

Development experiment, 2026-11-24. This work is separate from the released
launcher and its graphics profiles. No new game supersampling setting is
qualified or enabled by these tools.

## Ordered work

1. Establish higher-resolution rendering and downsampling with the pinned
   DXVK 3.1.1 runtime, then qualify the original game's window, input and UI.
2. Inventory original material/shader resources and establish one real
   material-to-shader binding before replacing a material shader. See
   [the material shader lab](MATERIAL-SHADER-LAB.md).
3. Compare the remaining shadow/detail instability using matched captures.
   See [the shadow/detail lab](SHADOW-DETAIL-LAB.md).

The three investigations can proceed independently. Native renderer changes
depend on their actual observations. The existing
[shadow/detail controls](GRAPHICS-LOD-SHADOWS.md) and
[distant filter correction](SHADOW-DOF-REVIEW.md) are the starting point.

## Resolution and cost

The scale multiplies each image dimension. At 1.5x there are 2.25 times as
many pixels; at 2x there are four times as many pixels. These ratios describe
pixel counts, not a predicted GPU time or memory allocation.

| Output | Scale per dimension | Render image | Pixel ratio |
| --- | --- | --- | --- |
| 1920 x 1080 | 1.5x | 2880 x 1620 | 2.25x |
| 1920 x 1080 | 2x | 3840 x 2160 | 4x |
| 2560 x 1440 | 1.5x | 3840 x 2160 | 2.25x |
| 2560 x 1440 | 2x | 5120 x 2880 | 4x; outside the initial limit |

Keep the existing 3840 x 2160 pixel budget for the 32-bit game. It is a
conservative limit, not proof that every map fits. Render targets, depth,
postprocessing surfaces and any multisampling need additional allocations.
Neither a simple bytes-per-pixel estimate nor a successful small GPU fixture
qualifies a large saved settlement.

## Output-cache lifetime candidate

### Actual three-edition checkpoint, 2026-11-24

The combined observer/capture build was run sequentially with the original
Ubisoft History, Steam History and Gold executables in isolated portable copies.
All three produced real world geometry with a 1920 x 1080 scene target,
viewport and source, a 1280 x 720 physical client/output, and a successful
same-frame GPU source/output capture. No ReShade was prepared.

| Edition | Filtered / total presentations | Output chain creations | Peak private memory | Exit |
| --- | ---: | ---: | ---: | --- |
| Ubisoft History | 68,939 / 68,940 | 1 | 1,948 MiB | Old measurement watchdog stopped the client; not a regular-exit pass |
| Gold | 42,918 / 42,919 | 1 | 2,032 MiB | 0, original UI after a new free-slot save |
| Steam History | 23,646 / 23,647 | 1 | 2,025 MiB | 0, original UI after a new free-slot save |

Each initial transition used the original path once. Gold and Steam terminal
cleanup restored the public slots and released the cached chain once. Shared
profiles and the five guarded game binaries were unchanged; private test
configuration was restored without cleanup errors. No memory pressure or
monitor failure was recorded. Counters are successful nonzero draw/presentation
counts, not FPS or GPU timing.

The healthy-client watchdog has since been corrected: the measurement deadline
only requests a regular close and keeps memory monitoring and local services
alive. Gold exercised this soft deadline and was subsequently saved and closed
normally. A time budget does not establish a hung process.

These cases establish the three-edition 1.5x experimental path and actual cache
reuse. They do not qualify 2x/4K, every HUD/picking/DPI/reset scenario, demanding
settlement performance or the final installed package. Materials were observed
read-only; no material or shadow appearance improvement is claimed here.

The Vision main render-context camera stayed fixed even while the visible Gold
view moved. It is retained only as getter provenance. The bound RiverColor
vertex shader's eye and view-projection constants did change coherently with
the castle-to-village images. Their sample frames differ from the GPU capture
frames, so this is tracking evidence, not same-frame camera matching.

The first working game run created the output swapchain 70 times. The original
bridge deliberately dropped it before every external device `Release`, including
temporary references. The next candidate keeps it across those ordinary releases.
It drops the output before Reset, on a rejected/failing presentation, and during
explicit cleanup or the owned window's `WM_DESTROY`/`WM_NCDESTROY` lifecycle.
It never treats the numeric return from COM `Release` as proof of device death.
Terminal cleanup restores the documented public method slots before releasing
the cached child. The method table itself must belong to the pinned renderer or
the separately pinned Windows compatibility image; no private object fields or
compiler-private slots are accessed.

The standalone GPU fixture observed 1,002 ordinary device releases with cache
retention. A deliberate sequence of 1,000 `AddRef`/`Release` pairs and its following
filtered presentation reused the same output swapchain. Only three swapchains
were created across three actual allocation epochs; Reset, deliberate fallback,
recovery, owned-window destruction and table restoration passed. Four captured
outputs matched the complete reference with maximum channel error 0.5. This is
resource-lifetime evidence from the synthetic host, not a measured S7 pacing gain.
The corresponding S7 performance and lifecycle qualification remains separate.

Exact original executable profiles are now identified for Ubisoft History,
Steam History and Gold. Read-only checks found their vBase90 and Vision90 DLLs
byte-identical, including the named device getter and its documented C++ signature.
Their original executable fingerprints remain distinct. Adding those identities
to the guard table does not qualify supersampling, input or saved-game rendering
on the two additional editions.

## Combined native diagnostic interface

### Actual 4K and lifecycle checkpoint

Original Ubisoft History loaded its owned Waldreich save at 3840 x 2160 with a
1920 x 1080 physical output. Same-frame source/output captures confirm full
scene target, viewport and filtered presentation. Original material bindings
were changed and restored on real draws, with no shader/set/restore failure.
This establishes execution, not the desired visible material improvement.

The first combined material run exited with access violation 0xc0000005 after
the original desktop confirmation. Its fault module/stack is unavailable; it
must not count as a clean-exit pass. A subsequent candidate cleans up before
the guarded original engine D3D deinitializers and retains the device through
the original window callback in the fallback path. The first actual retest was
interrupted before Exit by two bounded address-scan timeouts; the existing
memory guard stopped that test, and its limits/retry policy remain unchanged.

The next actual retest loaded the previously created private save, rendered the
world with materials enabled, disabled the effects, saved to another free slot
and exited through the original UI with code zero. Both guarded original engine
deinitialization calls returned; one early cleanup completed with no rejected
call or window-destroy fallback. The monitor stayed functional, recorded no
pressure and peaked at 1,910 MiB private memory. Shared profiles, binaries and
private-configuration restoration checks passed. A rotating owned private
autosave advanced and its before-image was retained; the strict whole-case
qualifier continues to expose this rather than silently declaring preservation.
This is one Ubisoft lifecycle retest, not an AV root-cause proof or Gold/Steam
qualification of this new candidate.

The cache correction also has real draw evidence: 89 further active frames
restored 13,973 material draws without another shader-object creation. Three
generated programs remained retained and zero remained after disable/cleanup.
These are D3D9 object counters, not driver compilation events or a pacing gain.

Separate 30-second PresentMon measurements of the earlier material candidate
recorded CPU frame intervals with no collector loss or capture/epoch changes:
effects off mean 16.654 ms / p95 18.941 ms / maximum 25.605 ms; effects on mean
16.674 ms / p95 19.252 ms / maximum 69.706 ms (one interval above 50 ms). Both
were capped at 60 Hz on this machine. They are not displayed-FPS measurements,
matched-demanding-settlement benchmarks or evidence for the later cache change.

Original P pause can reuse a cached world backbuffer. A diagnostic capture after
an effect toggle therefore waits for fresh successful geometry in that epoch;
it must not label the old displayed image with the new setting. In one observed
case the capture completed only on the first resumed world frame. Original
water render-time inputs can change independently, so unchanged camera/light
uniforms alone do not isolate water appearance. Further controlled comparisons
and visible improvement remain required.

The initializer retains its 1,048-byte version-1 ABI. Version 2 has a 3,132-byte
configuration: the original prefix, flags, a scalar shader-catalog path and
SHA256, and a separate private observer-report path. Native20 accepts read-only
observation (`1`) only. The later `effects-v1` candidate implements Water (`2`),
Terrain (`4`) and Vegetation (`8`) behind an explicit configuration handshake;
Stable shadows (`16`) remains rejected. Compiled implementation is separate from
per-edition runtime qualification. At render scale 100%, the
observer uses the original Present without an extra swapchain or logical resize.

`WePlayNativeGraphicsConfigureObservation` accepts the bounded 952-byte exact-
pair capture configuration after initialization. `WePlayNativeGraphicsRequestCapture`
accepts a 12-byte `(size, version, sequence)` request and queues one capture on
the owning render thread. Both remain explicit diagnostic interfaces; they do
not enable an unqualified launcher option.

Surface capture reads the actual high backbuffer and filtered output from the
same frame, after the GPU blit and before presentation. It writes new private
BMPs and a scalar receipt beside the configured report, with bitmap/state hashes,
actual dimensions, camera/LOD/sun availability, frame counters and capture stall
duration. Unavailable camera or environment fields remain `null`; no elevation,
exposure or simulation turn is inferred. It never samples the desktop. The
readback/write stall is explicitly excluded from pacing measurements.

The combined x86 GPU fixture passed with both version 1 and version-2 observation
enabled. It also injected a failed filtered Present followed by a successful
original fallback. That frame's buffer files were complete, but its receipt
retained the failed `filtered_present_hresult`, `fallback_used: true` and
`presentation_path: fallback_original`; the final caller's `S_OK` did not become
a filtered-presentation success. A qualifier must check those per-frame fields,
not merely `complete` or later global counters. The native-only path is labeled
`original`, not `filtered_output`.

The combined candidate has standalone GPU evidence for cache reuse, logical
resize/input contexts, original Reset/fallback/recovery, owned-window cleanup,
actual shader `GetFunction` observation, same-frame surface hashes and negative
presentation metadata. Those results do not establish current S7 pacing,
cross-edition SSAA, 4K memory safety, or a material appearance improvement.

Current native reports and surface receipts label `draw_counter_semantics` as
`successful_nonzero`: high-resolution and indexed counters advance only after a
successful original draw with nonzero primitive count (and nonzero indexed vertex
count). Earlier receipts counted attempted calls; their historical counters are
not retroactively reinterpreted as successful geometry draws.

## First GPU experiment

`native/supersampling_probe.cpp` is a standalone x86 D3D9 host. It loads only
the exact pinned DXVK binary after validating its SHA256. It does not start
S7, install a hook, change a desktop display mode or deploy an adapter.
Builds and private captures belong in a new directory under `work/`.

The fixture checks real surface dimensions and GPU readback against a generated
pattern with distinct corner markers and fine alternating pixels. GPU
downsampling, successful presentation and the actual visible output are
different observations and must be recorded separately.

Microsoft documents windowed D3D9 presentation stretching between source and
client rectangles. DXVK's implementation also blits a D3D9 backbuffer into its
Vulkan presentation image. This provides a concrete experiment, but does not
establish how S7 reacts to a window smaller than its render resolution.

References: [D3D9 Present](https://learn.microsoft.com/en-us/windows/win32/api/d3d9/nf-d3d9-idirect3ddevice9-present),
[pinned DXVK swapchain implementation](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/d3d9/d3d9_swapchain.cpp).

## Observed standalone GPU results

The initial implementation was independently rebuilt and run on 2026-11-24
with the pinned x86 DXVK 3.1.1 runtime on an RX 9070 XT. Both builds produced
probe SHA256 `5ebd618db067c6661d16615dcf0f3babc6e0cf4eafec28f8bae423aef40746e7`.
The source, build script and executable hashes are bound in a checked build
manifest. The runner loads a separate verified runtime copy in its new work
directory; it does not keep a game test copy's renderer loaded.

| Fixture | Actual source / client | Full-frame maximum channel error | Result |
| --- | --- | ---: | --- |
| Explicit GPU downsample, 1x | 640 x 360 / 640 x 360 | 0 | Pass |
| Explicit GPU downsample, 1.5x | 960 x 540 / 640 x 360 | 0.375 | Pass |
| Explicit GPU downsample, 2x | 1280 x 720 / 640 x 360 | 0.5 | Pass |
| Explicit GPU downsample after resize/Reset, 2x | 1600 x 900 / 800 x 450 | 0.5 | Pass |
| High backbuffer with direct windowed Present, 2x | 1280 x 720 / 640 x 360 | 127.5 | Filtering reference failed |
| High backbuffer with direct windowed Present, 1.5x | 960 x 540 / 640 x 360 | 159.1875 | Filtering reference failed |

All four corner markers survived in every case. The explicit path calls
`StretchRect` with `D3DTEXF_LINEAR` into an output-sized backbuffer before
Present. The direct path copies at equal source/backbuffer dimensions, leaving
only Present to scale into the smaller client. At 2x that direct path resolves
the alternating black/white pattern to black instead of the expected gray
127.5. Thus shrinking the window alone does not provide effective supersampling
on this tested configuration; a deliberate filtering pass is needed.

An independent CPU check of every explicit 2x output pixel against a four-source-
pixel area average also passed, with maximum channel error 0.5. The pattern is
CPU-generated and uploaded: these checks establish GPU minification and actual
presentation, not high-resolution S7 scene rasterization or game image quality.
Surface sizes were read back from the GPU and visible output was captured only
inside the probe's own window. Failed screen crops were not persisted.

Reset after a window resize and rejection of an invalid Reset were observed.
Actual device loss was not induced. The runner's successful exit means the
probe completed and recorded its mixed findings; inspect the per-case flags and
`direct_present_bilinear_passed: false`. It does not mean every path passed.
Private receipts and BMPs remain under `work/`; no generated binaries or game
data are committed. The 39 Python tests cover sizing/provenance, the material
inventory and the shadow comparator independently of the GPU experiment.

## Actual game experiments and remaining qualification

### Logical-size prototype: first working local observation

The next guarded prototype separates logical game dimensions from the physical
client. It replaces documented COM slots in place, preserving the DXVK compiler's
private vtable/RTTI structure. Known game-module import slots for GetClientRect,
ScreenToClient and ClientToScreen provide logical sizes/points only for the owned
window and pinned game callers. DXVK, unrelated callers/windows and unsupported
states retain physical behavior. The owned window procedure translates the
matching size/client-mouse messages; minimize and unrelated messages are retained.
The resize runs outside the presentation/reset lock.

The independent GPU fixture verified actual high-resolution Reset and drawing,
physical low-resolution presentation, coordinate roundtrips, foreign-context
exclusions, fallback/recovery and explicit detach. It also rendered the authored
SM3 diagnostic marker. Device loss and the extended device interface remain
unqualified.

Actual original Ubisoft History gameplay on 2026-11-24 then retained a 1920 x 1080
backbuffer, selected scene target and viewport with a 1280 x 720 client/output.
The private Waldreich save loaded through the original UI. Final counters were
16,980 presentations, 16,979 filtered presentations, one initial transition
fallback, 48,632,359 high-resolution draw calls and 64,432,334 indexed draw calls.
The final failure code was zero. These are call counts, not frame-time or FPS
measurements, and include multiple render passes.

Original menu selection, world selection opening the military panel, text entry
and saving to a new free private slot were observed. The original save remained
present. The game exited zero without a watchdog/forced stop. Shared profiles and
game binaries matched their checkpoints; the durable recovery journal restored
private options/runtime/DXVK configuration with no cleanup errors. Memory
monitoring remained functional; peak private memory was 1,863 MiB and no pressure
was recorded in this run.

This establishes a local experimental supersampling path, not a released option,
pixel-perfect HUD/picking certification, a 4K benchmark or cross-edition/platform
qualification. Additional swapchain recreation occurred 70 times and still needs
pacing review. Existing production adapters and release pins remain unchanged.
The native build and fresh runtime case are retained privately under `work/`.

`tools/graphics_lab/game.py` starts only a fingerprinted portable test copy,
backs up explicit profiles and omits ReShade preparation. Its fixed x86 loader
binds the hook DLL to the current source/build manifest and retains a read-only
file lease through loading. `tools/graphics_lab/recover.py` journals only the
three private configuration files before mutation. Recovery refuses running
games, changed binaries/shared profiles or unsafe paths, preserves diagnostic
settings, and never replaces saves. Journal fsync/atomic replacement does not
promise protection beyond the underlying storage system's guarantees.

### First actual game observation and power-loss recovery

The separate portable Ubisoft History copy was started with its packaged native
adapter and pinned DXVK, requesting 1920 x 1080 with a 1280 x 720 output. A
baseline loaded the private Waldreich save and exited with code zero; shared
profiles and game binaries were unchanged. This is single-client evidence.

The first hook run was interrupted by a user-reported power outage. Its last
statistics file was zero-filled and cannot qualify any rendering result. After
restart, no test game remained. The original shared profile fingerprints and
game binaries matched their checkpoints, and only the private test options were
restored. No save was replaced with a diagnostic backup. Sources passed all 64
then-current Python checks and the hook build fingerprint still matched.

The fresh post-restart run provided the decisive real-game result: resizing the
client from 1920 x 1080 to 1280 x 720 caused S7 to request a successful original
Reset at 1280 x 720. Actual backbuffer, selected target and viewport all became
1280 x 720. The hook recorded 8,066 original presentations and zero filtered
output presentations, correctly rejecting the low-resolution source with reason
7. Real world rendering and indexed draws were observed, but this was **not
supersampling**. The process later exited zero without a watchdog/forced stop;
shared profiles and binaries were unchanged and private options were restored.

The next implementation must keep S7's logical render dimensions separate from
its physical output window. Changing only Reset's backbuffer dimensions would
not establish high-resolution scene rendering. HUD/world picking, original
offscreen targets, viewport dimensions and original resize/reset behavior remain
part of the required observation. No production option or release pin is enabled.

- Verify the original scene's actual color/depth and final backbuffer sizes.
  Raising only a presentation surface can leave the scene at its old resolution.
- Verify the smaller output receives the full image, rather than a crop or an
  upscaled low-resolution frame.
- Check whether `WM_SIZE`, a graphics-menu apply, focus changes or device reset
  recreate render targets at the output size.
- Check HUD size, text legibility, pointer picking, edge scrolling and dialogs
  at every supported scale and Windows DPI setting.
- Compare the same save, camera and sun phase. S7's existing LOD selection
  depends on resolution; record effective detail so a geometry change is not
  mistaken for a downsampling benefit.
- Test memory and pacing in a large settlement, restoration, and a fresh start.
  Keep normal game speed and the pinned renderer.

On 2026-11-24 the user clarified that the Crossplay chat is archived; its older
exclusive reservation is no longer current. Isolated local game experiments
can continue under the existing authorization. Check actual running processes
before each launch and preserve profile/save isolation. Do not replace a loaded
adapter, alter release pins or present synthetic GPU results as observed S7
gameplay. The first standalone fixture milestone did not start S7.

## Reproduce the standalone experiment

Run from the repository root with the x86 Visual Studio C++ toolchain installed.
Use fresh output names; existing diagnostics are never overwritten.

```powershell
rtk proxy powershell -NoProfile -ExecutionPolicy Bypass -File native/supersampling_build.ps1 -OutputDirectory work/supersampling-build-local
rtk proxy python -B -m tools.supersampling.plan --width 2560 --height 1440 --scale 1.5
rtk proxy python -B -m tools.supersampling.probe --probe work/supersampling-build-local/supersampling_probe.exe --dxvk <path-to-verified-DXVK-d3d9.dll> --output work/supersampling-gpu-local
```

The GPU runner briefly displays its own synthetic test window without requesting
focus. A visible-output check requires an unobstructed interactive desktop. A
capture failure is a failed observation, not evidence that downsampling works
or fails in S7. No driver setting, monitor mode or game preference is changed.
