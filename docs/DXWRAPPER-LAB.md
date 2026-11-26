# dxwrapper / D3D9On12 evaluation

Requested 2026-11-26. This is an isolated compatibility experiment, not a release
renderer change or a raytracing implementation. The accepted Clear Bank / Leaf B
and supersampling release preset remains unchanged.

## Plan and completion criteria

- [x] Inspect current handoff, pending changes and running clients.
- [x] Pin official dxwrapper v1.9.8700.25 (upstream commit
  `d77702d55f1725eeae15a6466091db4b998d89b4`). Verify its published archive hash.
- [x] Implement a separate opt-in laboratory route with durable renderer backups,
  owned-process checks, unchanged isolation/profile/memory guards and recovery.
- [x] Verify staging, interrupted recovery and refusal of changed files with CPU
  tests before launching any game.
- [ ] Run matched native D3D9 / dxwrapper D3D9 / dxwrapper D3D9On12 cases. Load the
  same original private save, observe the world, close normally, verify restoration.
- [ ] Record actual renderer evidence, visible compatibility and memory. Distinguish
  module loading, successful device creation and actual world rendering.
- [ ] Assess compatibility with the existing native graphics adapter. Preserve its
  renderer identity guard; an unsupported combination is not a qualified feature.
- [ ] Publish a sanitized receipt and recommendation, sync the source index, commit
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

57 focused CPU tests passed (renderer staging, device-runner admission, existing
controller/recovery and shader-family integration). An attempted full laboratory
suite produced no completion result after an unusually long run and was
interrupted; it is not counted as a pass.
The standalone x86 device fixture builds with `/W4 /WX`; execution is deferred
until the same host headroom guard passes. It queries the actual
`IDirect3DDevice9On12` / `ID3D12Device` interfaces, checks DXR capability, reads
back known pixels and exercises Present/Reset. Its results are synthetic and
must remain separate from observed S7 behavior.

At the first subsequent GPU preflight the host had less than the required
4 GiB RAM/commit headroom. No guard was weakened and no unrelated process closed.
No D3D12, performance, visual improvement or release qualification yet.

Existing supersampling/material hook compatibility is currently **unsupported**:
both `tools/graphics_lab/load_hook.py` and `native/supersampling_hook.cpp` require
the pinned DXVK renderer identity. The lab route rejects combining this hook
with dxwrapper before launching. Supporting a second renderer requires a
separate guarded adapter change and its own actual GPU/game validation.

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
