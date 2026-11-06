# Longer detail retention and dynamic shadow resolution

Unreleased source change, verified locally on 2026-11-07. The published RC3.18
downloads have not been replaced. The active user game was not modified.

The launcher has two independent, optional settings under Graphics:

- **Keep high detail longer when zooming out** extends model LOD distances by
  approximately 50%, including SpeedTree. Modern graphics additionally applies
  DXVK's fixed `samplerLodBias = -1.0` and `clampNegativeLodBias = False`.
- **Higher-resolution dynamic shadows** doubles the shadow map edge length:
  1024/2048/4096 for low/medium/high native shadow quality, instead of
  512/1024/2048. High therefore has four times as many texels.

Both apply on the next game launch and remain disabled for legacy and fresh
preferences. Neither quality preset silently enables them. Native D3D9 supports
the model and shadow changes; the texture bias is specific to Modern graphics.
The existing 60 Hz DXVK cap, Strict water correction, AF selection and game
quality levels remain independent. Higher GPU cost and texture shimmering are
possible; turning the options off restores the original next-launch behavior.

Shadow resolution and shadow range are separate. This change does **not** add a
new shadow-distance or wind-animation-distance control. It does not establish
that every distant tree is animated or casts a detailed dynamic shadow. The
original wind, shadow frustum and culling rules remain in place. The user's
request for a separately extended shadow cutoff remains an investigation item;
no guessed projection/culling parameter is exposed as a working setting.

## Native implementation and integrity

The final implementation changes three guarded operands in process memory,
after the existing executable SHA256/PE checks. It does not rewrite any EXE or
BBA. The earlier private Models.xml distance experiment is not shipped.

The original resolution-dependent LOD setter still runs. Its one multiplication
operand uses a private constant 1/200 instead of the original 1/300. The resulting
scale feeds the ordinary model and SpeedTree squared-distance calculations.
Native object quality and output resolution continue to participate. The
multiplier instruction and relocated operand are checked independently.

The shadow initializer uses a base dimension of 4096 instead of 2048 before
the original quality shift. The native resource estimator gets the same change.
The existing patch transaction validates all sites before writing and restores
them on a failed installation. Protections and instruction-cache flushing follow
the existing writer. Both Ubisoft and Steam have separately mapped addresses
and original-byte guards. No simulation/FIFO/priority/save data is changed.

The GUI only enables these settings for the newly pinned production adapter:
`4488aa58c02387fdffdbdb7db906d836a24d7bc981d3eca66ed51e12e72d94ae`.
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

All isolated game files, profiles, timestamps and runtime configuration were
restored. The private test clients have ended. Native builds passed `/W4 /WX`;
the new suite exercises selection, invalid values, both ASLR layouts, protection
restoration, exact rollback and changed-opcode rejection on synthetic memory.
The launcher build and bilingual UI checks also pass. Actual Steam gameplay,
other GPUs, a moving-camera shimmer assessment and longer sessions remain
outside this local qualification.
