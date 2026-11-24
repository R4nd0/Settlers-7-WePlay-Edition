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

## Game qualification still required

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

The current handoff reserves game test copies for Crossplay and prohibits
concurrent games. These independent GPU/offline fixtures do not consume those
copies. Do not replace a loaded adapter, alter release pins or present synthetic
GPU results as observed S7 gameplay.

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
