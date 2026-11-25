# Native graphics evidence, 2026-11-25

This records observed progress separately from release qualification. The earlier
local gallery is `outputs/Grafikvergleich-Zwischenstand-20261124/Vergleich.html`.
Separate River pixel-program trials are documented in
[WATER-MODERNIZATION.md](WATER-MODERNIZATION.md) and
[WATER-BALANCED.md](WATER-BALANCED.md). Galleries and source images remain ignored
local artifacts.

| Area | Actual evidence | Remaining limit |
| --- | --- | --- |
| Water | Steam w34/w36 separately captured original/new/original with matching measured camera, light and pinned River UV phase within each case. The new pixel program visibly reduces dense silvery surface detail and clarifies shallow stones. Both states use 3840×2160 → 1920×1080. | The initial A variant appears too matte. B raises the reflection floor, but A/B use different phases and do not establish a controlled gloss gain. Natural motion, additional views and three-edition qualification remain open. |
| Terrain | Steam t30 executed the extended near-detail-distance and normal-strength variant in separate off/on/off captures. | The normal-distance images did not establish convincing visible improvement; the technical pass is not a visual pass. |
| Vegetation | Steam t30 executed the changed Leaf lobe in separate off/on/off captures. | The captured light/view direction is a negative control for the intended backlight. No convincing normal-distance benefit established. |
| Shadows | Original shadow binding, map and sampling parameters were inspected. A guarded near-filter candidate is prepared. | No new controlled before/after result without ReShade, and no claim of reduced flicker. |

## Water images and controls

The user visually accepted water B on2026-11-25. Keep its optical recipe fixed;
technical compatibility, performance and package checks remain separate.

The newer water-only galleries are
`outputs/Wasservergleich-20261125/Vergleich.html` (A) and
`outputs/Wasservergleich-B-20261125/Vergleich.html` (B). They retain unaltered
full output images, source-resolution crops and the second OFF control. Within
w36, all three captures match measured raw camera/light/diagnostic UV; the ON
frame has120 water applications and120 restorations in that same frame, with
legacy material effects disabled. These scalar checks support the comparison;
the image itself must demonstrate the benefit.

w36 saved a new free slot and exited through the original menu with code0.
The admitted debugger recorded no access violation and no dump. Shared profiles,
old saves and game binaries were preserved, with private options restored.
Nevertheless its overall qualification remains false: the existing memory
policy recorded pressure2 and minimum host commit headroom893MiB. This isolated
exit pass does not establish a fix for the earlier exit crashes. The authoritative
retained aggregate is `work/w36-qualification-20261125-03`.

The subsequent Ubisoft w38 motion attempt was stopped normally before loading:
Windows clipped the oversized startup client, and an engine Reset changed the
render target before the first admitted 4K draw. The hook correctly declined the
mismatched context. It produced no qualified water or motion evidence. Startup
ordering is being corrected without relaxing render-context guards.

The following section describes the earlier constant-only trial, not the new
River pixel-program result.

The gallery's first four PNGs are lossless copies of the retained actual25
analysis images. Output PNG pixels were independently compared with the original
GPU BMP RGB pixels. The water detail is a crop of the high-resolution source,
without colour enhancement. A third image shows the effect switched off again.

The actual case is `work/ngfx-ubi-comparison25-20261124`, capture sequences 1,
3 and 5, frames 30878, 37652 and 37677. The comparison has 120 draw-local
applications and 120 restorations in each captured frame. Shader UV pinning is
diagnostic-only; reflected/refracted scene textures can still animate. The
control repeat is required to assess that residual. Pixel differences do not
automatically establish better appearance.

Retained image assessment and receipts:

- `work/material-shaders-20261124/actual25-analysis/assessment.private.json`
- `work/material-shaders-20261124/actual25-analysis/roi-refined.private.json`
- `work/native25-ubi-evidence-20261124-01`
- `work/native27-steam-evidence-20261125-01`

Steam27 independently provided three valid filtered captures with matching
measured pose/light and pinned UV phase. This does not convert either case into
a successful lifecycle or visual release qualification.

## Shadow evidence boundary

The earlier native distant-shadow correction is documented in
[SHADOW-DOF-REVIEW.md](SHADOW-DOF-REVIEW.md). Its retained comparison used
SMAA through ReShade and does not establish a new result for the current work
without ReShade. Its reversal counts also did not establish a general flicker
cure. Existing code and synthetic fixture success must not be presented as new
visible shadow progress.

The new near-filter candidate and the offline temporal comparator remain
separate from an actual-game visual pass. See
[SHADOW-DETAIL-LAB.md](SHADOW-DETAIL-LAB.md).

The retained actual draw snapshot is
`work/ngfx-ubi-comparison24-20261124/observer-pin-diagnostic-snapshot.private.json`,
frames 69244–69246, observer target 1, vertex/pixel IDs 3/4. Each records sampler
1 as a valid 2048×2048 D16 texture, pixel constant 57.xy as exactly 1/2048 and
constant 33 as [2.75, 2.75, 0, 0]. The gallery's `Schatten-Messwerte.json`
contains those selected draw records and the original snapshot SHA256. This is
a binding observation, not a before/after result. The unintegrated new near
candidate rejects a 2048 map and was not active in those observations.

## Reported black streaks

The reported paired black streaks are visible in the actual25 high source
backbuffer before downsampling, including with material effects disabled.
The output reproduces them. This rules out an output-only downsampling artifact
and a cause requiring enabled material replacements. It does not identify the
generating draw, prove a UI origin, or exclude an upstream supersampling effect.

The gallery includes high-source and output crops; the output crop is explicitly
labelled as enlarged with nearest-neighbour sampling. The independent read-only
review is `work/ui-streak-analysis-20261125/findings.private.json`. No native
functional change or extra game launch was made during that review.

## Stability remains open

Actual25 Ubisoft and actual27 Steam exited with `0xc0000005` after the original
desktop-exit action. Steam27 now has first- and second-chance exception captures.
Independent original-code analysis places the failing write in the original
StormMM allocation-list cleanup during connectivity/sandbox teardown. Both
graphics deinitialization calls had already returned. The invalid allocation
header and its history are absent from the basic dumps; the corruption cause
and any integration relationship remain unproven. No null-check or skipped
free was added to hide the failure.

Steam27 was not forcibly stopped. Original/shared profiles and game binaries
were preserved; private options were restored, with no cleanup error. Its
memory monitor stayed healthy with pressure 0 and peak private memory 1982 MiB.
Debugger timings are diagnostic observations, not performance measurements.
There is no production release qualification from these cases.

## Subsequent V2 trial: technical pass, visual rejection

The separate Steam trial `work/t30` used the immutable `work/n30` build,
source set `effects-v3` with25 source snapshots. Its SHA256 is
`27b0542433492aca2ccbdfcf5406e5ea5b9db635f7c0a2def7e8764442ab7a1c`.
Material V2 is an explicitly declared6180-byte/version2 transport; the original
4132-byte/version1 allocation remains supported independently. Guard-page tests
verify that a legacy allocation is not overread as V2. The actual owned-worker
prefix was392 bytes, giving6572 bytes including V2 within the8192-byte bound.

The new terrain/leaf backend supports five pinned recipient-generated programs.
It uses a positive-only identity cache, accepts a matching shader after cache
capacity is reached, binds device/thread ownership only after an eligible draw,
and retains original query references independently of cache capacity through
mutation/restoration. CPU transactions and real GPU V1/comparison regressions
passed. Those checks establish safe machinery, not visual quality.

Actual t30 loaded the existing private checkpoint and created a new manual
slot75 through the original UI. It captured separate terrain0/2/0 sequences
1/3/5 and vegetation0/4/0 sequences6/8/10. Both triplets matched measured raw
camera, lighting and River comparison phase at the fixed4K→1080p dimensions.
All five programs were created once,635 material draws were applied/restored,
and query/set/restore/cache faults were zero. No original query references
remained held after the transactions.

**Neither triplet establishes a convincing visual gain in that normal view.**
Terrain-region changes were similar to control-repeat movement. The leaf view
was a front-lit/zero-backlight control; its measured centre-view dot with the
sun was approximately−0.792. A sun-facing view remains necessary to assess the
candidate's changed angular lobe. Lossless images and the independent review
are under `work/material-t30-analysis`; no improvement percentage is inferred.

The observer now measures31 explicitly available render states only for
selected, budgeted capture samples. The original952-byte request is unchanged;
output samples/Stats have exact-size guards. Actual observations identified
River, cascade mask, SSAO, Decal and menu UI. A rearmed snapshot separately
records later world observations. They do not establish the generating pass
for the black streaks. Coverage still excludes DrawPrimitiveUP/DrawIndexedUP;
absence on the currently observed Draw/Indexed path must not be reported as
global absence.

t30 again exited with the original StormMM access violation, with first- and
second-chance captures in `work/c30`. Its shared profiles, game binaries and
private options were preserved/restored; memory pressure stayed0 with healthy
monitoring and peak private memory1976 MiB. The strict same-case role review and
modern source/ABI aggregate are in `work/t30-qualification-20261125-01`; the
whole case and visual/release qualifications remain false.
