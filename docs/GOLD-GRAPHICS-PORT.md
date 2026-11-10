# Gold graphics port candidate

Development checkpoint, 2026-11-10. This is not a Gold-compatible WePlay
release. The native renderer and Mittsommer have actual Gold world observations;
save reload and the camera correction passed a local check. The separate
Maximum pre-UI candidate now removes the reproduced menu overlay in actual
Gold tests. Complete effects qualification and multiplayer remain pending.

## Maximum menu correction

The original Maximum trial showed a pale duplicate castle over menus, also
confirmed by the local tester. The new opt-in `-Gold -GoldPreUI` add-on renders
the selected effects after Gold's final scene composite and before its UI.
The profile manager, main menu, load dialog, reloaded world and in-game menu
now render without that strong duplicate. All four selected Maximum shaders
remain compiled and enabled; no UI mask or reduced preset is used.

Two separate faults had prevented a reliable early injection. The pinned
ReShade 6.8.0 runtime did not report the newer Vulkan viewport/scissor and
descriptor/push-constant entry points used by DXVK. The reviewed
`tools/gold/reshade_state.patch` forwards those calls unchanged and supplies the
same add-on events as the older hooks. It preserves availability of core and
EXT/KHR aliases, including KHR-only Vulkan 1.3 devices. The patch applies only
to exact upstream commit `18deaa52de0c425a78b329e9cb3c497281cd00ec` and verifies
all three input/output file hashes. Its upstream code retains the accompanying
[ReShade license](../tools/gold/RESHade-LICENSE.txt).

The pipeline metadata cache also retained completed command lists until later
reuse. It reached its fixed bound during menu transitions, so new lists lost
their shader identities. Entries now expire on close, reset and destruction.
Pipeline-library metadata is copied only from the same device; explicit
shader/blend state wins and linked pipelines survive library destruction.
Bounds remain in place. The source candidate excludes per-draw tracing.

The add-on checks the exact Gold executable/clock layout, owned Vulkan runtime,
single full-HD render target without depth, and the observed scene-to-UI
shader/blend sequence. FNV shader fingerprints select the rendering phase;
they are not a security or cross-edition compatibility identity. The launcher
still pins actual executable, renderer, runtime, add-on and asset bytes.
Before rendering effects it snapshots bounded command state, including dynamic
descriptor offsets and push constants, then resumes the native render pass
with LOAD/STORE and restores that state. Unsupported state invalidates the
capture. Resource views follow image/device lifetime and runtime access is
serialized. No simulation callback or game memory write is introduced.

Developer build commands, using a separate upstream source checkout with its
official submodules/dependencies already prepared:

```powershell
pwsh -File tools/gold/build_reshade_hooks.ps1 -SourceDirectory work/reshade-v6.8.0 -OutputDirectory work/gold-hook-fixtures -BuildRuntime
pwsh -File native/build_atmosphere.ps1 -Gold -GoldPreUI -OutputDirectory work/gold-pre-ui
```

These commands build developer artifacts under `work/`; they install and
launch nothing. Rebuilt runtime hashes must be reviewed before admission.
`tools/gold/pre_ui_runtime.py` isolates the actually tested candidate pins and
sets descriptor heaps/buffers off only in the child process. Normal Gold and
History runtime pins, global Vulkan layers and the original game installation
stay unchanged. This remains an explicitly selected development candidate,
not a normal-installer release.

The source build passed 214 clock/FPU checks and the actual bounded state/cache
fixtures; wrapper fixtures cover unchanged native forwards, state conversion,
dynamic offsets, constant ranges, disabled events and KHR fallback. These use
mock GPU APIs. Separately, actual Gold reloaded a saved world and advanced through
14,096 to 14,855 with the first source-built add-on. The final build synchronizes
initialization before its early callback and advanced from 17,124 to 17,609
after a fresh reload; its initial loaded turn was not sampled. The ordinary
Gold clock remains byte-identical to its earlier build. Native 4096 shadows,
1/200 detail, extended ranges and the real Mittsommer timeline were observed. All successful
comparison clients exited normally and all 16 protected shared files retained
their file set, hashes and timestamps. See the
[Maximum verification receipt](verification/gold-maximum-ui-20261110.json).

Qualification is limited to the tested 1920x1080/high DXVK configuration on the
local AMD GPU. Other resolutions/devices, display/resize recovery, a complete
Mittsommer cycle, measured performance and independent output comparisons of
every effect remain open. This graphics correction does not implement Gold
networking or establish Gold/History determinism.

## Native renderer

`native/gold_visual.inc` maps the pinned Gold executable independently. Its
strict `0`/`1` choices match the existing History controls:

- `S7_EXTENDED_DETAIL`: resolution-dependent model/vegetation scale 1/200
  instead of 1/300, in both Gold's setter and its inlined world-start setter.
- `S7_DETAILED_SHADOWS`: allocation and resource estimate both use a 4096 base.
- `S7_ULTRA_SHADOWS`: optional 8192 base and .92 near-cascade blend.
- `S7_EXTENDED_SHADOW_RANGE`: 19200 detailed and 76800 far terminals, with
  matching span/ratio and Gold's combined culling volume.

The distant-cascade filter retains the original 2048 sampling footprint on
4096/8192 maps. Gold uses a stack cascade index and ECX vector; EAX already
holds its callback. Its separate bridge preserves EAX, EDX, nonvolatile
registers, flags and x87 and reproduces the original pushed argument. It checks
the independently identified renderer owner, map size and four inverse values.

All edits are in memory after whole-file and instruction/relocation guards.
Gold defaults remain pinned DXVK and native high quality; additional controls
require explicit selection. No simulation or packet field is changed.

An isolated actual Gold start reached the profile manager at 1920x1080/high.
Read-only observations confirm a real 4096 map, four 1/4096 inverse values,
both 1/200 detail operands and the extended range operands. This establishes
neither visual acceptance of all scenes nor an FPS gain.

## Gold-owned assets and effects

`tools/gold/graphics_assets.py` accepts only the exact original Gold Interface
and Shr archives. It composes the local profile corrections with six camera
DOF transitions and the optional Mittsommer menu/weather hook. Camera changes
preserve the pending DOF choice. The environment is generated from Gold's own
Neutral XML using the shared authored curve generator.

All original shared resources remain byte-identical; one environment is added.
Six interface members change, including the two local profile corrections.
Each changed actual Gold Lua script compiles under Lua 5.1. Game scripts/XML
are not distributed. Actual selection loads WePlayMidsummer as the eleventh
environment, with five azimuth keys over the 1,200,000 ms period. The world
renders with the selected lighting; full-cycle and Maximum effects checks
remain pending.

Gold's original focalizer adds its cumulative `game.turnsExecuted` counter to
the rotation input every update. Once the local simulation advances, this
rotates the camera without a rotation command. The builder removes that exact
single addition under the original archive guard. It preserves the counter,
manual inputs and damping. The reloaded world remained steady without input,
manual zoom worked, and the local tester confirmed no visible rotation.
This does not establish every focus-loss/input-device combination.

`tools/gold/visual_runtime.py` uses the existing ReShade integrity/preset
machinery in a separate module instance, with its own folder and Gold clock
pin. It requires the prepared Gold EXE and pinned DXVK. DLL, manifest, shader,
texture, add-on and preset-repair checks remain active. Only the owned child
receives Vulkan-layer variables. Setup is extracted after hashing, not run;
History runtime pins and installed profiles are unchanged.

Build the separate clock with `native/build_atmosphere.ps1 -OutputDirectory
<new-work-dir> -Gold`. It accepts only the prepared Gold executable. Gold's
registry, active environment and scalar-key vectors have an independently
checked extra four-byte header. The callback remains read-only and preserves
floating-point state. An actual Neutral observation confirms the registry,
record/timeline vtables, period, angle and key layout; this does not qualify
Mittsommer haze/heat output.

The first SMAA-enabled start did not reach the menu. Its verified owned,
unresponsive process was stopped after bounded diagnostics. The DXVK-only
comparison subsequently reached the menu after a long initialization. The
delay and effects interaction remain unresolved. A later SMAA-enabled start
reached the menu and the reloaded world; its log confirms successful SMAA
compilation and its preset selects that technique. A rendered effect comparison
and full Maximum output qualification remain pending. The later pre-UI
correction above addresses the reproduced menu artifact. DXVK stayed active; no legacy fallback
was used.

## Verification and next checks

Both Gold build variants pass ABI/transaction fixtures, 25 changed graphics-site
rejections, 60 filter ABI combinations, 75 ordinary network checks and
Gold-specific guards. The original Gold image matches all 13 mapped instruction
sequences. Gold Python/Lua checks pass 35 tests, including counter-independent
camera input and exact-anchor rejection. The clock reader/FPU suite
passes 214 checks across three layouts, and 17 existing Mittsommer checks pass.

The long logistics interval and subsequent DXVK graphics/reload interval exited
normally with all 16 shared profile/save files unchanged. The stopped effects
test also retained those shared files. The SMAA/Mittsommer interval and subsequent
Maximum/save-correction interval also exited normally with all 16 shared files
unchanged. Maximum compiled all four shaders, loaded the Gold clock add-on and
rendered the reloaded world, but showed visible menu overlays in the original
trial. The separate pre-UI candidate above corrects that reproduced artifact.

The saved turn 2,667 reloaded after a client restart and advanced through
2,731, 3,881 and 7,920. Another reload with the camera correction advanced
through 2,890 and 4,745. The local tester confirmed completed construction and
material delivery; completed buildings were observed and their turn-11,982
checkpoint loaded after a client restart. This remains a single-map check.

Next: qualify the remaining effects and display lifecycle,
then implement and test Gold's
multiplayer adapter and command/save contract against both History editions.
Normal installer admission and network compatibility identities stay closed.

[Verification](verification/gold-graphics-port-20261110.json) ·
[Local simulation](GOLD-LOCAL-SIMULATION.md) · [Integration plan](GOLD-EDITION.md)
