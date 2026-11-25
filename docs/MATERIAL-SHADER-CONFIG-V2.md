# Proposed material configuration V2

This is an integration contract for the offline leaf-lobe and terrain-distance
candidates. It does not advertise a supported production feature or change the
running V1 adapter. Actual visual qualification remains outstanding.

## Binary layout

Keep the existing V1 layout and its 4132-byte allocation intact. A separate V2
structure has size 6180 and version 2. Its first 4132 bytes have the existing
field order, followed by two fixed 512-code-unit UTF-16 arrays:

| Offset | Bytes | Field |
| --- | --- | --- |
| 0 | 20 | size, version, flags, render width, render height; five uint32 |
| 20 | 16 | River reflection/density and terrain detail/relief scales; four float32 |
| 36 | 1024 | leaf vertex path |
| 1060 | 1024 | bump pixel path |
| 2084 | 1024 | parallax pixel path |
| 3108 | 1024 | private report path |
| 4132 | 1024 | bump vertex path |
| 5156 | 1024 | parallax vertex path |

The host's existing 8192-byte bound accommodates V2. Read and validate the size
and version first, then copy exactly the recognized structure size. Enlarging
V1 and copying 6180 bytes from a caller's 4132-byte allocation is invalid. Use
separate V1/V2 initialization entry points or an explicitly sized normalization
step; retain compile-time size/offset assertions for both layouts.

## Explicit recipe selection

V1 retains its current generated-program pins. V2 selects the distinct leaf
second-power lobe and terrain near-distance recipe set. It must never infer a
recipe from a filename or silently accept arbitrary bytecode. The flags retain
River=1, Terrain=2 and Leaf=4; no diagnostic timer pin is a production flag.

The bounded initial terrain candidate uses full detail through 8000 and zero
detail at 12800 world units. Its far-layer visibility constants remain original.
Changing these candidate distances changes the generated shader hash and needs
an explicitly supported recipe revision rather than an unbounded runtime knob.

| V2 program | Bytes | Generated SHA256 |
| --- | --- | --- |
| Leaf VS | 3828 | `119352b9def2ba290c8a5bc99b18b77cf4730461e97d0fbf677fba104e1e472a` |
| Bump VS | 2004 | `46a8dd89646f84cb761f832f47739f3d0c4fe5494688b3104b56c43fac8d3f07` |
| Parallax VS | 2004 | `d985366573816225b04473b762fd6f899241849185274347f513c1f4cf7de6c6` |
| Bump PS | 1836 | `c2ae22a6739e071774edd071870e6dc8fd058b5bf6eec6490bc1a4603fea2fa3` |
| Parallax PS | 3596 | `d0d082bbb9b8d2de1388b790165bcebc5fda571d88895db3e8b36004bf2e52fa` |

Generate these programs only from the recipient's verified original resources
and exact original VS/PS pairs. Ship authored transforms and compatibility
metadata, never original or derived game programs. Existing offline candidate
helpers are excluded from production staging; explicit V2 recipe integration
must precede a launcher capability or ordinary setting.

Enabled Terrain requires all four terrain paths; disabled Terrain requires
those paths empty. Leaf similarly requires only its declared leaf path. The
host must retain private-root/ancestor/reparse checks, regular immutable files,
exact program hashes/stages and private report confinement. The component still
checks absolute paths, bounded dimensions/scales and exact file contents.

## Draw and lifecycle contract

Classify the original VS/PS pair before either replacement is bound. V2 terrain
temporarily binds both its generated VS and PS. Capture all original shaders
and constant backups before mutation, restore both shaders and every touched
constant after successful, failed or throwing draws, and verify restoration.
Preserve the existing unsafe-state retry and fail-closed host behavior. The
original-program observer runs only after restoration. The offline transforms
do not change positions, fog, UV calculations, texture mip selection or the
separate far-layer visibility fade.

The retained generated-program cache grows from three to at most five objects.
Keep per-frame original identity refs separate. Release both classes before
every external device Release, actual Reset, disable, reconfiguration, detach
and Shutdown under the serial host lock/TLS contract. Keep current/peak retained
counters visible. Original and generated shader refs must never outlive the
caller's guaranteed live device at these cleanup boundaries.

Subset/off A/B switches keep the same pinned recipe configuration. Switching
V1/V2 or changing paths/scales needs complete shutdown/reinitialization at the
queued render-thread safe boundary, after the just-rendered frame and capture.

## Qualification required before activation

Extend CPU and authored real-GPU fixtures to cover both terrain shader stages,
all partial setter failures, restoration retries, failed original draws,
eight warm frames, Reset, disable/re-enable and both configuration versions.
Verify legacy 4132-byte callers without reading trailing memory.

Then use actual owned-game views with matched original camera/light uniforms:
a backlit leaf view with positive angular input, and ground where the extended
near-detail weight is positive. Compare off/on/off images and residual motion,
inspect artifacts and measure pacing separately from capture stalls. A changed
pixel count, successful disassembly or positive role application counter alone
does not establish improved graphics.
