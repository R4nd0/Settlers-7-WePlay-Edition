# UI rotation center correction, 2026-11-26

The paired black road streaks and displaced/missing minimap crosshairs at
200% share one verified cause: the original GUI vertex format stores rotation
centers in signed 16-bit tenths of a pixel. Centers past3276.7 source pixels
wrap. At3840×2160, rotating minimap elements then rotate around the wrapped
center and appear in the world image. They already exist before downsampling.

## Actual game evidence

Ubisoft History, Waldreich, the same saved castle/stone-bridge camera:

| Test | Observed result |
| --- | --- |
| 100%,1920×1080 source/output | Successful source capture without the paired streak; matching raw eye and light against200%. Expected resolution-dependent projection half-texel terms differ. |
| 200%,3840×2160 source to1920×1080 | Streak present in original high-resolution source. |
| Single-frame draw attribution | Frame8258,8689 successful draws, no failure/overflow. ROI is clear through draw8675; UI DrawPrimitive8676 introduces the paired streak. |
| Fix OFF/ON/OFF/ON | Frames2950/3008/3070/3130. Streak present/absent/present/absent; minimap crosshairs and connections missing/correct/missing/correct. |
| Interaction | Minimap click moved the measured camera; corrected UI remained visible. Minimize/restore retained successful filtered output without fallback. |
| Fresh default-enabled start | Frame3731: successful3840×2160 source and1920×1080 output; corrected minimap and clear road,19 UI applications/restorations, zero faults. |

All four controlled captures have exactly matching measured original River
projection, eye, sun direction and specular values. Clear Bank/index1 remains
120 applications/restorations and Leaf B/index1 remains13/13 in each frame.
Their accepted programs are unchanged. Terrain, experimental shadow and
ambient effects remain off. The user also confirmed100% working.

Both the controlled reversal run and final default-enabled200% run exited
normally with code0. Shared profiles and game binaries were unchanged, private
options restored, cleanup errors empty. Memory observations remained honest;
they neither gate nor stop diagnostic games under the user's current policy.
The last extra100% agent control was cancelled after the user's confirmation;
it is not additional world-capture evidence.

## Narrow implementation

`native/ui_rotation_fix.cpp` admits only the exact original484-byte GUI VS and
1108-byte PS, their semantic vertex declaration/28-byte stride, valid buffer
range, full matching A8R8G8B8 target/viewport and finite GUI projection.
It is automatically enabled only for source widths3277–3840 and heights up to
2160; ordinary1920×1080 bypasses it.

The recipient's original VS is transformed in memory. A negative wrapped X
center is unwrapped only when the actual vertex-to-center gap exceeds3276.7
pixels and rotation is nonidentity. The normalized integer wrap period is
65536/32767. Y, nonwrapped centers, identity rotations and original pixel shader
remain unchanged. No original shader bodies are bundled or committed.

The original DrawPrimitive executes exactly once. The original VS is restored
and read back before the observer, including failed draws and exceptions.
Unconfirmed restoration retains the original reference and blocks rendering
until safe recovery. Reset/release/teardown release generated resources before
forwarding. Original executable/callsite/ABI guards remain intact.

Source set is `effects-v16`; public initialization/material ABIs and compiled
capabilities remain unchanged. A12-byte/version3 observation subrequest permits
explicit diagnostic OFF/ON comparisons only with Observe intent.

## Validation and handoff

- 128 strict CPU/COM checks: wrong identities/layout, poisoned failures,
  render thread, restoration retry/unsafe state and reference balance.
- Recipient-original GPU test with authored vertices: reproduces displacement
  from[3600,200] to[3536,1114], corrects it, and proves bit-identical nonwrapped,
  negative ordinary-center and identity-rotation images;7 applications/7 restores,
  real Reset and recreation.
- Integrated authored combined GPU fixture passes with final build, including
  original state restoration, material pixels, selected UI state and Reset.
- 383 host,75 UP forwarding and20 SDK CPU checks pass;59 focused Python tests pass.

Actual correction is verified on Ubisoft History with DXVK. Other editions,
renderers, performance and final installer qualification are not claimed here.
Release packaging and publication remain with the separate release checkout.
Scalar identities and evidence are in
[the receipt](receipts/ui-rotation-center-fix-20261126.json).
