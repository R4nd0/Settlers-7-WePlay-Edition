# dxwrapper / D3D9On12 evaluation

Requested 2026-11-26. This is an isolated compatibility experiment, not a release
renderer change or a raytracing implementation. The accepted Clear Bank / Leaf B
and supersampling release preset remains unchanged.

Evaluation closed with a **no-go for release**. All planned modes were exercised;
the D3D9On12 world failed visual compatibility and its regular-exit check was
preempted by the unchanged memory guardian. This is a completed evaluation with
failed compatibility criteria, not a completed renderer migration.

## Plan and completion criteria

- [x] Inspect current handoff, pending changes and running clients.
- [x] Pin official dxwrapper v1.9.8700.25 (upstream commit
  `d77702d55f1725eeae15a6466091db4b998d89b4`). Verify its published archive hash.
- [x] Implement a separate opt-in laboratory route with durable renderer backups,
  owned-process checks, unchanged isolation/profile/memory guards and recovery.
- [x] Verify staging, interrupted recovery and refusal of changed files with CPU
  tests before launching any game.
- [x] Run matched native D3D9 / dxwrapper D3D9 / dxwrapper D3D9On12 cases. Load the
  same original private save, observe the world, close normally, verify restoration.
  Native and wrapper D3D9 closed normally; D3D9On12 reached the exit confirmation
  but the memory guardian stopped it first. Its regular-exit criterion **failed**.
- [x] Record actual renderer evidence, visible compatibility and memory. Distinguish
  module loading, successful device creation and actual world rendering.
- [x] Assess compatibility with the existing native graphics adapter. Preserve its
  renderer identity guard; an unsupported combination is not a qualified feature.
- [x] Publish a sanitized receipt and recommendation, sync the source index, commit
  and push the authored milestone. Preserve unrelated camera/grass prototypes.

Stop a case on the existing memory limits. A failed compatibility case is a valid
evaluation result; it must not be hidden by silent D3D9 fallback or a changed guard.
No performance improvement is claimed from short smoke tests. Full release,
multiplayer, other editions and raytracing remain outside this evaluation.

## Pinned inputs

Official archive: <https://github.com/elishacloud/dxwrapper/releases/tag/v1.9.8700.25>

- `dxwrapper.zip`: `e6aa4fc685115c8f73dee4ea2d446b6a1f8b57dd04db9889d2c6d1b6d7b8c825`
- `dxwrapper.dll`: `23a2889e0785a02f17f1ee764254bf22b937fc5bdc6ed6374201a9f50cc7b43d`
- x86 `Stub/d3d9.dll`: `b8b878e29c2931d6a8d064665c4c93edb28a0105152e7247f8f543c1603985c3`

The pinned release uses `D3d9on12=1` in `[Compatibility]`, together with
`EnableD3d9Wrapper=1`. Its supplied INI and tagged implementation are authoritative
for this version. The older wiki calls the option `ForceDirect3D9On12`.
`D3d9to9Ex`, forced AA/AF, plugins, custom DLLs, exception handling and timing
patches stay disabled so they do not confound this comparison.

Upstream source:
<https://github.com/elishacloud/dxwrapper/blob/v1.9.8700.25/d3d9/d3d9.cpp>

Windows' own D3D9On12 is used. Do not ship a replacement for the operating system
component: <https://github.com/microsoft/D3D9On12#what-can-you-do-with-this>.

## Results

Native reference completed: original private NativeP54 scene loaded, castle,
water, terrain and vegetation observed. A new diagnostic save was created in a
free private slot. Original menu -> desktop exited with code 0. Shared profiles,
private options and guarded game binaries all verified restored; cleanup empty.
Peak private memory 1709 MiB, guardian max pressure 0. This is a bounded smoke
test, not a performance or stability qualification.

60 focused CPU tests passed (renderer staging, device-runner admission, existing
controller/recovery and shader-family integration). An attempted full laboratory
suite produced no completion result after an unusually long run and was
interrupted; it is not counted as a pass.
The standalone x86 device fixture builds with `/W4 /WX`. All three actual GPU
cases passed pixel readback, Present and Reset. Only the D3D9On12 case returned
`S_OK` for both `IDirect3DDevice9On12` and `ID3D12Device`; its device reported
DXR Tier 1.1. The other two returned `E_NOINTERFACE` as expected. These are
synthetic results, not raytracing implementation or S7 qualification. The runner
now rejects silent fallback even when the fixture process exits successfully.

All game cases used the original private NativeP54 save and High-quality
1920x1080, without the native graphics hook. This is matched input and bounded
visual observation, not a frame-identical image comparison or performance test.

| Mode | World observation | Exit | Restoration | Peak private MiB |
| --- | --- | --- | --- | ---: |
| Windows D3D9 | Castle, terrain, vegetation and visible water | Original UI, code 0 | Verified | 1709 |
| dxwrapper D3D9 | Same scene, no gross visual regression observed | Original UI, code 0 | Verified | 1717 |
| dxwrapper D3D9On12 | Same scene; large water surfaces black | Owned memory-guardian stop, `0xe0537021` | Verified | 1718 |

The D3D9On12 guardian recorded minimum host commit headroom 1219 MiB and pressure
level 1. It stopped the owned test while the original desktop-exit confirmation
was open. No normal-exit success is claimed. All cases retained shared profiles,
restored private options and the five guarded binaries, and had no cleanup errors.
No unrelated program was closed. The source of the host-memory drop is unknown;
these observations do not establish a renderer memory leak or a memory saving.

The actual S7 D3D9On12 path is supported by the pinned stub/wrapper hashes,
Windows D3D9On12/D3D12 module inventory, one `Direct3DCreate9On12` branch in its
log followed by object/device creation, and zero native/Ex fallback branches.
The tagged source returns immediately after successful On12 object creation and
forwards CreateDevice to that object:
<https://github.com/elishacloud/dxwrapper/blob/v1.9.8700.25/d3d9/IDirect3D9Ex.h>.
A direct COM query was performed in the standalone fixture, **not in the game**.
Loaded modules alone are not the device proof.

Sanitized aggregate evidence: [evaluation receipt](receipts/dxwrapper-evaluation-20261126.json).
The earlier progress receipt is historical and superseded by this evaluation.

Existing supersampling/material hook compatibility is currently **unsupported**:
both `tools/graphics_lab/load_hook.py` and `native/supersampling_hook.cpp` require
the pinned DXVK renderer identity. The lab route rejects combining this hook
with dxwrapper before launching. Supporting a second renderer requires a
separate guarded adapter change and its own actual GPU/game validation.

## Decision

Retain DXVK and the accepted release graphics preset. Keep the pinned dxwrapper
route available only for isolated investigation. D3D9On12 provides a viable
D3D12 device on this hardware, but this S7 configuration is visually incompatible.
The black-water cause is not established; a fix would require a separately
scoped investigation of the original water rendering and translation behavior.
There is no performance, other-edition, multiplayer or release qualification,
and no raytracing feature was added to S7.

## Reproduce / recover

Use a prepared History laboratory bundle under `work/`, with the original
standalone manifest, isolation marker and pinned DXVK renderer, and no production
graphics auto-loader configuration. Never use the original installation.

```powershell
python -B -m tools.graphics_lab.dxwrapper run --bundle work/sg24 --output work/NEW_NATIVE_CASE --mode native
python -B -m tools.graphics_lab.dxwrapper run --bundle work/sg24 --output work/NEW_DX9_CASE --mode dxwrapper-d3d9 --package work/dxwrapper-20261126/release
python -B -m tools.graphics_lab.dxwrapper run --bundle work/sg24 --output work/NEW_DX12_CASE --mode dxwrapper-d3d9on12 --package work/dxwrapper-20261126/release
```

Only one case/game at a time. Close normally via the original game UI. The
measurement budget requests regular closure, never automatically discards a game.
After an interrupted controller, first confirm no S7 process is running, then:

```powershell
python -B -m tools.graphics_lab.dxwrapper recover --case work/INTERRUPTED_CASE
```

Renderer restoration precedes the existing private-settings recovery. Unexpected
target/backup changes cause refusal rather than overwriting newer data. Saves
are backed up but never rolled back. Logs and private module inventories stay
under `work/`; only sanitized aggregate receipts belong in Git.

Build and run the synthetic device checks separately, with no S7 client running:

```powershell
powershell -NoProfile -File native/dxwrapper_device_fixture_build.ps1 -OutputDirectory work/NEW_DEVICE_BUILD
python -B -m tools.graphics_lab.dxwrapper device-fixture --build work/NEW_DEVICE_BUILD --output work/NEW_DEVICE_CASE --package work/dxwrapper-20261126/release
```

The runner verifies current source/build-script and binary hashes, gives each
renderer its own directory, and limits each subprocess to 30 seconds. The fixture
configuration differs only in the process allowlist name. A successful process
exit must still be interpreted alongside the recorded COM and pixel results.
