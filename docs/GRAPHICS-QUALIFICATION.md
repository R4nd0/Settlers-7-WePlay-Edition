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

## Visual decisions, 2026-11-26

Accepted supersampling and water B remain the comparison baseline. The terrain
macro trial, distant-shadow footprint trial, AO profile B and leaf hemisphere
trial have not shown a convincing benefit at normal camera distance. Their
successful bindings are diagnostic evidence, not a reason to expose them as
qualified launcher options. The AO and hemisphere strength variants are frozen.
Private comparison galleries remain under `outputs/`, with complete output
views, unchanged crops and OFF/ON/OFF controls; no game images are committed.

The subsequent isolated direct-diffuse Leaf pixel-shader trial shows a visible
change: darker leaf sides become lighter and more readable, with a possible
flatter appearance. Its V3 transport, actual balanced substitutions and matched
OFF/ON/OFF images are verified. Visual acceptance, performance and final package
qualification remain open; this candidate has not entered the qualified launcher
set. See [the direct Leaf lab](LEAF-WRAPPED-DIFFUSE-LAB.md).

Original-UI Clutter and Shadows High/Off/High trials retained the black road
streaks. The selected Grass colour/depth passes stopped under Clutter Off and
returned under High. Selected shadow generation/mask passes likewise stopped
under Shadows Off while world drawing continued. This weakens those observed
passes as necessary direct generators of the streaks, but identifies no causing
pass and excludes no unobserved path. Exact camera/light vectors matched;
animation, seasonal colours and tooltips still differ across the longer trials.

The completed Steam SDK/DXVK baseline b46 reproduced the original-menu exit
access violation without the modern graphics DLL. Its retained owner admission
and independent audit verified physical module identities, including the
mapped-file proof for driver path aliases. The failing original instruction and
call chain match the earlier Steam graphics cases. This establishes reproduction
in that SDK baseline; it does not establish a pristine retail baseline or rule
out every graphics-related cause. No destructor/free bypass is justified.

Leaf trial l47 also ended with an access violation after regular Alt+F4. Its
memory monitor remained healthy, with no pressure, and shared profiles, game
binaries and private options verified preservation/restoration. Neither that
restoration nor balanced material substitutions waives the failed exit.

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
Historical native20 builds retain fourteen native source/header inputs plus the
DLL and fixture EXE. The effects-v1 build has nineteen source inputs; effects-v2
adds three comparison inputs and a separate diagnostic comparison ABI of 12
bytes. The effects-v3 build has twenty-five source inputs, adding the versioned
wire/config/host-contract headers and an explicit effects_v2 ABI of 6180 bytes.
The effects-v4 build has twenty-eight source inputs, adding the separate water
module/draw contract and water ABI of 1076 bytes.
The effects-v5 build has twenty-nine source inputs, adding only
`terrain_macro_guard.h`. It keeps the effects-v4 ABI sizes, capabilities and
12-byte diagnostic comparison ABI. The larger source set establishes support
for the fixed terrain recipe; it does not establish that recipe was enabled or
visibly improved an actual frame. Recipe variant 3 uses the existing version-2,
6180-byte material transport and requires its explicit stage/configuration and
runtime substitution evidence. It is separate from the fixed shadow diagnostic.
The effects-v6 build has thirty-two source inputs: the effects-v5 set plus the
fixed shadow policy and its module header/implementation. Main ABI sizes and
reported capabilities stay unchanged. Its separate diagnostic ABI must contain
exactly `comparison: 12` and `shadow: 12`; the extra shadow intent is not a
release capability. Build consistency alone does not qualify shadow activation,
restoration, pixels or performance.
The actual32-source sh42 trial retained valid native GPU pairs and a separate
same-frame shadow application/restoration review, but established no convincing
normal-view gain. Its original-menu access violation and unclassified changed
private autosave keep the whole case rejected. The retained aggregate is
`work/sh42-qualification-20261125-01`; successful native deinitialization counters
do not waive the failed process exit.
The effects-v7 build has thirty-six source inputs: the exact effects-v6 set plus
the UP observation protocol and the AO policy/module header/implementation. Its
main ABI and capabilities remain unchanged; its diagnostic ABI must contain
exactly `comparison: 12`, `shadow: 12` and `ambient: 12`. Ambient capture state is
independent of shadow state, including its frame proof and raw PS81 bitwords.
This source schema establishes neither actual AO activation nor a visual pass.
Read-only UP observation also does not identify the source of the black streaks.
The effects-v8 build retains the same thirty-six source names and main ABI, but
requires the additional diagnostic entry `ambient_v2: 12`. Historical effects-v7
retains exactly its three diagnostic entries and the original profile-A contract;
shared source names do not grant profile-B support. Profile B is an explicit
version-2, twelve-byte AO operation, with its own fixed policy. Internal AO Stats
are version2/320 bytes with the profile appended at offset312; shadow Stats remain
312 bytes. Exact immutable source snapshots bind this difference. This does not
introduce automatic visual qualification or a new evidence artifact role.
The effects-v9 build adds the explicit Leaf pixel-shader transport, version3 and
7204 bytes, as `effects_v3`. It retains historical V1/V2/water sizes, capabilities
15 and the four effects-v8 diagnostic entries. Its thirty-eight inputs include
the separate V3 configuration and host-contract headers. The new transport uses
an appended Leaf pixel path; historical vertex/terrain paths cannot carry it.
Only the fixed isolated Leaf recipe is admitted. A successful transport or
authored GPU fixture does not establish a visible improvement in the game.

All effects sets retain three binaries, capabilities 15, and explicit
initialization/observation/effects/capture ABI sizes. All ten source sets
remain supported. Their retained build-directory
snapshots are hashed against the exact build manifest; the report does not
substitute current edited sources for the files actually compiled. Unknown
source sets, capabilities, ABI sizes, extra/missing identities and changed
snapshots reject consistency.

Graceful terminal detachment can follow either a recorded window shutdown or
the guarded early original-engine deinitialization route. The latter requires
the supported effects build schema, positive deinitialization calls, the same
number of returned original calls, zero rejected calls, cleanup completed and
shutdown stage 3, together with exit code zero and no owned forced stop. An early
cleanup counter alone never waives a failed exit or the independent save,
binary, recovery and memory checks.

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
overwriting an original manual save rejects the preservation evidence. A narrow
Gold/History exception classifies the original rotating autosave only when the guarded
original GetSaveGameSlotRangeSP code and original Lua mapping establish its
role, and its matching before-image remains in the case backup. This preserves
`all_original_private_saves_unchanged=false` truthfully while separately requiring
all manual originals unchanged. Unknown changed files are never waived. This
tool never restores a file. Actual recovery remains in
[the interruption journal](../tools/graphics_lab/recover.py).

Ubisoft History additionally verifies its original registration initializer,
complete save-range callback and read-only double values 30/50. Its original
Interface archive contains the same verified Lua slot mapping as Gold. The
classification applies only to the exact edition's slot-31 relative path and
matching retained before-image. Steam's original code is packed, so matching
script text or an analogous Ubisoft callback cannot qualify its slot policy.
The query/read-only `tools/graphics_lab/save_role_read.py` inspected the actual
registered Steam callback during the owned R27 run. After closing, independent
review matched the entire 66-byte callback against the verified Ubisoft
instructions after only two constant-address and two call-relocation changes.
Both calls use the guarded original Lua-number insertion routine, with actual
first/count values 30/50. The original Steam Interface member then matched the
same verified Lua hash as Gold/Ubisoft, establishing quick index 1 and auto
index 2. Steam classification now additionally requires that exact retained
runtime receipt, the reviewed reader checkpoint, same-case process identity,
completed actual-case result and exact original packed EXE/Lua identities.
Only a changed original slot 31 with a matching retained before-image can use
this role classification; unknown/manual changes still reject preservation.
No reader launches a game or restores a save.

### Draw-correlated camera and lighting uniforms

New effects builds can include `scene_uniforms` from the exact original River
vertex/pixel programs, independently of the generic Vision camera. The qualifier
requires the original program hashes, positive draw/catalog identities, exact
capture-frame equality, configured full render target and viewport, format and
render-state guards, and finite raw VS12–15, VS16.xyz, PS5.xyz and PS7.rgb values.
The catalog hash must also match `observer_catalog_sha256` in the successful
initialization receipt. Numeric catalog IDs are not fixed: adding observation
resources changes them. Stale or unavailable uniforms remain null and are never
replaced with the Vision context.

`same_frame_river_uniforms_recorded` establishes the recorded draw/capture
association, not a complete A/B scene match. The raw fields represent the view
projection, shader eye, sun direction and sun specular color; elevation and
exposure remain unavailable. Full-size target dimensions alone do not prove the
draw contributed to the final image, so `visible_camera_validated` remains false
pending actual tracking/contribution review. The actual Gold pan/zoom comparison
showed changing River eye/projection values while the generic Vision camera
remained fixed; those earlier uniform samples were not taken on the BMP frames.

The optional material epoch records requested/configured/drawn masks and
revisions. A matching epoch requires geometry in that frame and no unsafe,
disabled or module-failure state. This describes the configuration used for
geometry; it does not prove any individual material was changed. Per-material
draw/substitution records and visual review remain separate requirements.

For an actual material case, the aggregator can also bind `effects-config.bin`
and `effects-configured.private.json`. It checks the immutable 4132-byte/version-1
or 6180-byte/version-2 header, dimensions, finite parameters, bounded terminated
UTF16 path arrays and exact initialized DLL/export/guard receipt. It opens no
program path contained in that binary. A native `effects_config_version=2`
requires the effects-v3 source set, exact V2 ABI and matching config/receipt.
Active masks must remain within the prepared resource flags. A legacy V1 version
inferred from the bound historical transport is explicitly distinguished from
a version measured in the capture. Unknown versions, size/version mismatches or
a receipt from another DLL reject consistency. Neither configuration nor a
matching drawn epoch automatically establishes a visible improvement.

### Separate recipient water program

The separate water path is qualified independently from the legacy material
mask. Its immutable 1076-byte/version-1 configuration must match render size,
enabled preparation, the exact recipient-generated PS hash/size/envelope and
same-case recipe path. The stage record must identify all three pinned original
Water resource aliases. A successful `WePlayNativeGraphicsConfigureWater`
receipt must match the initialized DLL and its compiled effects-v4 source set.
Stager `active` or `runtime_verified` flags are not runtime proof.
The selection is an explicit fixed hash/recipe pair: A is
`river-depth-fresnel-v1-candidate`, B is `river-depth-fresnel-v2-balanced`.
Unknown pins or cross-labeled paths are rejected. The 1076-byte transport is
unchanged. On-B captures must also report native variant 2; historical A captures
without that later field retain their bound-A provenance.

The capture's `water.frame` already normalizes module-local completed frames by
the native frame origin. It must equal the actual capture frame. On frames
require enabled/requested/configured state, a positive epoch, positive equal
completed applications/restorations, both last-operation frames equal to that
capture, actual shader creation and no shader/set/restore/exception/pending-ref
faults. Initial off controls may be unconfigured; they still require a healthy
same-frame native record with no applications/restorations. A retained global
enable flag alone cannot establish replacement.

A water-only case additionally binds the intentional empty legacy bootstrap
4132-byte/version-1 config, legacy flags zero and native Water intent 1. Any
active legacy River mutation rejects the separate-water proof. Reports keep
water replacement, original-water control, water-only configuration, ordinary
material masks and presentation evidence separate. Same-source controls,
control/control residuals, original poses/light/UV and visual review still gate
quality claims; failed exits remain whole-case failures.

The actual w34 A triplet recorded water off/on/off, legacy masks zero throughout
and matching raw pose/light/UV. Its on frame had 120 same-frame applications and
restorations with no shader/state faults. The case nevertheless ended through
the existing owned memory guardian with `0xe0537021`: `scan_timeout`, failed
monitoring/recovery requested, max-pressure level 1 and process-private peak
1,898 MiB. This case is **not an AV observation**. No new manual slot 72 or CDB
admission was completed, and a clean original-UI exit was not established.
Shared profiles, binaries and private options verified restoration with no
cleanup errors. The owned rotating autosave retained its matching before-image;
manual originals were unchanged and no save was restored. Per-frame water proof
remains recorded separately while whole-case, lifecycle, visual and release
qualification remain false. Memory cutoffs were not relaxed to obtain a pass.

The subsequent w36 balanced-B run recorded a matched-pose/light/UV off/on/off
triplet with native variant 2 and 120 same-frame applications/restorations on
the enabled frame, while legacy material flags stayed zero. The original menu
exit completed with process exit 0. Its owned CDB watch independently recorded
zero first-/second-chance AV events, zero dumps and no watcher termination. A
new free slot 72 was retained; all earlier private save hashes, shared profiles,
game binaries and private configuration verified preservation/restoration.
The UI route was observed by the owning operator; exit/watch/source records are
the separate measured lifecycle evidence.

This is one clean lifecycle observation, not proof that the earlier c30 AV cause
was fixed. w36 monitoring completed without monitor failure or recovery request,
but recorded warning level 2, minimum host commit headroom 893 MiB and a
1,945-MiB process-private peak. The existing pressure-free qualification rule
therefore still rejects the whole case. The reason is reported as a memory
warning/limit rather than a failed monitor; no acceptance condition was relaxed.
Per-frame B proof and the clean exit remain visible separately from the memory,
visual and release gates. No new Steam role read was needed to excuse a change:
all original private saves, including the rotating autosave, were unchanged.

### Diagnostic water-scroll isolation

`native/material_comparison.h/.cpp` is a separately staged diagnostic component,
not a production graphics choice. Original River PS80 is the CTAB float4
`g_UVScrollTimer`; it continued changing during the paused material comparison.
The component matches the exact original River programs and full render context,
reads all four finite components, and pins the first value only after a successful
original geometry draw and bitwise restoration. Every later matching control and
candidate draw receives the same float4 temporarily. Each draw restores its own
original PS80 even on failure or exception. It changes no game clock, RNG,
simulation, asset, image or file.

The trusted hook must nest this outside material effects, restore both modules
before observing the original shader, and stop any unsafe rendering/fallback
claim if immediate restoration retry fails. An explicit diagnostic-only
12-byte configuration is separate from read-only observation configuration;
the production loader must not expose it. Reconfiguration/disable clears the
pin. Routine Release cleanup drops all shader identities/device references while
retaining the pin and already-recorded frame evidence. An actual device Reset
or attachment-context change additionally calls `InvalidateFrame()` to discard
current/completed frame evidence. Shutdown clears everything after all
transactions are restored.

A retained pin alone proves no capture used it. State includes local frame,
last application/restoration frames and completed-frame counters; the integrating
host must bind them to the actual GPU capture frame. It must also preserve the
pin across ordinary material on/off switches. First-match shader scrolling can
be isolated this way, but reflected/refracted scene texture animation remains
possible. A residual original/original image comparison is still required.
Diagnostic constant-setting costs also must not be reported as production cost.

The qualifier binds this diagnostic to `comparison_uv_requested=true` in the
frozen source checkpoint, a successful OBSERVE initialization with catalog hash,
and `comparison-configured.private.json` from the exact diagnostic export and
initialized DLL. The raw capture record must explicitly say `diagnostic_only`,
match the capture frame and its last application/restoration frames, record
positive equal completed application/restoration counts, retain a finite float4
pin from a valid earlier/current frame and a positive epoch, and show requested,
active, pinned and fault-free state. It reports
`draw_local_ps80_isolation_recorded` separately from shader observation and
filtered-presentation evidence. Matching the same pin across separate captures
still requires the material comparison utility and a residual control/control
image comparison; it never qualifies visual quality automatically.

The standalone x86 build compiles production code and a synthetic COM fixture
under `/W4 /WX /fp:strict`. Its 1,232 CPU checks cover matching/context refusals,
finite inputs, successful and failed draws, partial/silent/throwing setters,
bitwise restoration and retry, bounded identity references, foreign-thread
cleanup, no-geometry frame counters, LastError and FPU/SSE controls. This
standalone build starts no game or GPU; its CPU checks alone establish neither
runtime integration nor visual isolation. Actual observations follow below.

The first actual 24 comparison attempt did not commit a pin and supplies no
water-isolation proof. Independent read-only samples confirmed the original
River programs, eligible 4K context and finite changing PS80 values. Review found
that caching negative shader identities could fill the 128-entry cache before a
later River program appeared. The corrected component retains only positive
identities, still hashes/matches valid programs when that positive cache is full,
and chooses its device/thread only after an eligible River input. CPU regressions
cover 129 wrong pairs at the actual 2592/3740 byte sizes before a valid pair,
positive-cache saturation, and an unrelated first draw on another thread. These
checks establish the source correction; actual pinning was subsequently observed
in the R25 run described below.

The subsequent actual Ubisoft History R25 case recorded three 4K/off-on-off
captures with material masks 0/7/0 and drawn revisions 1/2/3. Their original River
view-projection, eye, raw sun direction and sun specular vectors matched exactly.
All three retained the same diagnostic PS80 float4, epoch and initial pin frame,
with 120 applications and 120 bitwise restorations in each captured frame. The
recorded cumulative failed-original-draw counters were zero. These are positive
render-time diagnostic and filtered-buffer observations; application/restoration
counters generally include transactions whose original draw later fails and
must not be renamed successful original-draw counts.

R25 still exited with access violation `0xc0000005`. Whole-case consistency and
release qualification remain rejected despite all five binary pins, shared
profiles and private configuration verifying restoration, a 1,914-MiB process-private
peak and no recorded pressure/monitor failure. The exact source-verified owned
rotating autosave change retained its before-image; manual originals were
unchanged and no save was restored. A separate Windows GPU Process Memory
snapshot recorded approximately 1,974.69 MiB dedicated, 394.54 MiB shared and
2,183.22 MiB committed. Those are separate overlapping counter views, not values
to add together or to process-private memory; one snapshot is not a peak or FPS
measurement. Exit reliability and final visual/control-residual review remain
open independently of the successful UV isolation.

Steam History R27 likewise retained three filtered 4K/off-on-off captures with
exactly matching River view-projection, eye and raw sunlight vectors, one stable
PS80 pin/epoch and 120 applications/restorations per captured frame. All original
private saves remained unchanged; two added saves were retained. The verified
slot-31 policy therefore required no changed-original exception in this case.
R27 still ended with `0xc0000005` through the original exit path, without a forced
stop. Whole-case consistency remains rejected. Shared profiles, binaries and
configuration verified restoration; process-private peak was 1,982 MiB with no
recorded pressure or monitor failure. Its separate GPU-memory point snapshot
was 1,974.69 MiB dedicated, 410.54 MiB shared and 2,199.22 MiB committed, again
independent overlapping views rather than quantities to sum or a peak. The
positive graphics observations do not establish a reliable exit or a release.

`tools/graphics_lab/camera_read.py` is a separate, bounded read-only process
reader for an existing owned case. Ubisoft and Gold mappings follow the actual
registered GetCamTranslation/GetCamLookAt callbacks and guarded instructions.
Steam uses a bounded original-name registration lookup plus exact unpacked
instruction templates because its file code is packed; that runtime route still
requires actual verification. The helper calls no game functions, writes no
memory and supplies no same-frame GPU or FOV proof.

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
