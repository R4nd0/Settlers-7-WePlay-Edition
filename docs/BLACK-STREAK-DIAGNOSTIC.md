# Original-pass diagnosis and visible shadow target

The visible target is stable, readable ground/wall/roof shadows at ordinary
gameplay zoom: fewer thin broken lines, less edge crawling and smoother distance
transitions, without detached shadows, halos, light leakage or smeared foliage.
The black ground streaks are an artifact to identify before selecting a fix.
No new shadow improvement or streak generator has been runtime-qualified.

## Scenes and control conditions

Use the original saved castle/road scene containing the paired black lines,
a static wall/roof corner, and a tree line crossing near/mid/far detail ranges.
Use a normal playable camera distance, not only an extreme crop or zoom.
Compare off/on/off in the same owned run; the second off frame measures residual
animation/noise. Keep original save, view, raw light, LOD, selected renderer,
100-ms simulation pace and SSAA fixed for a pass experiment. Use the guarded
River eye/projection/light uniforms, not the unrelated fixed Vision main camera.
Keep the mouse away from the world ROI and preserve the original profile/saves.

First establish whether the streaks persist at original-size rendering versus
2x rendering. This is an association test, not a shadow-quality A/B: rendering
resolution also changes the original model-LOD calculation and projection
half-texel terms. Record those differences. Then isolate one original pass at a
time, with material variants off. Root owns all actual game/GPU slots and guards.

## Evidence already retained

The existing actual crops show the black paired ground lines with materials off
and on. They are also present in the high-resolution backbuffer before the
downsample, so they do not require the enabled material variants or the output
filter alone. Their proximity to army UI is not proof that UI draws generate
them. The 25/27 rendering observations do not resolve the independent exit AV.

The retained actual shadow sample is
`work/ngfx-ubi-comparison24-20261124/observer-pin-diagnostic-snapshot.private.json`:
three original MaskShader draws recorded sampler 1 as 2048-square D16, PS57.xy
as 1/2048 and PS33.xy as 2.75/2.75. This establishes a baseline binding, not a
visible improvement. The existing distant 4096/8192 correction remains separate.
The staged `native/shadow_detail_candidate.h` is off, unintegrated and rejects
2048 maps. No 2048 widening is justified solely by those measurements.

## Exact original source mapping

`tools/shadow_detail/pass_inventory.py` supports independent/per-record source
fences and mixed-platform Vision libraries without broadening the material
replacement parser. It validates the original directory/payload fingerprints,
length-prefixed SM2/3 D3D9 token envelopes and CTAB; non-DX9 payloads are explicitly
listed as ignored. It exports scalar identities, bindings and declared states,
never original source or bytecode bodies. Declared states remain unverified
runtime values. Numeric legacy family 2 is only a wire label for diagnostic
records and never activates a material effect.

The private catalog in `work/shader-pass-diagnostics-20261125-02/` has 271 scalar
entries from 15 original libraries. All 15 selected resource hashes independently
match the owned Gold, Ubisoft History and Steam History archives; this is source
coverage, not proof that a pass is actually drawn in any edition.

| Original pass | Source/compiled facts | Required next evidence |
| --- | --- | --- |
| Vision `BlobShadow` | SM2 VS 412 bytes, PS 332 bytes; VS matrix c8–11 and UV planes c64/c65; PS `ShadowColor` c2 and texture s0. | Actual pair, world target/viewport, texture descriptor, color and complete blend/depth/alpha states. |
| Vision `BlobShadow_Terrain` | SM2 VS 468 bytes, PS 332 bytes; extra VS sector scale/offset c38; PS `ShadowColor` c7. | Same runtime checks, independently from regular BlobShadow. |
| S7 `Shadow/MaskShader` | Known cascade PS57 inverse footprint, PS33 kernel and depth sampler 1. | Actual cascade/range context; first-per-frame samples do not cover every cascade. |
| S7 `PostFX/SSAOShader` | VS target size c68; PS strength c81 and depth texture s0. | Actual sizes/strength/depth inputs and an original SSAO off/on association test. Do not blindly zero the entire c81 vector. |
| S7 `PostFX/SSAOBlurShader` | VS screen size c18; PS target size c37, depth s0 and AO s1. | Actual target/viewport and texture-size relationship at each render scale. |
| S7 `GUI/UIShader` | VS GUI matrix c64–67, up to four pixel textures. Some records inherit shaders. | Actual pair and blend factors; identical programs can serve multiple UI states. |
| S7 `Decal/OuterShader` and Overlay selection/blocking | Original projection/screen-size/color/shadow/AO bindings. `InnerShader` has no separately encoded VS. | Observe actual paired VS/PS rather than inventing inheritance or a UI/unit role. |
| S7 `Entity` depth/shadow/skinning/alpha permutations | Original ATI High library has 165 records and 145 directly encoded programs, including ShadowShader, ShadowSkinningShader, ShadowAlpha and DepthAlpha. Many records inherit one or both stages. | A separate 416-entry catalog adds these known identities; actual pair/caster context is still required. BlobShadow is not an exhaustive unit-shadow inventory. |

The original `Graphics/Vision/Shaders/BlobShadows.inc` hash is
`50492f20f12d492a1bcceb17f47763dc9ed1a3cc9811220e5a0cbd5aa3b2d565`.
Its pixel expression multiplies a texture sample by ShadowColor. The libraries
declare ZERO/INVSRCCOLOR blending, depth writes off, depth comparison EQUAL and
an alpha test. These facts make the pass a concrete diagnostic candidate, not
the established cause of the streaks. Use the compiled CTAB c2/c7 bindings;
conditional source declarations for other platforms are not register authority.

The optional catalog in `work/shader-pass-diagnostics-20261125-03-entity/` adds
the original ATI Entity programs without changing the eight probe roles. All
numeric IDs are regenerated and the corresponding capture config must travel
with that catalog. The tool's `--include-entity-ati` switch reproduces this
selection. This is still below the observer's 512-entry limit.

An offline hash join against the retained actual24 unknown-program rows already
identifies original SSAO, UI, decal and entity depth/skinning/alpha program
identities. It does not prove their pair or target. That old record overflowed
its 64-unknown-program list; absent BlobShadow entries therefore exclude nothing.
The richer catalog reduces reliance on that truncated unknown list.

## Read-only first probe

The generated `capture.bin` is the existing 952-byte observer ABI, with eight
exact program pairs: River, both blob passes, cascade MaskShader, SSAO, SSAO blur,
UI and Decal Outer. Every requested constant and sampler is checked against
that program's CTAB. The private `capture.private.json` binds its catalog hash,
actual numeric IDs and program hashes. IDs differ from older material catalogs.
All suppression is disabled. The existing source-bound initializer can use this
catalog; only Root schedules and invokes the actual experiment.

The actual older 22-source observer reports target/viewport/format,
blend/test/sRGB booleans, constants and sampler descriptors. Those reports do
**not** contain enough state to approve a neutralization. A verified source
extension is now promoted for the next build: 31 individually measured render
states with explicit availability bits and null unavailable values, plus actual
viewport coordinates. The 952-byte request is unchanged; native selected samples
are 984 bytes and Stats 80080 bytes, so all compiled readers must match. Its
770-check strict CPU/COM fixture covers partial poisoned getter failures,
availability, null/zero distinction, recovery, size guards and no extra state
queries outside selected-target budgets. The first actual runtime state capture
is retained from t30 below. First-per-target/per-frame sampling may miss later instances.
No observed sample is not evidence of absence when cache/query/capture/unknown
overflows occurred. The original observer cache has a fixed 256-entry bound;
its handling of late programs must be reviewed before treating a zero count as
an exclusion, following the already-fixed comparison-cache starvation defect.

The next build's source extension covers actual SRCBLEND/DSTBLEND/BLENDOP, separate-alpha enable,
alpha function/reference, Z enable/write/function, color-write mask and relevant
stencil/cull state. Sampler filter/address/LOD states are useful for a later UV
or texture-footprint fix. Keep availability explicit; missing states are not
zero/default values. Geometry/draw ordinal and per-frame matched counters would
let the pass be correlated to individual objects instead of only its first draw.
Inherited/pixel-only records require a verified actual pair or an explicitly
versioned stage-specific observation interface, not a wildcard invented in v1.
Selected capture frame N explicitly means completed presents before the draw;
its candidate presentation is N+1, not an automatic association with a later
GPU readback. The new report declares that convention directly.

The retained `work/t30/observer-state-snapshot.private.json` contains fifteen
actual selected samples, all with complete 31-field availability, context 7 and
zero getter/cache/context/capture failures. Its bound catalog/config/initializer
are checked in `observer-state-analysis.private.json`. UI157/158 samples are
startup frames 0–2; the world samples capture River352/353, Mask328/329,
SSAO167/168 and DecalOuter1/2 around frames 7911–7914. They do not automatically
describe the later normal-camera material triplets.

The cascade sampler remains 2048-square D16. Native SSAO renders a 1920×1080
target while sampling a 3840×2160 R32F depth texture, with a full-size source
target vector; that relationship may be intentional and is not itself a bug
finding. No regular/terrain BlobShadow or selected SSAOBlur identities were
counted on the observed successful DrawPrimitive/DrawIndexed path. The original
unknown/pair lists overflowed, but known-identity counters did not depend on
those truncated lists. Coverage is still limited: DrawPrimitiveUP and
DrawIndexedPrimitiveUP are not intercepted, so global absence of those passes
is unproven. A neutral-blob activation is therefore not yet justified. Extend
read-only draw coverage or use controlled original SSAO/shadow/scale association
before selecting the next diagnostic mutation.

## Staged neutral-RGB diagnostic policy

`native/streak_diagnostic_policy.h` is a pure disabled-by-default plan with no
device calls or runtime integration. It accepts only the exact two original
blob program pairs and their source-derived constant indices. It requires
explicit OBSERVE/diagnostic authorization, nonzero geometry, observed full-size
A8R8G8B8 target/viewport, a real 2D sampler, confirmed ZERO/INVSRCCOLOR ADD blend,
separate alpha and sRGB off, depth enabled/EQUAL with no writes, valid alpha
state and RGB writes, and a finite original ShadowColor. The old observer alone
cannot fill its required observation bits, so the plan remains ineligible.

For an eligible future diagnostic draw, only ShadowColor.rgb becomes zero.
Alpha stays **bitwise original**: the source shader's alpha-test decisions,
depth/stencil participation and alpha output retain their original inputs.
Zero RGB is neutral for the confirmed multiplicative color blend. This is more
specific than skipping an arbitrary draw or blackening a whole screen pass.
It removes a selected contribution to diagnose association; it is not a
production solution that deletes unit shadows.

A future transaction must snapshot, set/read back, execute the original draw,
and restore/read back the complete original float4 on success, HRESULT failure
and exception. Preserve LastError/FPU state, retain original shader identities
safely, restore before the original observer, and expose immediate retry/unsafe
state. Failed restoration must stop diagnostic rendering and original-fallback
claims. Configuration must be a separate explicit diagnostic interface, queued
at the safe render boundary and unavailable in production UI. Auto-disable on
reset/context change and bound it to the requested capture frames. No game
clock, simulation, save, asset or permanent D3D state is changed.

Thirty CPU checks validate the pure eligibility guards, exact pair/constant
selection and alpha-bit preservation. They do not establish transaction
lifecycle, GPU behavior, streak origin or visible improvement. Required GPU
fixtures must prove neutral RGB with unchanged alpha and successful restoration,
then actual off/neutral/off captures must reproduce the streak change above
control/control residuals without hiding unrelated scene regions. Only then is
pass association supported. A source-specific UV/projection/sampling correction
still needs separate evidence and visual review.

## Practical sequence

1. Original-size versus 2x association, normal camera and material variants off.
2. Read-only exact original-pass observation; reject missing/overflowed evidence.
3. If blob bindings/states match, a short neutral-RGB off/on/off diagnostic.
4. If not associated, use original SSAO/shadow/HUD feature controls independently,
   with recoverable options and matched frames; do not suppress an uninitialized
   full-screen render target and infer causality from a broken composition.
5. Implement the smallest source-specific correction, retain all current
   4096/8192 safeguards, then compare normal camera scenes and measured cost in
   all three original editions. No 2048 or visible-improvement label precedes
   actual evidence.

## Accepted-preset follow-up, 2026-11-26

An isolated Ubisoft History world reproduced the paired road streak beside the
stone bridge in Waldreich, with only the accepted Clear Bank/index1 and
Leaf B/index1 enabled. A successful actual GPU capture retained both the
3840×2160 source and 1920×1080 output. The rectangular paired streak is visible
in the high source. The captured frame records 120 water applications/restorations
and 13 Leaf B applications/restorations, without shader/set/restore failures.
This corroborates the upstream-of-downsample finding with the current accepted
preset; it still does not identify the generating draw.

The new [bounded draw-order/ROI diagnostic](DRAW-TRACE-DIAGNOSTIC.md) passed
strict CPU/COM tests and an authored DXVK GPU test, including two distinct
in-scene draw results read back before Present. It retains the exact original
binding hashes after each observed successful draw; it does not suppress draws
or change the accepted effects. The old v1 observer ABI remains unchanged.

No actual-game draw trace or controlled 100/200-percent pair completed. The
first world case was stopped by the controller after a Windows report-replacement
access error; bounded retry now handles transient publication denial while
persistent errors remain fatal. The next case was stopped by the existing memory
guard after host commit headroom fell to 103 MiB and the scanner timed out.
Both cases verified shared profiles/game binaries unchanged, private options
restored and empty cleanup errors. Their forced owner stops are not clean exits
or release qualifications. No other game process was closed.

Further game/GPU work requires a stable host memory slot with the existing
headroom/monitor checks passing. The causing pass, original SSAO association,
controlled scale comparison and actual correction remain open. No speculative
shader, shadow, alpha or texture fix has been activated.

The resumed 100-percent case reached the same saved Waldreich scene with the
accepted effects requested. The prominent paired streak was not apparent in
the observed normal-size preview, but the capture helper correctly refused
the unhealthy memory state before any GPU readback. The existing guardian then
stopped its owned client at pressure1, minimum host commit headroom1040MiB.
Monitoring did not fail; shared profiles and game binaries remained unchanged,
private options were restored and cleanup errors were empty. This is a limited
preview observation, not a completed source-image or camera-uniform comparison.
After repeated loading-time headroom drops, the private next-case preflight now
requires at least8GiB of free host commit in addition to the unchanged standard
headroom checks. The existing running-game memory policy is unchanged.

The offline source join inspected179 of181 original Ubisoft graphics libraries
and mapped all64 retained unknown program identities to source-derived SM2/3
program hashes/sizes/stages, from4711 program records. The two remaining
libraries contain no verified supported D3D9 envelopes. The old unknown list
overflowed, so this covers those64 retained identities only. Source aliases are
retained; a matching compiled program does not uniquely identify a material,
target or causing draw. A prepared507-entry private observer catalog appends
those64 identities while preserving the original443 entry bytes and IDs.
It contains scalar identities only, enables no material replacements and has
not yet been used in a qualified actual-game trace.
