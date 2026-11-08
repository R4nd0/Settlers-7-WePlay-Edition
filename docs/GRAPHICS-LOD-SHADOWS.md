# Longer settler/tree visibility and dynamic shadows

Unreleased source change, verified locally on 2026-11-07. The published RC3.18
downloads have not been replaced. The active user game was not modified.

The later [visual-quality controls](VISUAL-QUALITY.md) add a separate Ultra
shadow option with an 8192 high setting and a near-camera cascade distribution.
The original optional 4096 setting and measurements below remain available.

The launcher has three independent, optional settings under Graphics:

- **Keep high detail longer when zooming out** extends model LOD distances by
  approximately 50%, including settlers and SpeedTree. It also extends the
  distance-based model culling limit. The current source candidate keeps native
  mip selection and clamping. The earlier global -1.0 mip override was removed
  after the [shadow/DOF investigation](SHADOW-DOF-REVIEW.md).
- **Higher-resolution dynamic shadows** doubles the shadow map edge length:
  1024/2048/4096 for low/medium/high native shadow quality, instead of
  512/1024/2048. High therefore has four times as many texels.
- **Show shadows farther away (+50% range)** extends the detailed cascade end
  from 12800 to 19200 and the far end from 51200 to 76800 engine units. The near
  plane stays at 100; intermediate splits are recomputed coherently. Enable
  longer detail retention as well to retain detailed distant tree geometry.

All apply on the next game launch and remain disabled for legacy and fresh
preferences. Neither quality preset silently enables them. Native D3D9 supports
the model and shadow changes; the texture bias is specific to Modern graphics.
The existing 60 Hz DXVK cap, Strict water correction, AF selection and game
quality levels remain independent. Higher GPU cost and texture shimmering are
possible; turning the options off restores the original next-launch behavior.

Shadow resolution and shadow range are separate. A wider range spreads each
shadow map over more ground, so the independent higher-resolution option is
useful together with it. Tree wind continues through the original wind update;
there is no separate animation-distance override. Distant trees can still
become billboards, and fog, visibility, occlusion and the camera frustum still
apply. These options do not force every tree or hidden settler to draw.

## Native implementation and integrity

With all three options enabled, the implementation changes seven guarded operands in process memory,
after the existing executable SHA256/PE checks. It does not rewrite any EXE or
BBA. The earlier private Models.xml distance experiment is not shipped.

The original resolution-dependent LOD setter still runs. Its one multiplication
operand uses a private constant 1/200 instead of the original 1/300. The resulting
scale feeds the ordinary model and SpeedTree squared-distance calculations.
Native object quality and output resolution continue to participate. The
multiplier instruction and relocated operand are checked independently.

The character renderer uses that same model culling/LOD selector. Read-only
runtime tracing followed real settler graphics handles through their character
resources to the actual LOD node, rather than merely counting simulation
entities. The large-object size exemption and category visibility masks are
not distance settings and are left unchanged.

The shadow initializer uses a base dimension of 4096 instead of 2048 before
the original quality shift. The native resource estimator gets the same change.
The range option changes the original split span/ratio (12700/128 to 19100/192)
and the two terminal distances. It retains the original logarithmic/linear
blend of 0.87 and five shadow volumes. Other light-space depth bounds are left
unchanged. Instruction prefixes and relocated operands have separate guards.
The existing patch transaction validates all sites before writing and restores
them on a failed installation. Protections and instruction-cache flushing follow
the existing writer. Both Ubisoft and Steam have separately mapped addresses
and original-byte guards. No simulation/FIFO/priority/save data is changed.

The initial milestone enabled these settings for the pinned production adapter:
`a5c5cde6098a100974c4dd565d8a7f986d9819871bc398c04f2e912d1b2d97b5`.
The later Ultra-shadow milestone advanced the launcher/installer pin to
`73c221bc124ada7cc86cc95c47da9ef7f1e780fb12b6299683e7f70fbce87ab6`.
The unreleased distant-filter correction advances the current source pin to
`be3954590e196e1f205db71af8f81a2ed717083e1fe3045e6e29f16d18594ffe`.
It retains the 2048-map sampling step for the two distant cascades when the
actual allocation is 4096/8192, preserving native map fields and kernel/noise
parameters. See [actual-game checks and remaining flicker limits](SHADOW-DOF-REVIEW.md#distant-shadow-filtering-follow-up).
Unknown adapters cannot silently accept enabled preferences. The launch helper
strips inherited aliases and supplies only validated, fixed child-only flags.
No global environment variables or shared game content identity are changed.

DXVK bias behavior is checked against the pinned upstream
[3.1.1 configuration](https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf) and
[D3D9 option parser](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/d3d9/d3d9_options.cpp).
It chooses finer existing mip levels; it cannot create missing texture detail.

## Observed game tests

Private evidence: `work/graphics-lod-20261106`. Fixed Grassland Mesa camera,
1280×720, highest native quality, depth of field off, AF16, RX 9070 XT, two
physical cores. Camera translation/look-at and quality were identical.

| Read-only observation | Original options | Both options enabled |
| --- | ---: | ---: |
| Native LOD scale | 0.9992601 | 1.4988899 |
| Actual shadow renderer map dimension | 2048 | 4096 |
| Active render-loop sample | 300 intervals / 5 s | 300 intervals / 5 s |
| Loop frequency | 59.985 Hz | 59.981 Hz |
| Interval p95 / maximum | 16.763 / 17.781 ms | 16.748 / 17.505 ms |
| Intervals over 50 ms / missed intervals | 0 / 0 | 0 / 0 |

These are guarded screen-update-loop counters, **not Vulkan presents or a GPU
benchmark**. Both samples were foreground for their entire measured duration.
The short capped scene demonstrates operation without an observed stall; it
does not establish unchanged GPU cost or long-session performance. CPU use was
about 0.772 versus 0.825 cores. Startup-inclusive private-memory peaks were
1795 versus 1778 MiB; those are not steady-state or VRAM measurements. The
allocation-pressure monitors reported no pressure or observation failure.

A separate two-client loopback test loaded the existing late-game NEO save,
then ran turns 67349–67849. A used the new options, B the originals. Live reads
confirmed A's 1.4988899/4096 and B's 0.9992601/2048. Both clients exited normally;
all 451 structured replay CRC records matched, and both gzip streams completed.
This is one same-PC load/500-turn compatibility test, not multi-hour or remote-PC
qualification. No content-ID exclusions or CRC substitutions were introduced.

## Shadow-range, wind and settler follow-up

The follow-up used a farther fixed Grassland Mesa camera at 1280×720, the same
GPU and two-core limit, and a running simulation. The earlier frozen-clock
fixture cannot establish animation and was not used as animation evidence.
Actual renderer reads confirmed the following six split points:

| Split | Original range | Extended range |
| --- | ---: | ---: |
| Near | 100 | 100 |
| 1 | 718.3821 | 957.6006 |
| 2 | 1822.7930 | 2460.0073 |
| 3 | 4562.0029 | 6362.6538 |
| Detailed end | 12800 | 19200 |
| Far end | 51200 | 76800 |

Both range-comparison clients used longer model detail and 4096 shadow maps.
In each three-second live sample, all six native wind matrices and 16 sampled
leaf-angle values changed. The tree draw queue included branch/leaf geometry
with shadow-pass flags at the far camera. Together with the original shared
wind update and extended geometry LOD, this supports continued animation of
detailed distant trees. It is not a per-tree pixel-motion measurement: queue
reads are not frame-atomic, casters can sit outside a receiving cascade, and
these counts do not establish a precise shadow cutoff or that every billboard
animates. The verified range values above come from the actual shadow renderer.

A separate original-versus-extended detail comparison read real graphics
resources for 16 common settler handles. All 16 detail-transition distances
increased by 1.5. For five handles with unchanged model bounds, the distance
culling limits also increased by 1.5 (within floating-point rounding). Examples:

| Settler graphics resource | Original culling distance | Extended culling distance |
| --- | ---: | ---: |
| Category 21 / 65541 | 33868.04 | 50802.05 |
| Category 22 / 65536 | 31976.43 | 47964.63 |
| Category 23 / 65536 | 34424.04 | 51636.05 |

Distances are engine coordinate units, not meters. Handles that changed model
bounds as their tasks changed were excluded from the culling-ratio comparison.
This verifies the actual distance gates, not guaranteed on-screen visibility
through terrain, fog or other culling. No second settler-only multiplier was
needed; the existing longer-detail option already reaches their renderer.

Two five-second foreground screen-loop samples measured 59.981 Hz with the
original range and 59.381 Hz with extended range. The latter had one 73.429 ms
interval; the original had none over 50 ms. Both used live, independently
progressing simulations. This is a short operational check, not a GPU benchmark
or a claim of identical performance. Neither allocation monitor reported
pressure or observation failure.

The final range-enabled native candidate also passed a new loopback multiplayer
save load and 500 turns (67349–67849): A enabled all three options, B disabled
all three. Both exited normally, both replay gzip streams completed, and all
451 structured CRC records matched. Private evidence: `weavery-20261106/pair-08`;
the settler and shadow observations are under `graphics-lod-20261106`.

All isolated game files, profiles, timestamps and runtime configuration were
restored. The private test clients have ended. Native builds passed `/W4 /WX`;
the new suite exercises selection, invalid values, both ASLR layouts, protection
restoration, exact rollback and changed-opcode rejection on synthetic memory.
The launcher build and bilingual UI checks also pass. Actual Steam gameplay,
other GPUs, a moving-camera shimmer assessment and longer sessions remain
outside this local qualification.
