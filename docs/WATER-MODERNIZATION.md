# River optics implementation and qualification limits

The previous River constant changes produced only a subtle depth/colour
difference in actual25. They did not establish a visible modern-water gain.
`tools/material_shaders/water.py` implements the separate River pixel program A.
It now has an explicit native DLL integration and actual Steam scene evidence.
The user's subsequent choice of calmer water with a little more gloss produced
the separately pinned B recipe; see [the balanced variant](WATER-BALANCED.md).
These diagnostic results do not establish production or three-edition quality
qualification.

The intended visible result at normal gameplay zoom is clearer shallow edges
and submerged stones, a better separated deep channel, reflection that responds
to viewing angle, and broader, less harsh glints. The algorithm changes water
surface/depth calculations, with no global contrast, saturation or screen filter.

The exact original High River uses the existing cubemap for reflection. Its
compiled reflection weight stays within roughly 4–22 percent before the material
class multiplier. The planar reflection layer code in the private original
include is inactive. This candidate retains that cubemap; it does not add scene
reflections, SSR, new wave geometry, a new normal texture or fabricated depth.

| Calculation | Candidate | Purpose |
| --- | --- | --- |
| Reflection | `0.02 + 0.83 * (1 - saturate(NdotV))^5`, then the original class multiplier and saturation | A bounded Schlick response instead of the restricted cosine-squared range. |
| Existing optical depth tint `L` | `L * L * (3 - 2 * L)` | Preserve clear/opaque endpoints; reduce shallow tint and strengthen deep separation. The original fog/depth classes still supply `L`. |
| Existing specular powers | `256/96` becomes `96/32`; peak multiplied by `128/352` | Broaden the lobe while limiting its peak. The integral comparison is approximately energy-neutral when both original normal terms share the same dot, not a general physical-energy proof. |

For example, depth tint 0.2 becomes 0.104 and tint 0.8 becomes 0.896. Reflection at
normal incidence is 0.02 and approaches 0.85 at grazing before the original class
factor. These are equations and CPU checks, not observed visual qualification.
Colour tables, all 15 original texture reads, flow/UV inputs, depth tests,
atmospheric scattering operations and original colour/alpha output instructions
remain intact. Two unused shader-local constants and one unused temporary are
allocated after checking both instructions and CTAB declarations.

Generation requires the recipient's exact original ATI, ATIx or NV High Water
resource and the exact River VS/PS pair. Original VS, 2592 bytes, SHA256
`f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0`;
original PS, 3740 bytes, SHA256
`dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b`.
All three vendor resources generate the same 3840-byte candidate:
`8a27791a9907f73d4f42c847bc74a0a25b57da48b81f8e9cb566eca59bfcf0d1`.
Only the authored recipe, tests and compatibility hashes belong in the package.
The original or generated program must remain in recipient-owned private data.

Microsoft D3DDisassemble accepted all three generated aliases. The conservative
instruction estimate is 192 slots, below the minimum SM3 budget of 512; no texture
fetch is added. Authored CPU fixtures execute the emitted arithmetic across
view angles, depth/fog classes, reflectiveness limits and specular inputs. They
also check output/alpha preservation and refusal of ambiguous/mutated patterns.
The standalone authored GPU fixture subsequently verified the switching
mechanism, and actual Steam runs w34 and w36 rendered the recipient-generated
programs in S7. Their evidence and limits are recorded below.

The representative test uses the retained bridge-and-river scene at normal
gameplay zoom, with a continuous shallow bank, submerged rocks and the deep
channel visible together. A second normal-distance view should look along the
river toward the sun, so angle-dependent reflection and glints can be assessed.
Use identical render/output sizes in control and candidate. Off/on/off captures
must bind the actual River draw to the same camera, light and diagnostic UV
phase; reflected/refracted scene animation still needs its control repeat. The
1080p output itself must show the intended benefit without a magnified crop or
colour enhancement. Numerical differences alone do not constitute a pass.

Integration remains explicit and separate from the existing terrain/leaf
V2 configuration of 6180 bytes. The separate WaterProgram transport is 1076 bytes:
five DWORDs `size/version/render_width/render_height/enabled`, 512 UTF16 path
characters and 32 SHA256 bytes. It is implemented by
`native/water_shader_effects.h/.cpp`. Explicit A/B preparation uses
`tools/material_shaders/water_variants.py`; the original A-only staging helper
remains available for historical fixtures. The source-bound DLL exports
`WePlayNativeGraphicsConfigureWater`, which copies the fixed payload and queues
it for the owning render thread after the old frame's Present. The host must
classify the original VS/PS by exact hashes and byte counts before temporary
binding, verify the full render context, and restore the original PS before the
observer on success, failure, Reset and exit. Disable old River VS 68/69 scaling
for this test so two effects do not overlap. A compiled or configured water flag
does not establish actual successful replacement.

The native module accepts only the exact recipe/hash filename below the admitted
report directory's `MaterialShaders` or `LocalData/MaterialShaders` tree. It
rejects noncanonical/reparse paths, verifies the opened final file path, pins
the entire generated program and retains a deny-write lease. The integrating
host still supplies the existing exact executable, renderer, module, case and
build admission; this component is not an independent process injector.

`BeginDraw` must receive only nonzero geometry, with the host serial lock and
internal/TLS scope retained across the original call and `EndDraw`. The original
River pair is classified before the water PS is bound. The integration order is
`ComparisonBegin → TerrainLeafV2Begin → WaterBegin → original draw → WaterEnd →
TerrainLeafV2End → ComparisonEnd → original observer`. V2 flags 6 do not modify
River draws, so water sees the original River pair. On terrain/leaf draws, water
rejects the unrelated temporarily changed pair. Any failed restoration retains its original COM reference and
marks the transaction unsafe. Retry inside the same callback; no further draw
or observer may run while it remains unsafe.
The pending original PS reference belongs to module State, not the caller's
stack transaction. If that callback returns while unsafe, the opaque caller
token is never dereferenced later. `RetryPendingRestore(device)` is an explicit
owner-thread, host-serial/TLS-only recovery using that durable state; it refuses
a healthy active transaction and does not qualify the failed frame. Reset and
shutdown continue to refuse cleanup until the original binding is verified.

The positive identity cache is bounded and cleared every frame. Wrong hashes do
not consume its capacity; a positive overflow is still checked and accepted.
One generated PS survives warm frames. `Reset` must run before every external
device Release/Reset, and explicit shutdown before the original graphics
teardown, so the extra shader reference cannot prolong device ownership across
unload. Cleanup refuses an active or unrestored transaction. Ordinary Release
cleanup preserves completed scalar frame proof; actual Reset/device replacement
also calls `InvalidateFrame`. The host must call `EndFrame` on every owned
Present to bind water application/restoration counters to the same native frame.
Shutdown closes files/algorithm/COM refs and preserves scalar historical
counters with all live flags cleared.

The x86 module compiles with `/W4 /WX /fp:strict`. Its authored fake-COM fixture
tests cache saturation, warm reuse, partial/silent/throwing setters, creation and
query failures, original draw failure, retry, blocked unsafe draws, lifecycle,
thread ownership, source/program/path checks, reference balance and LastError /
x87 / SSE preservation. The production build without fixture identities also
accepted an actually recipient-generated staged program and the Python 1076-byte
transport in a CPU-only load/disable/shutdown test. No D3D device was created by
either CPU test; they do not establish GPU pixels or actual-game stability.

The separate `native/water_gpu_fixture.cpp` then passed a real owned GPU test
with pinned DXVK 3.1.1. Its rendered baseline/candidate shaders, geometry,
textures and constants are authored test content; fixture-only identity pins do
not exist in the production module build. Actual shader switching and restored
bindings, exact off/on/off pixel rollback, a warm one-program cache, a genuine
invalid-stride draw failure, target resize rejection, two device resets and
re-enable all passed. The rendered target was 800x600, with a 960-wide mismatch
test. This demonstrates the integration mechanism and authored optical
arithmetic, not the actual S7 scene or appearance.

The separately supplied genuine recipient-generated production PS was also
accepted by the real driver's `CreatePixelShader` and returned the same 3840-byte
SHA256 through `GetFunction`. It was deliberately never bound or drawn in that
fixture. No original S7 shader/scene data was copied into the test program.
The owned fixture exited 0, with peak private memory 237121536 bytes. Private
receipts and raw GPU BMPs are in `work/water-gpu-run-20261125-01`; readback times
are excluded from performance claims. The runner verifies current/snapshot/build
and executable hashes, pinned private DXVK copy, generated program hash, no
concurrent game, host headroom, a 45-second bound and a 1-GiB private-memory cap.

Actual Steam History evidence is limited to the retained normal-distance scene
at 3840x2160 internal resolution and 1920x1080 output. In w34, A's off/on/off
captures established its actual draw path and showed a calmer surface with more
legible shallow features. That case ended through the existing memory guardian
after a scan timeout; it did not establish a shader AV, clean exit or stability.
The original binaries, shared profiles and options were restored. A's fixed
recipe and program remain unchanged.

In w36, B's off/on/off captures 1/3/5 have the same measured River VP, eye,
lighting and diagnostic UV phase. Capture 3 reports variant 2, 120 successful
applications and 120 restorations in frame 17882, no shader/set/restore failures,
exceptions or pending original refs, and a successful filtered Present without
fallback. Legacy River scaling was off. A new save and regular original UI exit
completed with code 0; the admitted CDB watcher recorded zero real AV events and
zero dumps. Shared profiles, game binaries and private options were restored.
However, the run reached memory pressure level 2 and minimum host commit headroom
of 893 MiB. Whole-case qualification is false. The static technical proof applies
to this Steam case; it does not imply motion, pacing, long-session stability,
other-edition or release qualification.

The Ubi w39 case then exercised the same fixed B program with natural water
animation and the corrected n36 clipped-window startup. Its captures 2/3/4
(off), 6/7/8 (B) and 10/11/12 (off) span 21.482 seconds of receipt-completion
time. All nine have the same measured raw River VP/eye/light, comparison
requested/active/pinned zero and successful filtered output. Each enabled frame
reports 120 successful applications and 120 restorations in that exact frame
with no water fault, unsafe state or pending ref.

Lossless native-output crops show changing water patterns in both successive
B intervals. Left-water RGB differences average 3.31/3.56 levels, with
43.8%/47.3% of pixels changing above three levels, compared with roughly
8.60-9.33 mean differences in the original off intervals. This numerical change
supports non-frozen sampled animation; lower temporal difference is not itself
a quality or frame-pacing score. Inspection of the selected native crops shows
changing ripples with subdued speckle, with no obvious frozen rectangular patch.
Sparse readbacks cannot rule out between-sample shimmer or establish smooth
continuous motion. No further optical adjustment was made after the user's
acceptance of B.

`work/water-w39-analysis/motion.private.html` provides all nine output images
and lossless water crops with a step player/contact sheets. Playback uses
receipt-file completion gaps and labels each readback stall; these are not
exact GPU acquisition timestamps. It is explicitly sampled evidence, not a
continuous recording or benchmark. Original BMPs and case checkpoints remain
unchanged. Exit and memory conclusions require the final w39 owner result.

The bounded motion procedure used in w39 uses only the existing admitted public commands, with
normal original game speed and a stationary normal-distance camera. Omit
`--comparison-uv` on a fresh case so PS80 remains original. If comparison was
initialized, queue `comparison` with `enabled:0` first and verify requested,
active and pending are zero before collecting evidence. Use increasing command
sequences and await each command receipt and each completed native capture:

1. Queue `water` with `enabled:0`; collect three `capture` samples about one
   second apart.
2. Queue `water` with `enabled:1`; collect three further samples at the same
   interval.
3. Queue `water` with `enabled:0`; collect three further samples, leaving water
   off afterwards.

Bound the measurement segment to 30 seconds. If a command/capture does not
complete in that window, retain the partial evidence and end the measurement;
do not change the guardian or game speed. Command acceptance means queued, so
validate the completed frame's actual water state. Every enabled sample must
report variant 2, balanced positive application/restoration counts in the exact
captured frame, zero faults/unsafe state/pending refs and successful presentation
without fallback. Require matching raw VP/eye/light across samples; UV is
deliberately unpinned. Compare normal-output water regions between successive
frames for flowing ripples, crawling glints or frozen/repeating tiles, using
both original off spans as controls. This is sampled visual evidence; native
readback stalls exclude these intervals from pacing claims. The current frame
receipt does not record the unmodified PS80 value, so it establishes disabled
comparison, not a measured UV-phase delta. No new engine hook or simulation
change is needed for this first motion check.

Private CPU provenance is retained under
`work/water-modernization-20261125/candidate-receipt.private.json`.
Actual static evidence is retained in `work/w34`, `work/w36` and `work/c36`;
Ubi sampled-motion evidence is retained in `work/w39` and `work/water-w39-analysis`.
Immutable case checkpoints must not be changed. Continuous-motion assessment,
representative additional views, healthy pacing/memory runs and three-edition visible evidence remain
required before release qualification.

Shader arithmetic follows Microsoft's
[DP3](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dp3---ps),
[MUL](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/mul---ps),
and [SM2/3 modifiers](https://learn.microsoft.com/en-us/windows/win32/direct3dhlsl/dx9-graphics-reference-asm-ps-instructions-modifiers-ps-2-0)
documentation. The local original include/disassembly establishes the specific
River semantics; public documentation alone does not establish a game binding.
