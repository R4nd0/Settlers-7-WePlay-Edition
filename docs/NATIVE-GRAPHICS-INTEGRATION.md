# Native graphics integration contract

This note records the native20 observer interface and the following `effects-v1`
candidate. The original fourteen-source checkpoint remains historical evidence;
the effects candidate has a separately bound nineteen-source build. Neither a
compiled flag nor the synthetic tests qualify a release package. The parent
launcher/runtime owns deployment and game tests.

## Fixed loader sequence

Keep the optional graphics DLL separate from the platform adapter. Preserve the
original executable, engine-module and DXVK identity checks and the existing
pre-entry, owned-process handshake. No arbitrary export/remote-call interface is
needed; the loader can allow only these named operations:

| Operation | Export | Input bytes | Requirement |
| --- | --- | ---: | --- |
| Initialize | `WePlaySupersamplingInitialize` | 1048 v1 / 3132 v2 | Existing initializer contract |
| Observe exact inputs | `WePlayNativeGraphicsConfigureObservation` | 952 | Optional, after OBSERVE initialization |
| Configure material effects | `WePlayNativeGraphicsConfigureEffects` | 4132 | Required before first draw when a material intent is selected |
| Capture a frame | `WePlayNativeGraphicsRequestCapture` | 12 | Existing queued owning-thread operation |

The later `effects-v2` diagnostic build additionally exports
`WePlayNativeGraphicsConfigureComparison` with a fixed 12-byte
`(size, version, enabled)` command. It requires OBSERVE and is excluded from the
production loader allowlist and user graphics choices. Its manifest records
`diagnostic_abi.comparison: 12` separately from compiled feature capabilities.
Historical native20 fourteen-source and `effects-v1` nineteen-source manifests
remain distinct from the new twenty-two-source checkpoint.

ConfigureEffects exists in `effects-v1`, not native20.
Use separate fixed worker invocations for initialization, observation and effects.
Their combined configurations alone total 8,216 bytes; combining them would
already exceed the current 8,192-byte RW allocation bound before paths/strings.
Each individual operation fits the existing bounded RW/RX worker model.

The loader must verify each return before resuming the original entry point.
A selected material with no successful ConfigureEffects call must not be reported
as active. Track requested and configured masks separately. In the next native
checkpoint an unconfigured material intent must block diagnostic drawing rather
than silently render the original and describe that as an effect.

ConfigureEffects returns acceptance of a queued configuration. Its application
runs on the owning render thread after EndFrame cleanup and after that frame's
capture/presentation. The current pixels therefore retain their original effect
revision. Runtime control must wait for `effects_pending: 0`,
`effects_configured: 1`, the intended `effects_active` role mask and an advanced
`effects_revision` before requesting a candidate capture. Capture requests while
a configuration is pending are rejected. `effects_requested`/`effects_active`
use effect-module role bits, not the native intent bits in the table below.

Configured revision alone is not evidence that those pixels were drawn. The
capture path waits for successful nonzero geometry latched to the same
`effects_drawn_revision` and `effects_drawn_active` values; a Present with no fresh
draw after a toggle cannot capture the old backbuffer as the new epoch. A safe
module failure deactivates effects, records failure counters/reason, cancels stale
pending captures and continues original rendering. Only unrestored GPU state
blocks drawing. Any subsequent fallback capture remains explicitly faulted and
cannot qualify the requested effect.

An explicit zero/subset mask is supported for same-process paused A/B. For an
otherwise identical pinned configuration, SetEnabled retains CPU program leases
and evidence counters. Changed artifacts or factors are shut down and initialized
at the same safe boundary. Flags never change in the middle of a draw or get
applied retroactively to an already captured frame. Normal non-OBSERVE reporting
is event-only; read-only diagnostic observation retains its periodic reports.

## Configuration and recipient artifacts

Native v2 flags and effect-module flags are different namespaces:

| Native intent | Native bit | Effects role | Effects bit |
| --- | ---: | --- | ---: |
| Observe | 1 | No material mutation | — |
| Water | 2 | River | 1 |
| Terrain | 4 | Bump/parallax | 2 |
| Vegetation | 8 | Leaf | 4 |
| Stable shadows | 16 | Separate implementation | — |

`supersampling_hook_effects_contract.h` binds the 4,132-byte configuration to the
native render dimensions and selected roles. It validates finite bounded factors,
private reports, recipient artifact presence, canonical paths, ancestor reparse
exclusions, report separation and confinement beneath the native report directory.
Generate/stage recipes in the next case or recipient LocalData tree before
configuration; never use the still-running case as an artifact staging target.

The public package should contain the native DLL, recipe/generation tools,
scalar compatibility metadata, licenses and qualification metadata. Original or
derived game shader programs, BBA archives, game binaries and raw captures stay
out of the repository and public package. Recipient-generated `.vs.bin`/`.ps.bin`
files belong only in the recipient's private cache and remain identity checked
by the effect module.

## Draw transaction

Retain the serial device lock and internal/TLS scope across the entire callback:

1. Classify the original VS/PS pair and validate the actual RT/viewport/context.
2. Begin the bounded effect transaction.
3. If Begin reports unsafe state, retry restoration immediately. If still
   unsafe, stop diagnostic rendering; do not issue a draw or observer callback.
4. Call the original draw exactly once.
5. End the transaction on original success, HRESULT failure or C++ exception.
6. Retry an unsafe End immediately. If still unsafe, stop rendering and do not
   run the original-program observer or label the path an original fallback.
7. Only after verified restoration may the original-program observer run.

`supersampling_hook_effects_draw.h` stages this ordering independently of the
live DLL. Its result distinguishes the actual original HRESULT from the safe
caller result. Transactions never escape the callback: shader pointers borrow
frame-cache references and must not survive Present/Release/Reset.

Clear effect and observer cache references before every external device Release,
Reset and teardown within TLS. EndFrame also clears the appropriate frame refs.
Keep the independently tested output-swapchain lifetime policy; ordinary temporary
device releases must not unnecessarily recreate the output swapchain.

## Capabilities and package qualification

Diagnostic comparison wraps the material transaction: comparison Begin, material
Begin, original draw, material restoration, comparison restoration, then the
original-program observer. It pins only the exact original River pair's finite
PS c80 float4 after a successful draw and restoration. The original register is
restored immediately; no game clock, simulation, RNG or asset is modified.
Comparison remains applied to the material-off control while enabled, and an
explicit disable clears the pin.

Captured comparison proof requires active/pinned/nonfaulted state, an exact
local/native Present frame match, and positive equal application/restoration
counts in that completed frame. A retained global pin by itself proves no
captured-frame modification. Ordinary cache cleanup retains copied frame proof;
actual Reset/attach or explicit reconfiguration invalidates it.

Distinguish three facts:

- A flag is declared in the ABI.
- Its implementation is compiled and explicitly configured.
- Its exact DLL/artifacts/settings/edition have actual runtime qualification.

The installer must not infer the third fact from the first two or from a small
GPU fixture. Keep the runtime package pin and per-edition availability separate
from the platform adapter pin. Unknown/unimplemented bits remain rejected. A
replacement DLL or recipient recipe hash invalidates its previous qualification.

Qualification should bind the exact DLL and renderer hashes, source build
manifest, original edition, settings, recipient artifact hashes and actual
receipts. Require observed feature application and restoration counters, zero
unrecovered unsafe states, real UI/world interaction and saving/loading, reset/
fallback behavior, and pacing/memory observations at the intended resolution.
Capture readback stalls are excluded from pacing. Before/after appearance is a
separate observed comparison, not a conclusion from a shader compiling.

For world-pose matching, `scene_uniforms` is available only from an eligible
successful original River draw in the exact captured GPU frame. It contains raw
VS c12–15 MainVP, VS c16.xyz eye, PS c5.xyz sun direction and PS c7.rgb specular
color with original shader/catalog hashes and actual render context. Missing or
stale River data remains `null`; the generic Vision main-context camera may be a
dummy and must not substitute for it. These uniforms do not establish physical
sun elevation, exposure or simulation time.

The next GPU fixture must create genuine vertex and pixel shaders, apply at least
one intended transaction, compare actual GPU state before/after both successful
and failed draws, and exercise restoration retry/failure handling. Fixture-only
compatibility overrides must never be compiled into the production DLL. Actual
game trials remain necessary for each enabled material/edition path.

The `effects-v1` synthetic fixture now creates authored SM3 vertex/pixel programs
on the pinned DXVK device. It observed changed river constant-driven GPU pixels,
changed leaf replacement-VS pixels, exact restoration of original constants and
shader objects, restoration after a controlled failed original draw, same-process
disable/re-enable, and re-enable after Reset. Fixture-only program identity
overrides are excluded from the DLL build. Those observations establish the GPU
transaction machinery, not the appearance or safety of an original S7 material.
