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
The shadow kernel/cascade blend and distant edge hardness were not changed;
vegetation sharpness and all-scene temporal stability remain unqualified.

## Verification boundaries

Private evidence is in `work/shadow-dof-20261108`. Tests used the previously
guarded native lab copy with an isolated profile suffix, fixed camera fixture
and unchanged lab adapter. They are actual native-game observations, distinct
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
