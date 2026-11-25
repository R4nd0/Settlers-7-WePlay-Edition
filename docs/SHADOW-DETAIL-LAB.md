# Native shadow/detail comparison lab

The current source-based streak investigation and normal-camera shadow targets
are in [the original-pass diagnostic plan](BLACK-STREAK-DIAGNOSTIC.md). The new
blob neutral-RGB policy is a disabled pure diagnostic plan; it has no runtime or
visible-improvement proof and does not change the 2048/4096/8192 filter guards.

2026-11-24. The first implementation in this stage is an **offline capture
comparison tool**, not a new renderer effect or a native patch. No game was
launched, no reserved test copy was changed, and no shader or shadow improvement
was runtime-qualified by this work. Supersampling is the preceding stage; the
material-shader investigation remains separate. On 2026-11-24 the user clarified
that the Crossplay chat is archived: its older test-copy reservation no longer
blocks isolated graphics experiments. Release qualification and pin requirements
remain unchanged.

## Existing work to retain

The current source already implements these native controls without ReShade:

- Model/SpeedTree detail and culling distances use `visual_distance::Steps` in
  `native/visual_distance.h`. `native/visual_detail.inc` guards the History
  operands, and `native/gold_visual.inc` independently maps Gold. The original
  resolution-dependent LOD setter still runs. Higher rendering resolution can
  therefore change scene detail as well as antialiasing; record its actual scale.
- Detailed and Ultra shadow allocations have matching resource estimates.
  Ultra uses an 8192 base and a .92 logarithmic/linear near-cascade weight.
  Shadow range is a separate stepped control, optionally linked to model detail.
- `native/shadow_filter.inc` retains the original 1/2048 sampling footprint for
  distant cascades 2 and 3 on owned, validated 4096/8192 maps. Near cascades retain
  their original inputs. Gold's separate bridge preserves its distinct ABI.
- Global negative texture mip bias was removed after the controlled wall-region
  instability investigation. Do not reintroduce it as a detail enhancement.

See [existing controls](VISUAL-QUALITY.md), [native mappings and observations](GRAPHICS-LOD-SHADOWS.md),
[filter evidence and limits](SHADOW-DOF-REVIEW.md), and [Gold mappings](GOLD-GRAPHICS-PORT.md).
The earlier filter captures softened distant transitions, but their reversal
counts did not establish a consistent remaining-flicker improvement. Raising
allocation size or changing the kernel again has no new supporting observation.

## Offline implementation

`tools/shadow_detail/compare.py` reads two private capture manifests and their
SHA256-pinned, uncompressed 24/32-bit Windows BMP images. It measures a selected
stationary region using arithmetic RGB-channel averages on the 0–255 scale;
these are image intensities, not linear light or perceptual luminance.

The comparator requires the same save, executable, DXVK renderer, camera,
environment, effective invariant native-settings fingerprint, output and render
dimensions, measured native LOD scale, original 100-ms simulation pace, and ROI.
ReShade must be off, world hover disabled, and the ROI asserted free of animated
geometry/UI. Capture counts and native turns must pair exactly; elapsed times
must agree within .05 seconds, both sun angles within .1 degrees, and exposure
within .001. No frames are discarded or interpolated. Operator-supplied hashes
and stationary-region assertions are not independently verified runtime evidence.

Only the intended experimental settings belong in `variant`; record all other
effective graphics settings in the separately hashed private settings record.
For example, a shadow-allocation experiment places that allocation in `variant`
and leaves all unrelated settings fixed. A render-resolution or LOD change is
deliberately refused by this shadow comparator; evaluate supersampling first,
then establish a fixed resolution/LOD baseline for subsequent shadow work.

The report provides mean absolute intensity step, change per second, temporal
pixel range, and adjacent direction reversals where both steps exceed five
levels (configurable). Small steps interrupt a reversal sequence. It reports no
automatic improvement percentage, visual pass, game FPS, or shadow-cause result.
Native SSAO, lighting, missed animation, and readback artifacts can still change
the image. A lower aggregate alone cannot establish a correct shadow filter.

## Capture manifest contract

Keep real images, manifests and reports under `work/`; do not commit them. Each
manifest is JSON with the following members (the hashes below are placeholders):

```json
{
  "schema": 1,
  "conditions": {
    "edition": "gold",
    "executable_sha256": "<actual lowercase SHA256>",
    "renderer_sha256": "<actual lowercase SHA256>",
    "save_sha256": "<actual lowercase SHA256>",
    "camera_sha256": "<actual lowercase SHA256>",
    "environment_sha256": "<actual lowercase SHA256>",
    "native_settings_sha256": "<actual lowercase SHA256>",
    "output_size": [1920, 1080],
    "render_size": [1920, 1080],
    "native_lod_scale": 1.5,
    "simulation_pace_ms": 100,
    "reshade_active": false,
    "hover_disabled": true,
    "stationary_roi_asserted": true
  },
  "variant": {"shadow_dimension": 4096, "filter": "existing-distant-footprint"},
  "roi": [930, 450, 145, 140],
  "frames": [
    {
      "image": "frame-000.bmp",
      "sha256": "<actual lowercase SHA256>",
      "elapsed_seconds": 0.0,
      "simulation_turn": 100,
      "sun_azimuth_degrees": 240.0,
      "sun_elevation_degrees": 30.0,
      "exposure": 2.6
    }
  ]
}
```

Supply at least three frames with strictly increasing elapsed times and native
turns, and use `gold`, `ubisoft-history`, or `steam-history` as the edition.
The one-frame illustration is not executable input. Image paths are relative
to each manifest. Bounds: 300 frames, 65,536 ROI pixels, two million total ROI
samples per series, and 256 MiB per BMP. For the illustrated ROI, use at most
98 frames. JSON manifests are bounded to 1 MiB before decoding/parsing. The
measured LOD scale is an example, not a replacement constant.

```powershell
rtk proxy python -m tools.shadow_detail.compare work/shadow-lab/control.json work/shadow-lab/candidate.json --output work/shadow-lab/report.json
rtk proxy python -m unittest tools.shadow_detail.test_compare -v
```

The tests use synthetic BMPs and cover invariant refusals, phase wrapping,
cadence/exposure/native-turn mismatch, bounds, BMP orientation/padding/alpha,
corrupt images and hashes, independent metric behavior, and CLI integration.
They require only the Python standard library. Actual paired native captures,
visual inspection, memory/GPU measurements and lifecycle checks are still open.

## Next native experiment

For the next isolated game experiment, first capture the current renderer with
ReShade off and supersampling fixed. Keep simulation at normal
pace and capture the same native turns/sun interval in both runs. Compare a
stationary wall-shadow ROI separately from vegetation. Keep the current filter
and mip correction as the control. Only after identifying a repeatable shadow
artifact should a targeted native filter/cascade change be implemented under the
existing executable, byte, owner, reciprocal-vector and per-edition ABI guards.

## Profile safety helper

`tools/shadow_detail/profile_guard.py` supplies `snapshot_profile(root)` and
`ProfileGuard(named_roots, backup_dir)` without importing a debugger or optional
packages. Supply the resolved, explicit game-profile roots and a fresh backup
directory under `work/`. `capture()` backs up only `Options.ini`, `Profiles.xml`,
`.s7s` and `.s7local` files, retaining their timestamps and verifying their hashes
and file set before and after copying. Unreadable directories, symbolic links,
junctions and input bounds fail closed. Unrelated account-cache files are not
opened. Reads are limited to 256 MiB per protected file and 2,500 files/directories
per root.

After closing the owned game, `changes()` lists added, removed and changed
protected files. `verify_unchanged()` refuses a changed shared profile. Neither
method restores files: newer saves and profile progress remain available for
assessment rather than being replaced by an older backup. Restoring an owned
diagnostic options file is a separate caller action, followed by verification.
Private profiles expected to advance can be checked with `changes()` separately
from the shared-profile guard.

Seven synthetic filesystem tests cover copy-time mutation, unreadable paths,
timestamp/hash/file-set changes and preservation of a newer save. They do not
qualify actual game-profile isolation.

## Staged guarded filter candidate

`native/shadow_detail_candidate.h` contains a pure, disabled-by-default policy
for a later per-draw experiment. It matches the exact original ATI High
MaskShader programs and a real 4096/8192 square shadow texture on sampler 1.
Only the exact original inverse-size c57.xy input is eligible; the candidate
widens that sampling footprint by 1.25 while preserving c57.zw bit for bit.
The existing distant 1/2048 correction is consequently ineligible and remains
the control. No map allocation, bias, tap count, cascade split, kernel radius or
rotation frequency changes. The original shader also uses c33.y for rotation-map
frequency, so changing that parameter would affect more than edge softness.

This is staged policy, with no device calls or runtime enablement. Its sixteen
CPU checks cover exact program/context/input guards, the two eligible map sizes,
off behavior and unchanged nonselected bits. It has no actual image or cost
result. A future integration must snapshot and restore the original constant
around only the guarded draw, including failed draws, and first establish the
real sampler/register binding. Softer edges may conceal detail or increase
bleeding; a matched original/candidate review must determine whether it helps.

`tools/shadow_detail/catalog_probe.py` constructs an expanded private scalar
catalog from the original archive: the existing nine High water/terrain/tree
libraries plus ATI/ATIx/NV High shadow libraries. It writes metadata and hashes,
never original shader bodies. A mixed 952-byte read-only observation config
captures River camera/raw sunlight and a separate shadow target's PS57.xy,
PS33.xyzw and sampler-1 dimensions. Exact CTAB names/registers and program
identities are checked before generation. The wire catalog has only three
legacy numeric families, so shadow rows use legacy family 2 with explicit
`observation_group=shadow` metadata; this does not activate a terrain or shadow
effect. Consumers must identify original programs by hash/size rather than old
numeric IDs. Three CPU tests validate the ABI and binding refusals.

The subsequent owned Ubisoft History 4K material case recorded the exact
MaskShader pair in three consecutive frames: sampler 1 was a 2048-square D16
texture with one mip, PS57.xy was exactly 1/2048, and PS33 was
`[2.75, 2.75, 0, 0]`. The full 3840×2160 target/viewport and original render-state
guards passed. This establishes that baseline's original shader/register/sampler
binding; it does not qualify the staged high-resolution candidate, which correctly
rejects 2048 maps. No detailed-shadow child flag was requested for that case.

A follow-up baseline can use the existing guarded adapter with only the
child-local `S7_DETAILED_SHADOWS=1` flag, retaining the current detail/range,
material settings and fixed supersampling. The laboratory runner must record
this explicit choice after clearing inherited S7 flags; no adapter replacement
or production-pin change is required. Its actual sampler must then be observed
as 4096-square before testing the candidate. The observer currently samples the
first matching draw per frame; one reciprocal value does not establish every
cascade's input. The existing distant correction remains part of this baseline.
