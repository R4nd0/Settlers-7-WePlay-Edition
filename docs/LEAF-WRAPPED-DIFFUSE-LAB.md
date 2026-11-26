# One fixed direct-light Leaf PS candidate

This opt-in profile changes the original LeafCard pixel shader's direct diffuse
response, rather than another ambient strength adjustment. The original
normal-mapped Lambert term `saturate(dot(N,L))` becomes
`saturate((dot(N,L)+0.5)/1.5)`. Dot1 retains1; dot0 changes0→1/3 and dot0.5
changes0.5→2/3. Turned-away values below−0.5 stay0. This is a bounded wrapped
diffuse approximation for thin leaves, not a complete subsurface/PBR model.
It can brighten or flatten crowns; larger change is not automatic quality.

The exact original PS1328 diffuse DP3 at offset968 feeds diffuse scalar c76
then sun RGB c6. An original paired observation measured c76=1 and positive
sun RGB, so the path has real inputs. Per-pixel normal distribution and its
relative contribution are still unmeasured. Only that diffuse DP3 loses its
saturation before a new saturated MAD; wrapping an already clipped dot would
incorrectly light every backface. The separate specular dot and all ambient,
alpha, texture, season colour, shadow and fog calculations remain original.
The original VS is kept bound: no Hemi stack or geometry/wind/LOD modification.

Recipient generation reserves all CTAB/local float constants and refuses
unsupported relative/control/matrix programs. A proven-unused c223 receives
the fixed2/3 and1/3 coefficients. One local DEF and one pixel MAD add44 bytes,
zero texture fetches and zero passes. All three pinned High vendor resources
produce PS1372 SHA256
`284d5c84ee6700039ee818fe7f063b2b90536beac45e261a5b3e0cf8a6d1746a`.
Only the authored transform and pins are public; original/generated binaries
stay private or in the recipient's immutable LocalData cache.

## Explicit V3 transport and lifecycle

ConfigV3 is a flat7204-byte/version3 allocation: the historical6180-byte
prefix followed by a dedicated Leaf PS path. Old LeafVS/Terrain path fields
must be empty. This isolated profile accepts only Leaf intent; initially-off
flags0 preload from independently declared Leaf intent4. The exact new folder
is `leaf-wrapped-diffuse-v4`; old V1/V2/Hemi/Macro recipes and buffers are not
reinterpreted. The guarded dispatcher reads8 bytes first, then exactly the
known size/version. SamePinnedConfig compares all bytes except enable flags.

The complete worker payload must remain within8192 bytes. V3 leaves at most
988 bytes for every measured DLL/renderer/export/header/padding prefix byte;
the actual laboratory example392+7204=7596 fits. The stager rejects budget or
capability errors before filesystem work and checks all aliases, paths and
existing cache files before any write. It requires explicit config version3
and a trusted host capability. It never activates a runtime itself.

The material backend reuses Leaf slot0 with an explicit PS stage pin and owns
its generated pixel object separately. Only the PS is temporarily bound;
original VS/PS refs, readback, partial-setter rollback, unsafe recovery and
before-every-external-Release/Reset cleanup remain in force. No new module or
fifth draw scope is introduced. Counters label recipe4 and config3; historical
mode metadata remains unchanged. Warm reuse must be measured after capture/
readback cleanup, since that lifecycle boundary deliberately releases device
programs. An explicit uncaptured priming draw establishes the new warm epoch.

## Verification and outcome boundary

The public source generator/stager has seven focused authored-fixture checks;
three actual owned resource aliases pass the system disassembler. The actual
7204-byte recipient configuration passed host preflight, production module
load and disable/enable/close without identity overrides or a D3D device.
Historical V1/V2/Hemi1492 and Macro107 CPU regressions pass; the size-aware
wire has69 guard-page checks. These are technical checks, not image quality.

Required actual scene: the ordinary castle view with foreground and lateral
sun-facing tree groups, original/on/original controls, then wind and slow pan.
Include bark/open ground negatives. Root alone reserves GPU/game tests.
The authored V3 GPU fixture passed actual pixel replacement, public OFF/ON/OFF,
same-frame capture, failed-draw restoration, reset recreation and uncaptured
warm reuse. Its twelve substitutions/restorations are synthetic GPU evidence.

The actual Steam castle trial retained OFF/ON/OFF captures at frames11301,
11310 and11324 with identical measured camera/light vectors and fixed
3840×2160→1920×1080 rendering. Recipe4 changed only the Leaf pixel shader;
143 substitutions and143 restorations were established across the phase, not
as per-image counters. Unmodified full views and five crop triplets show
lighter leaf sides and clearer formerly dark groups. The same change can
flatten crowns, so visible acceptance remains open. The original/control
images retain wind differences. Comparison files remain private under
`outputs/Blattlichtvergleich-20261126/`; no game images are committed.

Performance, all-edition coverage, final lifecycle and package qualification
remain separate. No visual release is established by this document.

L50 subsequently ended with C0000005, without a forced stop. The operator
exit route is unconfirmed; no clean menu/key exit is claimed. Memory pressure
stayed0 and original files, profiles and options were preserved/restored with
no cleanup errors. The previously verified new manual save67 was retained.
The user's instruction to continue work is not explicit optical acceptance;
all-edition, motion, performance and full release qualification remain open.
