# RC3.23: SMAA and Lobby Graphics

**Update every participant to RC3.23 and complete the offered game-copy update
before joining.** The multiplayer content identity changes; RC3.22 and older
copies cannot join RC3.23. Existing supported saves and explicit graphics
preferences are retained by the normal update workflow.

## Graphics and camera

- **Standalone SMAA:** edge smoothing can run without the other ReShade effects.
  The WePlay recommended preset now enables SMAA only, alongside native detail
  and 4096 shadows. Color, contact-shadow, GI and other maximum effects stay off
  in this preset. Separately selected effects remain available.
- **Depth of field:** camera transitions respect the current user choice and
  no longer replace a disabled pending choice with Enabled. The correction also
  preserves an enabled choice across temporary camera disables.
- **Vegetation/shadow sampling:** longer model visibility no longer silently
  forces a global negative mip bias. This avoids changing texture sampling just
  because farther geometry is enabled.
- **Distant shadow edges:** 4096/8192 maps use the original filter sampling
  footprint for the two distant cascades. Near cascades, native kernel/noise
  parameters and shadow resolution/range choices are preserved.

Remaining shadow flicker and all-scene vegetation sharpness are **not fully
resolved**. GPU readback comparisons are not FPS measurements. No complete
flicker cure or new performance gain is claimed.

## Multiplayer graphics

Joining a host no longer forces that host's renderer onto the guest. Automatic,
Compatible and Modern graphics remain local choices. Only the complete, exact
pinned DXVK profile is normalized in multiplayer identity; game executable,
native adapter, maps and archives still have to match. Full local file checks
remain active, including the renderer files.

Automatic native fallback clears stale GPU selection and DXVK-only effect
choices instead of failing again during launch. Explicit unsupported Modern
requests and unknown or changed renderer profiles still fail their checks.

## Install or update

Windows: run the new **Setup.exe**, then allow WePlay to update its separate
game copy. For a portable copy, choose its outer folder or **App** folder in
Setup. Linux: extract the new Linux package and follow the
[Linux guide](LINUX.md). All peers need matching RC3.23 game content, including
when NEO is off. Old release downloads are retained unchanged.

## Verification and scope

Includes source, native ABI, profile integrity, packaging and signed-update
checks. Native Windows checks use isolated, backed-up test copies. Linux
packaging/runtime checks do not add native Linux multiplayer, Linux Mint,
Steam/Proton or Vulkan gameplay qualification. Separate-PC Internet testing is
not implied by local loopback checks.

[Release verification](verification/rc323-20261108.json) ·
[SMAA findings](SMAA-ONLY-REVIEW.md) ·
[Shadow and DOF findings](SHADOW-DOF-REVIEW.md) ·
[Multiplayer content checks](NETWORK-CONTENT.md) ·
[Visual settings](VISUAL-QUALITY.md) ·
[Earlier graphics measurements](GRAPHICS-RC319-MEASUREMENTS.md) ·
[Earlier Internet join fix](RC3.21-INTERNET-JOIN.md)
