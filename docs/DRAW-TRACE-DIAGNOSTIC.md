# Bounded original draw tracing

The optional observer diagnostic has a separate 44-byte/version-2 request.
The existing 952-byte/version-1 observation contract, selected-sample layout
and public Stats layout are unchanged. This diagnostic is not a rendering fix
and is not enabled by the release preset or a launcher option.

An explicit request arms the next complete frame, beginning at the following
Present boundary. The observer records successful nonzero original draw calls
on the admitted device/render thread, including both UP methods. Ordinals count
these observed draws, rather than API calls rejected before observation.
The record contains bound VS/PS hashes and aliases, actual target identity,
format/dimensions, viewport and per-field availability. It remains independent
of the observer's truncated aggregate unknown/pair lists. A frame is limited to
32,768 rows; overflows and getter failures are explicit.

For an attribution experiment, a request can additionally read a small region
after at most eight consecutive draw ordinals. The original target must match
the requested dimensions, be non-multisampled, and have a supported 32-bit
format. GetRenderTargetData reads a same-size/same-format system-memory surface;
only the requested ROI, at most 256 by 256 pixels, is retained. Output consists
of raw original bytes and SHA256, without recoloring or sampling. A8R8G8B8 and
X8R8G8B8 bytes are BGRA/BGRX; R32F bytes are little-endian floats, not RGB.
A2R10G10B10 retains its original packed representation.

No shader, texture, constant, viewport, stream or render state is set. Original
draws are neither skipped nor replayed. COM references, locks, FP state and
LastError are preserved. Failed, unavailable and unreached readbacks are distinct
from successful black or zero pixels. Requests cannot replace an outstanding
trace, replay a sequence or overwrite a prior trace file. Shutdown cancels an
unfinished request with an explicit incomplete receipt.

The fixed helper uses the existing unfinished case, source fingerprints,
recorded parent/PID/creation identity, loaded DXVK/DLL pins and live memory guard.
It exposes no arbitrary PID, export, program or output-path selector:

```powershell
python -B -m tools.graphics_lab.draw_trace --case work/OWNED_CASE --dll work/EXACT_BUILD/WePlaySupersamplingLab.dll --sequence 1
python -B -m tools.graphics_lab.draw_trace --case work/OWNED_CASE --dll work/EXACT_BUILD/WePlaySupersamplingLab.dll --sequence 2 --first-draw 500 --draw-count 2 --target 3840 2160 --roi 2500 1000 256 256
```

Trace JSON and raw ROIs remain private under the case directory. Do not commit
game images, original bytecode or runtime object addresses.

## Interpretation limits

Program hashes describe the restored binding observed after a draw. An admitted
material transaction can have temporarily substituted a program during that draw;
its separate exact effect/capture proof remains required. A target address is
only a same-process diagnostic identity, not proof of lifetime across frames.
Draw traces do not cover Clear, StretchRect, texture uploads or unobserved
foreign calls. An image change between two snapshots therefore needs inspection
of the actual intervening operations before assigning a cause.

Readbacks deliberately stall the GPU. Their time is not FPS, GPU cost or normal
frame pacing. A complete trace means its frame completed; requested readbacks
must separately have zero failures, zero missing samples and valid hashes.
Same-scene 100/200-percent association must still record camera/light, original
LOD/projection differences and the unchanged accepted material selection.

Initial strict CPU/COM validation passed 1,684 checks, including exact ROI bytes
with padded pitch, failed readbacks, UP kinds, sequence/budget guards, missing
ordinals and cancellation. Integrated host admission/frame/teardown passed 383
checks, UP forwarding 75, SDK protocol 20. These synthetic checks do not identify
the reported black-streak generator or establish an actual-game correction.

The authored combined GPU fixture also passed under pinned DXVK 3.1.1. It read
two different 4×4 ROIs after two original draws while their scenes were still
open, checked every pixel against the expected authored colors, checked the
complete two-draw trace/zero-failure receipt, and retained the existing combined
shader substitution/restoration and Reset checks. This verifies the readback
mechanism on that driver, not actual-game streak attribution.
