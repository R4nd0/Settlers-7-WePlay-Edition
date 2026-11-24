# Native graphics evidence and qualification

The active graphics work covers native supersampling, original material shaders,
and shadow/detail improvements, followed by launcher integration and testing in
Gold, Ubisoft History and Steam History. Local tests remain sequential and use
owned portable copies, normal game pace and pinned DXVK. Crossplay's older
reservation is archived. Existing executable/ABI/content guards, recoverable
private configuration and shared-save preservation remain required.

`tools/graphics_lab/qualification.py` is the private evidence aggregator. It
checks record consistency and measurement comparability. It does **not** certify
visual quality, correct input, performance or a release. Its output deliberately
keeps `visual_qualified` and `release_qualified` false. Existing actual Ubisoft
supersampling observations are described in [the supersampling lab](SUPERSAMPLING-LAB.md);
they do not establish all-edition coverage or completion of the other phases.

## Evidence gates

| Area | Required evidence |
| --- | --- |
| Native supersampling | Original game identity, high-resolution scene target **and** viewport/backbuffer, smaller physical output/client, real indexed drawing and filtered output presentations; original image plus filtered image from corresponding frames. A high presentation surface alone is insufficient. |
| Material shader change | Guarded original material/shader binding, exact authored replacement identity, actual substitution/draw observations and source/output captures. A compiled shader or diagnostic marker is an intermediate result. |
| Shadow/detail change | Current native parameters and guarded per-edition ABI, matched camera/save/LOD/sun/cadence with supersampling fixed, static-wall and vegetation comparisons, visual review and measured cost. Preserve the existing filter and mip corrections as the control. |
| Input/UI and lifecycle | Original menu/world picking, text entry, edge scrolling, dialogs, scaling/DPI behavior, minimize/focus/reset/resize, fresh start and save/reload; unsupported-state fallback must preserve ordinary rendering and recover. Native call counters alone do not establish these behaviors. |
| Launcher/release | Explicit supported scales and cost information, safe defaults, persistence/restart/off behavior, failure fallback, exact packaged binaries, all three original editions from the final package, and the project's existing release gates. |
| Safety/performance | Functional memory monitoring with no pressure, graceful exit, shared profiles and game binaries unchanged, original private saves preserved, journal recovery complete, actual timing and allocation measurements on demanding scenes. Call counts and readback intervals are not GPU time or FPS. |

The aggregator automatically checks the first gate's available records and
reports the remaining gates separately for review. It does not accept a generic
operator `passed: true` flag as a substitute for measurements.

## Create a private evidence manifest

Keep manifests, captures, source checkpoints and reports under `work/`; never
commit them. Completed cases may be recorded with the existing runner's files:

```python
from pathlib import Path
import json
from tools.graphics_lab import qualification as q

control = q.record_case(
    Path("work/<control-case>"), case_id="ubi-control", edition="ubisoft-history",
    role="control", captures=[("source", Path("work/<control-source>.png")),
                             ("output", Path("work/<control-output>.png"))],
    observation=Path("work/<control-scene-observation>.json"))
candidate = q.record_case(
    Path("work/<candidate-case>"), case_id="ubi-candidate", edition="ubisoft-history",
    captures=[("source", Path("work/<candidate-source>.png")),
              ("output", Path("work/<candidate-output>.png"))],
    observation=Path("work/<candidate-scene-observation>.json"),
    hook_build=Path("work/<exact-hook-build>/supersampling-hook-build.json"))
manifest = {"format": q.FORMAT, "cases": [control, candidate],
            "pairs": [{"control": "ubi-control", "candidate": "ubi-candidate"}]}
Path("work/<new-evidence-manifest>.json").write_text(json.dumps(manifest, indent=2))
```

The paths in this example are placeholders. A case without captures or guarded
observations can still be recorded to expose the missing evidence. `record_case`
only hashes existing files and constructs a record; it launches or edits nothing.
Pass `kind="synthetic"` for standalone/mock fixtures. Synthetic cases never count
toward native edition coverage.

Each artifact reference contains its path and SHA256. Standard artifact names
are `result.private.json`, `checkpoint.private.json`, `owned.private.json`,
`memory.private.json`, `hook-state.private.json`, `hook-initialized.private.json`,
`recovery-journal.private.json`, `portable-before/snapshot.private.json` and the
exact hook build manifest. Source snapshots below the case's `source/` directory
must match the controller checkpoint. Candidate initialization must match the
hook build's DLL identity and native source identities. Current game binaries
must match their checkpoint, including the original edition EXE and DXVK pin.
The combined build currently has fourteen native source/header inputs plus its
DLL and fixture EXE. Their retained build-directory snapshots are hashed against
the exact build manifest; the report does not substitute current edited sources
for the files actually compiled.

### Same-frame native captures

Pass `native_captures=[Path("work/<case>/capture-1.json")]` to `record_case` for
the source-bound hook's `s7-native-frame-capture-v1` sidecars. It binds the matching
`capture-1-source.bmp` and `capture-1-output.bmp` automatically. Supply the exact
combined `hook_build` manifest as above. These are GPU surface readbacks of the
same frame after filtering and before Present, not desktop screenshots.

The aggregator separates `gpu_buffer_pair_bound`,
`native_scene_dimensions_recorded` and `filtered_present_recorded`. Buffer
completion alone is insufficient: a filtered presentation requires the matching
physical client/viewport/selected-target sizes and native draw counters, successful
`filtered_present_hresult` and final HRESULT, `presentation_path="filtered_output"`,
`fallback_used=false` and `filtered_api_succeeded=true`. A failed filtered Present
followed by a successful original fallback retains its failed filtered result;
it never counts as a filtered output proof. The 1x control uses the original
path with equal source/output sizes. Readback stalls remain excluded from pacing.

Availability flags control camera, LOD and sun fields. Missing elevation,
exposure or simulation turns remain `null`. Native buffer-pair/camera evidence
does not manufacture the missing complete guarded scene series or qualify the
comparability of separate control/candidate simulations. All visual/input,
lifecycle/fallback, material, shadow and release gates remain separate.

The original private `.s7s`/`.s7local` files from the portable-profile checkpoint
are checked against the still-retained owned profile. New save slots are allowed;
overwriting an original save rejects the preservation evidence. This tool never
restores a file. Actual recovery remains in [the interruption journal](../tools/graphics_lab/recover.py).

## Native scene observation contract

An observation is a private JSON record from a guarded runtime reader, bound to
that reader's frozen source. Notes or manually entered camera/sun values do not
establish native measurements. The reader must verify the actual process, parent,
original executable and renderer before obtaining these fields:

```json
{
  "format": "s7-native-scene-observation-v1",
  "origin": "guarded-runtime-read",
  "reader_source": {"path": "work/<frozen-reader>.py", "sha256": "<actual SHA256>"},
  "identity": {"pid": 1234, "parent": 1000, "sha256": "<original executable SHA256>"},
  "renderer_sha256": "<pinned DXVK SHA256>",
  "render_size": [1920, 1080],
  "output_size": [1280, 720],
  "camera": {"translation": [1, 2, 3], "look_at": [4, 5, 6], "fov_degrees": 45},
  "save_sha256": "<original loaded save SHA256>",
  "native_lod_scale": 1.5,
  "simulation_pace_ms": 100,
  "frames": [
    {"turn": 100, "elapsed_seconds": 0.0,
     "sun_azimuth_degrees": 240.0, "sun_elevation_degrees": 30.0, "exposure": 2.6,
     "source_image_sha256": "<source capture SHA256>",
     "output_image_sha256": "<output capture SHA256>"}
  ]
}
```

Values and the one-frame series above illustrate the format, not real evidence.
Provide 3–300 paired frames and all referenced images as source/output captures.
Use actual numeric camera translation/look-at/FOV, measured LOD, the loaded save's
hash, increasing native turns and elapsed times. Normalize azimuth to 0–360
degrees. Control/candidate physical output size, camera, save, LOD and 100-ms pace
must match. Native frame pairs must agree within .05 seconds, .1 degrees for each
sun angle and .001 exposure. Frames are neither dropped nor interpolated.

Captures must have the native source/output dimensions and be PNG or uncompressed
24/32-bit Windows BMP. The tool checks file identities, dimensions and structural
PNG/BMP bounds; it is not an image decoder or an independent visual inspection.
Do not supply a cropped-looking window screenshot as a client-buffer capture
without establishing its actual capture region/stage. A matching file reference
does not independently prove that the reader ran or the image depicts its label.
Runtime/source review and visual inspection remain necessary.

### Original-engine camera reader

`native/graphics_observation.h/.cpp` provides a read-only x86 component for the
owning capture/render thread. `Initialize()` binds the original executable and
the actual common Vision90/vBase90 SHA256 identities, retains read-only file
handles, and checks exported RVAs plus ASLR-normalized entry byte guards.
`Read(Snapshot&)` calls the original Vision main-context, camera, world-matrix,
final-FOV, clip-plane and context-size getters under an FPU/SSE preservation
scope. This establishes original getter provenance, **not the visible S7 scene
camera**. The first actual Ubisoft world capture showed a zero-translation main
context and fixed FOV despite the visible castle view. S7's custom renderer may
use a separate camera path; pan/zoom response must establish the relationship.
Neither this context nor a generic D3D9 fixed-function transform substitutes for
that validation. The component never writes game/profile state.

The snapshot is a fixed 124-byte POD with mandatory availability bits. A camera
can be recorded in the alternate native form
`{"world_matrix":[16 numeric values],"fov_native":[x,y],"clip_native":[near,far]}`.
The original world-matrix getter writes fifteen components; the homogeneous last
element uses the same identity initialization as the original engine's caller,
and is a canonical element rather than a separately measured field. Native FOV
units are retained. Invalid output, changed context/camera or getter faults yield
an unavailable camera rather than partial measurements.

LOD is read only when the independently selected edition's live known setter
matches its two-MULSS/MOVSS-store shape and the derived destination lies inside
the guarded executable image. Steam's packed file bytes cannot qualify this
runtime condition. The existing `atmosphere_clock.h` supplies only guarded
Mittsommer azimuth and native atmosphere-clock milliseconds. **Sun elevation,
exposure and native simulation turn are not supplied by this component.** They
must remain unavailable until mapped separately; do not derive fake measurements
from authored curves or process uptime to satisfy the strict pair contract.

Build CPU fixtures and a normal object with
`rtk proxy pwsh -NoProfile -File native/graphics_observation_build.ps1 -OutputDirectory work/<new-observer-build>`.
The updated fixture run passed 36 calling-convention, canary, missing-data,
LastError and FPU/SSE checks. This is synthetic ABI evidence, not runtime qualification of the
getters, Steam LOD, sun data or any edition's visual output. The component links
`bcrypt.lib` and depends on its own header plus `atmosphere_clock.h` and
`atmosphere_fp.h`; source/build fingerprints must accompany combined hook builds.

## Run and interpret aggregation

```powershell
rtk proxy python -m tools.graphics_lab.qualification work/<evidence-manifest>.json --output work/<new-report>.json
rtk proxy python -m unittest tools.graphics_lab.test_qualification -v
```

The report separates `runtime_records_consistent` from
`native_ssaa_records_consistent` and `measurements_comparable`. Edition coverage
lists consistent original-game records, native SSAA records, and comparable
capture pairs independently. It also reports native counters and memory metrics;
missing observations remain missing. Real fallback/lifecycle, input/UI, visual,
material, shadow and final-package gates require their own evidence.

Bounds: 64 cases/pairs, 32 captures per case, 1 MiB per JSON/source record and
128 MiB per image, all under `work/` without links/junctions. Existing report files
are never overwritten. Private hashes/records are not cryptographic attestation.

## Native shadow/detail starting point

`native/visual_detail.inc` maps History and `native/gold_visual.inc` independently
maps Gold. `native/visual_distance.h` supplies internal scales 1.5, 2, 2.5, 3 and
5; the last internal step must not be confused with older UI documentation or
enabled simply because it exists. Model LOD depends on rendering resolution.
Shadow range is separate and can be linked to detail. Detailed maps use a 4096
base; Ultra uses 8192 and .92 near-cascade weighting. No increase beyond this is
justified by the current evidence.

The current distant correction preserves the original 1/2048 footprint only for
cascades 2/3 on an owned 4096/8192 map with the exact reciprocal vector. Near
cascades keep original inputs. Gold's stack/ECX/EAX bridge has a distinct ABI.
Global negative mip bias was already removed after the wall-flicker investigation.

The next justified filter experiment starts with matched current-filter captures
under fixed SSAA: static wall transitions separately from animated foliage, with
native mip sampling and measured cascade/range/LOD values. Only a repeatable
remaining shadow artifact can justify changing the distant footprint or cascade
behavior. Raising map size or reintroducing negative bias does not establish a
new improvement. See [the shadow/detail lab](SHADOW-DETAIL-LAB.md) for measurement
limits and [the previous correction](SHADOW-DOF-REVIEW.md) for observed results.
