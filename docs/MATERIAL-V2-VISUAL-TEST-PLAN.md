# Terrain and vegetation V2: visible acceptance targets

These targets were defined before native V2 integration. Supersampling remains
the accepted fixed baseline. V2 has no actual visual pass yet; CPU and GPU state
tests establish implementation safety, not an aesthetic improvement.

## Terrain target

Use the original high-quality BumpLayer/ParallaxLayer material pairs on an open
grass/stone patch near the foreground, with the normal-detail texture visible
and a camera comparable to the observed eye height around 8500 world units.
Original normal detail fades from full at distance 2000 to zero at 3200. V2's
8000..12800 interval should make existing material normals affect local lighting
and make parallax relief visible from an ordinary game view. It preserves the
separate far layer's 15000..19200 visibility fade, fog, UVs and positions.

Expected visible gain: more local ground light/detail structure and restrained
surface relief, with coherent transitions while moving the original camera.
Reject sparkling, swimming patterns, obvious new seams, abrupt detail popping,
implausible relief or a pacing/memory regression. Changing colour alone does
not establish this target. Existing texture mip selection stays active.

Prefer an isolated Terrain off/on/off sequence (flags 0/2/0), with Water and Leaf
disabled for this comparison. Record the raw original camera/light uniforms and
exact source/output dimensions for each fresh world draw. Report PS c33 policy
values and both original/generated shader identities. A high-pass contrast scale
of 1.0 can be used for a diagnostic attribution pass; the VS distance and paired
normal PS remain explicit pinned recipes. Beyond 12800 the normal/parallax
weight should still be zero; contrast changes can separately affect colour.

## Vegetation target

Choose leaf cards viewed into the source-proven sun direction, so the saturated
world-view/sun dot is positive. Original backlighting uses its fourth power;
the V2 second-power lobe should broaden existing warm transmitted leaf light.
At a dot of 0.5, the angular term changes from 0.0625 to 0.25; at zero and one
the endpoints stay unchanged. No global colour gain is added.

Expected visible gain: more readable warm leaf interiors at intermediate
backlit angles, while front-lit/zero-dot leaves do not acquire an added glow.
Reject flat blanket brightness, excessive orange foliage, changed alpha edges,
shimmering or altered wind/geometry. Rotate using original camera controls and
record actual VS20 sun/VS16 eye and the original scene matrix. These identify
the test direction; they do not by themselves measure every vertex's angle.

Prefer an isolated Leaf off/on/off sequence (flags 0/4/0), then repeat a front-lit
control. Keep wind/animation residuals separate from the material difference.
Do not call a changed-pixel count a quality pass.

## Required implementation evidence

Before any game reservation, retain CPU and authored real-GPU checks for exact
V1/V2 ABI dispatch, known/unknown program pins, paired VS/PS rollback at every
partial failure, failed original draws, exceptions and unsafe restoration retry.
Verify five bounded retained programs across eight warm frames, followed by
complete external-Release/Reset/disable/Shutdown cleanup and lazy recreation.

The root coordinates all real GPU/game launches, source checkpoints, profile
backup/restoration and actual visual review. Keep original resources immutable
and generate replacement programs from each recipient's verified resources.
V2 terrain/leaf and any future WaterProgram remain separate explicit recipe
contracts. Their coexistence must preserve original-pair classification and
restore every temporary shader/constant before the original-program observer.
