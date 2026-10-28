# Isolated D3D11 experiment

This opt-in experiment runs S7 History Edition's D3D9 renderer through the
official x86 dgVoodoo2 2.87.6 library with D3D11 feature level 11.0 explicitly
selected. It is a translation backend, not a native engine port or a new
lighting/post-processing implementation. No game executable or platform
adapter patches are introduced. Never deploy over a running game.

## Verified local result (2026-10-28)

The independent copy under `work/d3d11-review/game` reached the main menu.
The dgVoodoo watermark was visible and its local `d3d9.dll`, Windows D3D11 and
DXGI modules were loaded; no D3D12 module was loaded. The configuration forced
`d3d11_fl11_0`, one hardware adapter and a 60 FPS limit. Application resolution,
filtering and antialiasing were retained. No rendering defect was obvious in
the inspected menu screenshot; maps, water and extended gameplay are untested.

120/120 one-second passive samples were foreground samples (120.066 seconds).
Median CPU use was 0.766 CPU-core equivalents; first/last 30-sample medians were
0.720/0.812. Private bytes increased from 1869.5 to 1878.3 MiB. This was the
baseline four-physical-core adapter with process affinity 0x55, not the separate
two-core DXVK candidate, so this is not a controlled renderer comparison.
The original executable is already large-address-aware; it was not patched.

PresentMon 2.6.0 reported 2,169,558 lost ETW events and produced no frame CSV.
Its exit code was zero, but the capture is invalid. **No measured FPS, p99
frame time or absence of stutter is established.** The configured 60 FPS limit
is not an observed frame rate. Private raw observations remain under
`work/d3d11-review/menu-timing-1`; they are not distributed or committed.

The owned game and launcher were gracefully closed after the measurement;
the launcher's saved game exit code is zero. No D3D11 game remains running.

## Prepare a fresh copy

Download the original ZIP from the author's release:

https://github.com/dege-diosg/dgVoodoo2/releases/tag/2.87.6

ZIP SHA256: `9afa493396feec7108f42a2d97661eef96bbaac5addf0dfb5cfbc3001cd76a0a`

x86 D3D9.dll SHA256: `060cb161eb7a0c608a520185f0b334db325ad71d69016bc26060c9d5b9651f7d`

Run from the repository root, replacing the example paths and adapter number:

```powershell
python tools/d3d11/adapters.py
python tools/d3d11/prepare.py --source work/existing-native-test-copy --output work/new-d3d11-copy --archive work/dgVoodoo2_87_6.zip --adapter 1
```

The source must be an existing, fully inventory-verified local S7 test bundle
with its native D3D9 graphics path. The output must not exist or overlap the
source. Files are copied, not hard-linked. LocalData and profiles are excluded.
Only the pinned x86 DLL and generated configuration are added to game content.
Both manifests are checked, and the resulting content inventory must differ
in exactly these two files. An interrupted/incomplete copy must not be started.
Reversal means returning to the untouched source copy.

## Launch and choose hardware

```powershell
work/new-d3d11-copy/Runtime/python/python.exe -B tools/d3d11/launch.py --bundle work/new-d3d11-copy --archive work/dgVoodoo2_87_6.zip
```

A hardware-selection dialog appears on every invocation. DXGI supplies device
names, memory and one-based adapter ordinals; no vendor/model is hardcoded.
Software adapters are excluded. Cancel does not launch the game or local
service. For automated local tests only, `--adapter N` bypasses the dialog.
`--preflight --adapter N` validates/starts the temporary service without
launching the game. Run with the copy's Python to preserve process-ownership
checks. No system GPU preference, registry or security setting changes.

Selection is written while the bundle's existing session lock is held and
running S7 processes prevent configuration changes. Preparation verifies the
full content; launch rechecks the EXE, adapter, engine/graphics modules, menu
and launch/service code. It does not reread all maps/videos on each start.
The original local launcher still owns the game and
service lifecycle. Use `-B` to avoid adding bytecode to fingerprinted folders.

This is a separate experimental launcher, not an extension of the concurrently
developed common graphics starter. In this experiment the adapter ordinal is
part of `dgVoodoo.conf` and hence its content fingerprint. Different PC choices
may produce different LAN content IDs. Do not bypass admission checks; a proper
integration must separate local device preferences from shared content before
claiming cross-PC compatibility. The game itself remains 32-bit.

## Remaining qualification

- Measure trustworthy frame intervals and repeat a longer foreground run.
- Verify the selected physical GPU at runtime independently of configuration.
- Exercise maps, water, vegetation, shadows, alt-tab and exit/restart.
- Verify local two-client admission/gameplay and real two-PC LAN separately.
- Integrate the common per-start graphics selection without content-ID drift.
- Evaluate optional post-processing only after these compatibility checks.

A native D3D11 replacement is not needed to get the tested menu onto D3D11.
If a later wrapper defect blocks gameplay, identify its exact rendering call
first. A native fallback would require replacing D3D9 interfaces used by both
Vision90/vBase90 and the EXE's custom CDX9 render loop, including shader and
resource management; no such native port is implemented or promised here.

## License and tests

The author's redistribution rules permit unmodified components bundled with a
specific game/mod, require the complete original ZIP for standalone hosting,
and prohibit inclusion in general-purpose launchers/frameworks. This repository
contains no dgVoodoo binaries or copied shader/game assets. Obtain the ZIP from
the author; preserve its included terms if later distributing a game-specific
optional package.

https://dege.freeweb.hu/dgVoodoo2/ReadmeGeneral/

```powershell
python -m unittest discover -s tools/d3d11 -p test_prepare.py -v
```

Four synthetic tests cover copy isolation/profile exclusion, exact content
additions, pre-mutation corruption/foreign-renderer rejection, existing/nested
output refusal, link rejection and unexpected configuration templates. They
are not runtime rendering tests. The official archive and x86 DLL hashes were
also validated against the actual downloaded package.
