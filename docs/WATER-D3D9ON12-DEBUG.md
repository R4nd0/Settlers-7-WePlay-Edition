# D3D9On12 River water investigation

Requested 2026-11-26. A controlled scalar GPU reproduction and the actual
Waldreich River draw identify nonfinite atmospheric-scattering arithmetic as
the cause of black water in this scene. An opt-in compatibility correction
restores visible water under the pinned D3D9On12 route. The final build also
exits cleanly and restores the test copy. This is not production renderer
qualification. See the [sanitized receipt](receipts/water-d3d9on12-debug-20261126.json).

The debugging task is complete. Its acceptance criteria are the controlled
cause, observed correction, shader restoration and clean test-copy recovery.
Additional map or hardware qualification is not a requirement or an open task
(user clarification, 2026-11-26).

## Plan

- [x] Reproduce black water without the production graphics adapter.
- [x] Compare original quality controls after reloading the same private save.
- [x] Capture the exact failing shader pair, constants and depth input.
- [x] Compare those inputs with normally rendered D3D9 water.
- [x] Reproduce the arithmetic difference on both actual GPU backends.
- [x] Implement a narrow correction and observe restored water in the game.
- [x] Verify the final build's clean exit/restoration against completed original controls.
- [x] Record sanitized evidence and reproducible validation for the authored milestone.

The accepted DXVK Clear Bank / Leaf B / supersampling release preset, original
installation, simulation clocks and native adapter renderer/ABI guards remain
unchanged. Experiments use only the isolated History test copy. No original
programs, textures, game binaries or private profiles are distributed here.

## Actual quality controls

Original private NativeP54 scene, 1920x1080, pinned dxwrapper v1.9.8700.25,
Windows D3D9On12. Original options were read back after each map reload.

| Configuration after reload | Actual observation |
| --- | --- |
| Shader High, Water High | Large water surfaces black |
| Shader High, Water Low | Still black |
| Shader Low, Water Low | Water visible; the global low profile also changes other effects |
| Shader High, Water Low, original SSAO off | Still black |

The low global shader profile is a workaround, not this correction.
Case `water-on12-quality-20261126-01` exited 0, peak private 1800 MiB,
pressure 0, shared profiles/options/binaries restored, cleanup empty.

## Shader inputs and cause

The read-only observer matched the original River vertex program SHA256
`f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0`
and pixel program
`dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b`.

On12 case `water-on12-observe-20261126-02` visibly had black water and returned
S_OK for the actual game's `IDirect3DDevice9On12` query. D3D9 control
`water-d3d9-observe-20261126-01` showed normal water and returned E_NOINTERFACE.
Both captured the same programs, finite input constants, and entirely zero
scattering uniforms c87..c94. The depth readback was finite under On12.
Both cases exited 0 and restored profiles/options/binaries with empty cleanup.

The High vertex program computes two negative powers at zero scattering,
multiplies those results by an angular factor, then multiplies by zero
scattering coefficients. The resulting nonfinite additive scattering reaches
the pixel shader even though the atmosphere is disabled.

The all-authored scalar fixture uses runtime uniforms and a float render
target, including readback of raw float bits. It reproduces
`pow(base,-1.5) * 2 * base + 0.25`:

| Backend | base=0 original | base=0 finite cap after product | base=1 original/capped |
| --- | --- | --- | --- |
| D3D9 | 0.25 | 0.25 | 2.25 / 2.25 |
| D3D9On12, direct COM query S_OK | NaN (bits ffc00000) | 0.25 | 2.25 / 2.25 |

This is an actual GPU arithmetic reproduction, separate from game validation.
Cases `water-on12-math-dxwrapper-*-20261126-03` and the final reproducible
runner `water-on12-math-final-20261126-01` passed all draws/readbacks.
The Microsoft D3D9On12 shader converter's POW log/multiply/exp implementation
also explains overflow at zero with a negative exponent; this source inference
does not substitute for the measurements of the installed Windows runtime.

## Guarded correction

`--water-pow-cap` requires an explicit source-bound `--water-probe` build and
the pinned D3D9On12 laboratory backend. The original read-only mode stays the
default. Admission retains original executable/module/file hashes, x86 entry
bytes and imports, complete kernel isolation (7), profile backups and the
unchanged memory guardian. All 119 device method addresses must belong to the
pinned wrapper, with only four explicitly pinned Windows shim exceptions.

Only the exact original VS/PS pair on the admitted render thread is eligible.
Every corrected draw additionally requires all 32 scattering floats to equal
zero. Nonzero scattering, unrelated shaders and D3D9 devices are not corrected.
The recipient-owned vertex program receives one local `DEF c255` and two `MIN`
instructions limiting the two POW products to FLT_MAX before their zero
scattering multipliers. Capping only POW would allow the angular multiplication
to overflow again. All original instruction tokens, relative loads, outputs,
pixel shader, texture reads, alpha and render states are retained.

Generated VS: 2648 bytes, SHA256
`73b69274865f33023aef23132694d73544994153d348acd3e51d7de01e800814`.
The program is generated from the owned device's verified original, never
bundled. The original VS is restored after each single original draw; draws
are neither skipped nor replayed. One generated shader is retained for the
admitted process lifetime. No COM release occurs during CRT/DLL teardown.

Actual corrected case `water-on12-pow-cap-20261126-04` returned On12 S_OK,
showed visible water, waves and bank detail, and recorded 137998 applications
and 137998 restores, zero draw failures and diagnostic failure 0. The case
ended with C0000409 on close, pressure 0 / peak private 1698 MiB. All shared
profiles/options/binaries restored; cleanup empty. The first build retained
its program in a global COM RAII object; that late release has been removed
as a possible teardown cause. A clean repeat is required before claiming
successful final exit. The error has not been causally attributed to that
destructor by a crash stack.

The final source-bound build `water-on12-probe-build-20261126-10` repeated the
visible correction in `water-on12-pow-cap-20261126-06`: actual On12 S_OK,
147238 applications/restores, zero failed draws, diagnostic failure 0,
regular original-menu exit 0, pressure 0, peak private 1738 MiB. All profiles,
options and binaries restored, renderer journal restored, cleanup empty.
Its non-target GPU fixtures passed under both backends; native CPU 17 checks
and 57 focused Python tests passed. Synthetic evidence is distinct from the
observed game result.

An extra same-build read-only reversal attempt was stopped by the unchanged
host memory guardian before the world loaded (commit headroom fell to
1372 MiB). It produced no water observation and restored all guarded state.
The completed original On12 black-water and D3D9 normal-water controls above
remain the actual comparison; the blocked extra attempt is not counted as a
successful test.

## Reproduce

Use fresh explicit directories below ignored `work/`, no concurrent S7 client,
and at least the existing 4 GiB free physical and commit preflight. Build once
per PowerShell process so repeated compiler environment initialization does
not grow PATH. Source and binary hashes must still match the manifests.

```powershell
./native/water_on12_math_gpu_build.ps1 -OutputDirectory work/NEW_MATH_BUILD
python -B -m tools.graphics_lab.water_math --build work/NEW_MATH_BUILD --output work/NEW_MATH_CASE --package work/dxwrapper-20261126/release
./native/water_on12_probe_build.ps1 -OutputDirectory work/NEW_PROBE_BUILD
python -B -m tools.graphics_lab.dxwrapper run --bundle work/sg24 --output work/NEW_WATER_CASE --mode dxwrapper-d3d9on12 --package work/dxwrapper-20261126/release --water-probe work/NEW_PROBE_BUILD --water-pow-cap
```

Omit `--water-pow-cap` for the unchanged On12 control. Use `dxwrapper-d3d9`
without that flag for the normally rendered D3D9 input reference. The local
service may require the authorized loopback execution outside the socket
sandbox. Never relax isolation or memory limits to get a case running.

Raw constants/programs/textures and failed experimental tools remain under
ignored `work/`. The attempted RenderDoc pre-entry route crashed before the
menu in D3D12Core and produced no frame. Its prototype is retained privately;
it is not an available CLI path and is not needed for this correction.

## Limits

The visible result covers this High River program and this controlled scene.
No claim is made for other water programs, active atmospheric scattering,
performance, ray tracing, general
D3D9On12 compatibility or shipping a replacement production renderer.
The laboratory adapter does not admit the production DXVK-only graphics hook.

The separately requested one-run performance comparison is recorded in the
[10-second benchmark receipt](receipts/d3d9-on12-short-comparison-20261126.json).
At identical 1920x1080 High settings and camera, uncapped D3D9 averaged
92.23 application FPS / 10.84 ms, while the current On12 route including its
water correction averaged 75.01 FPS / 13.33 ms (18.67% fewer FPS). The On12
correction's per-draw identification overhead is included; this comparison
does not isolate translation overhead or measure physical displayed FPS.
