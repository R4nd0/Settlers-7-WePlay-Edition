# Shadow flicker and depth-of-field correction

Unreleased source candidate, verified locally on 2026-11-08. Published RC3.22
assets and the installed launcher remain unchanged. The earlier local SMAA
preview has not been rebuilt with this archive correction.

## Depth of field

The contradiction was real, but the checkbox alone did not establish an active
blur pass. Before the correction, the native active DOF option was **0**, while
the separate pending/write option was **1**. The menu reads the latter. Reopening
the graphics menu itself did not change a deliberately established 0/0 state.
Reapplying the same shader-quality value also did not reproduce the overwrite.

The six stage-camera transitions call `GFX.ToggleDepthOfField` unconditionally
to enable or disable their view. In the checked HE implementation, this call
writes the pending graphics choice; its active-value update is inhibited by
the DOF option's restart flag. A camera request to enable DOF can therefore
replace the user's disabled choice without enabling the current map's option.

The authored `camera_preferences.lua` wrapper respects the current pending
choice when a camera requests DOF. It restores that choice after a temporary
camera disable, using the existing pending-only setter. It reads the choice on
every call, so a subsequent user change remains authoritative. Camera focus
distances and the rest of the graphics settings are retained.

The archive transformation retains the two existing exact input fingerprints
and requires all six known call anchors. A repeat build from the retained,
verified archive stages is deterministic. Only `Script/local/stage/camera.lua`
differs from the published generated archive's member contents. The helper is
required in new packages and the generated archive fingerprint is updated to
`2fbf4a98a89ac735b3930995a1ff92331eb4d02958e46bf580458d0f4c899f68`.
Matching updated resources remain required on every peer.

Actual-game checks after the correction show 0/0 before loading, after loading,
and in the running scene. The graphics checkbox is off. A second fresh start
with DOF deliberately enabled shows 1/1 and retains the pending 1 across default
and long-range camera transitions. A subsequent pending disable remains 0 when
the camera requests its default view. These are native option observations,
not identification of every GPU pass or proof that DOF caused vegetation blur.

## Shadow sampling

The longer-model-detail option previously also forced global DXVK settings
`samplerLodBias=-1.0` and `clampNegativeLodBias=False`. That affected sampling
independently of the native model distance changes. The source now omits both
overrides. Extended geometry/culling, shadow map resolution, cascade/range
choices, optional AF16, Strict float emulation and the frame cap retain their
existing behavior.

Controlled native captures used the same late save, fixed 1920x1080 camera,
high native quality, Ultra/range flags, AF16 and **only SMAA** as a ReShade effect.
A private, pinned full-add-on runtime provided bounded GPU readback; production
SMAA still uses the standard runtime. No GI, authored tone, contact, haze or heat
effect was enabled. A guarded read of the real sun timeline selected the same
approximately 240–246 degree phase interval. The simulation continued running.

For the 145x140 castle-wall region at (930,450), restricting both series to their
overlapping phase interval gives:

| Measurement | Earlier mip overrides | Native/default mip sampling |
| --- | ---: | ---: |
| Samples | 38 | 33 |
| Mean absolute pixel-channel-average step, 0–255 | 9.485 | 1.036 |
| Pixels with at least four >5-level direction reversals | 14,468 | 1,287 |
| Example wall pixel (1071,577), min–max | 60.333–92.000 | 67.667–68.000 |
| That pixel's >5-level reversals | 14 | 0 |

This is a substantial reduction in the measured wall instability, not a claim
that every changing pixel is shadow flicker. Sampling intervals and native sun
angles are recorded; frame counts differ. GPU readback stalls make these runs
unsuitable for FPS or pacing claims. Remaining animation, native SSAO and other
native passes are not separately masked in the region metric.

Earlier controls included shadows off, 4096 maps, global sampler overrides off,
SSAO off, a stopped simulation, and a separate static-light fixture. The stopped
simulation also stops other animation and cannot isolate the sun by itself.
The static-light fixture used a different lighting direction and cannot qualify
a matching-phase improvement. Those exploratory runs do not establish that
Mittsommer's moving sun or the 8192 allocation is intrinsically defective.
At that milestone the shadow kernel/cascade blend and distant edge hardness
were not changed. Vegetation sharpness and all-scene temporal stability remain
unqualified.

## Distant shadow filtering follow-up

The larger shadow maps retain the native filter radii but reduce the sampling
step to 1/4096 or 1/8192. With the same cascade projection, this narrows the
filter footprint relative to the original high-quality 2048 map. Increasing
the radius itself also changes the shader's noise-rotation frequency. That
broader change was tested privately and is not included.

`native/shadow_filter.inc` instead substitutes a constant 1/2048 vector only
for the two distant cascade indices, 2 and 3. Cascades 0 and 1 retain the
original pointer. The substitution requires the live shadow-manager owner,
an actual 4096/8192 map, and all four original inverse components equal to
that map's exact reciprocal. Any other input retains the native argument.
The shadow-manager fields themselves are never changed. Map allocation,
cascade splits/blends, kernel radii/noise frequency, sample count and texture
sampling remain intact. This is a distant-filter correction, not temporal
antialiasing or a vegetation texture filter.

The hook reproduces the original argument push and pointer load, preserving
the remaining registers, flags, stack layout and FPU/SSE state. The existing
transaction applies it only with Detailed or Ultra shadows. Exact original
instructions, the preceding cascade-index load, the subsequent shader call,
and separate Ubisoft/Steam mappings are checked before installation. The
existing complete executable/PE checks and rollback remain required.

Actual Ubisoft lab runs confirmed the hook with both 4096 and 8192 allocations;
the native inverse vectors, original radius table and configured splits stayed
unchanged. The final 8192 comparison used the same save, camera, SMAA-only
effects and approximately 240.115–246.090 degree overlapping sun interval.
Distant shadow transitions are softened in the GPU captures. This does **not**
establish a general flicker cure: the final-frame measurements still include
moving vegetation, units, flags and native lighting. Readback stalls and unequal
sample counts also prevent pacing/performance claims.

A repeated early control exposed an additional confound: native mouse hover
can highlight the castle and open a tooltip in the measurement area. Therefore
the private fixture now disables world hover/tooltip updates before preparing
the scene. Earlier radius/all-cascade trials and hover-contaminated runs are
not used as improvement percentages. In the corrected comparison, mean
absolute intensity steps were 0.772 versus 0.754 in the wall region and
2.165 versus 2.139 in the distant region, with 38 versus 40 samples. Reversal
counts did not show a consistent improvement. These small differences are
observations, not evidence that the remaining flicker is fixed.

All eight game runs exited normally and their restoration checks passed. One
discarded radius experiment recorded a failed memory monitor; it is excluded
from qualification. The final control and distant-filter candidates retained
healthy monitoring. No original/shared save or option file was changed.

The unreleased production adapter fingerprint is
`be3954590e196e1f205db71af8f81a2ed717083e1fe3045e6e29f16d18594ffe`,
built with `-GameDirectory -NeoLogistics -ResumeCatchup -MaximumPhysicalCores 2`.
Launcher and installer pins match it. The selector's compiled instructions
match the actual-game lab candidate after normalizing address relocations;
all 84 loader exports remain present. No native binary is committed.

Validation: 3,744 shadow argument/ABI checks, 149 visual patch checks, 2,003
Steam mapping checks, 2,466 bootstrap checks, 52 directory-bridge checks and
89 Python launcher/installer/package tests passed. Synthetic Steam mapping
checks do not establish Steam gameplay qualification. The source candidate
and private evidence are retained; public RC3.22 and the user's existing
SMAA preview/normal launcher have not been updated by this follow-up.

## Verification boundaries

Private evidence is in `work/shadow-dof-20261108` and
`work/shadow-filter-20261108`. DOF/mip tests used the prior lab adapter; filter
tests used separate candidate builds of the same guarded native lab copy with
an isolated profile suffix and fixed camera fixture. These are native-game observations, distinct
from the Lua and installer fixtures. The production installation was not
patched in place. Shared Documents options/saves were resolved and backed up
before each launch; isolated profile bytes/timestamps and deployed game files
were restored, and shared
save/option hashes stayed unchanged. This does not undo the earlier autosave
incident documented in the SMAA-only review.

Lua 5.1 tests cover disabled/enabled choices, temporary disables, pending user
changes and live/deferred native semantics. Graphics tests check that extended
model detail no longer forces mip sampling, while selection, environment and
integrity guards remain intact. Installer tests cover helper completeness and
the resource/update path. No new multiplayer, Steam gameplay, native Linux or
other-GPU qualification follows from these local checks.
