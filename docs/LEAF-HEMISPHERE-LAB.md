# Leaf hemisphere contrast candidate

This explicit recipe targets normal front-lit LeafCard foliage. The original
High pixel shader already has directional sky illumination, so another RGB
multiplier would stack attenuation. This candidate remaps that existing
hemisphere response instead. The original ambient weight is
`.5+.5*saturate(.5*z+.5)`; the candidate is `.5+.5*saturate(z)`.
Both retain floor0.5 and upward top1. Horizontal cards change0.75→0.5,
and z0.5 changes0.875→0.75. The ratio remains between2/3 and1. Original
sky/sun RGB vectors, base/season colours and direct sunlight/specular normal
calculations remain unchanged. Actual final hue still depends on their mix;
this is no uniform tree tint or whole-image grade.

Source and typed bytecode identify the first wind-normalized, instance-rotated
world normal as TEXCOORD1. In the exact original LeafCard pixel shader this
input is declared only.z and has exactly one consumer: the saturated ambient
hemisphere MAD. It is not the later camera-facing normal used by specular.
The recipe inserts one MAD into the vertex program, immediately after its
original `mov o3.xyz,r4`, to write only `o3.z=2*r4.z-1`. It reads the existing
local2/-1 literals, changes no original byte and never overwrites r4. The
pixel shader, all other outputs, position/wind, silhouette, alpha and LOD
programs stay original. There is no added constant, temporary, texture fetch
or pixel instruction.

The recipient-generated VS is3852 bytes, SHA256
`ffb6d89661e9ae8a9d75ba3d322a318996d07d1dbb81281fefdb4b8df2f277f7`.
All three original High vendor aliases generate this same program. Original
and generated shader binaries remain private/recipient-local; only the
authored transform and compatibility metadata belong in the repository.

`staging_leaf_hemi.prepare` has the retained V2 staging signature and unchanged
6180-byte/version2 transport. It requires isolated Leaf intent: no Terrain or
legacy River mixture. Its new immutable folder is
`leaf-hemisphere-contrast-v3`. Historical Leaf V2 and Terrain Macro paths/pins
remain supported and untouched. Host and module both require the exact new
recipe path/hash, derive initially-off preload from declared Leaf intent, and
reject mixed sets. The report records `leaf_recipe_variant:3`; it does not
claim a terrain recipe for this mode. There is no new runtime module/header.

The expected scene is the normal castle foreground canopy: retained lit tops
with more readable lateral/underside groups. Actual geometry-normal
distribution and image coverage are still unmeasured. A source-dependent
signal is not proof that a particular crown contains enough orientation
variation. Closely spaced0/4/0 captures with an original repeat, then wind,
pan and unchanged card/billboard LOD-transition review are required. Stronger
contrast can reveal LOD colour differences despite unchanged LOD mechanics.
No visible gain or lack of shimmer is qualified by CPU checks.

Run `python -B -m unittest tools.material_shaders.test_leaf_hemi
tools.material_shaders.test_staging_leaf_hemi`. The native V2 CPU build covers
historical modes plus the new exact path, VS-only replacement, failed-original
and partial-setter rollback, query-output reference cleanup, warm one-object
cache and safe toggle. The contract fixture also checks initial OFF/Leaf-only
intent and exact full recipient/private paths. Actual recipient sources are
generated and admitted through both host and production module loaders before
any GPU test; private program acceptance is not visual qualification.

## Actual normal-view result

L47 recorded original/on/original frames150015/150024/150033, equal9/9-frame
spacing and matching captured camera/sun vectors. The phase established104
applications and104 restorations. The ON module report was older than its
image; these are phase counters, not a claim of per-image role counts.

The foreground canopy changed at most slightly darker/damped. Wind residuals
were comparable or larger, and no convincing normal-view canopy plasticity
gain was established. Actual geometry-normal distribution remains unmeasured;
the image result must not be used to invent a normal histogram. No visual or
performance release follows, and further subtle variants are frozen.

The owned run exited with C0000005 after regular Alt+F4, not a forced stop.
Memory pressure stayed0; original files, shared profiles and private options
were preserved/restored with no cleanup errors. This is not a clean-exit
qualification. The local canopy gallery retains lossless images, original
repeat controls and the explicit negative assessment.
