# Gold Edition integration

Status: development checkpoint, 2026-11-10. Gold/History crossplay is **not
implemented**. This checkpoint is not a Gold-compatible WePlay release.

## Identified installation

The selected Ubisoft installation uses the nested layout
`S7Gold/Settlers 7/Data/Base/_Dbg/Bin/Release/Settlers7R.exe`.
The executable reports version `1.12.1396.0`, is x86/PE32 and has 14,141,768
bytes. Its observed SHA-256 is
`ec27c7fab2c83a1444f043eb4730f0065393ca2e91607f71e5fd78ee5a5c7c94`.
This identifies the local build; it is not a publisher signature or a promise
that every Gold distribution has the same executable.

`S7Patcher --inspect` now recognizes this fingerprint and the Gold layout,
including its `S7Gold` parent. Inspection remains read-only. Unknown files in
a Gold layout are still unsupported; an ambiguous Gold/History directory is
rejected. Recognition does not grant multiplayer admission.

## Offline unlock preparation

The existing upstream [S7Patcher](https://github.com/Eisenmonoxid/S7Patcher)
contains a Gold default block in `Definitions/Definitions.bin`. The linked
[Steam discussion](https://steamcommunity.com/app/48210/discussions/0/6761670312890986628/)
describes restoring locked local content, not restoring Gold multiplayer.
Upstream requires official patch 1.12.1396 and a successful first game launch.

The new source tool uses that existing default block:

```console
python -B tools/gold/prepare.py inspect "C:/Games/S7Gold"
python -B tools/gold/prepare.py prepare "C:/Games/S7Gold" "work/gold/Game"
```

The optional `--dxvk <existing-d3d9.dll>` stages only the pinned x86 DXVK 3.1.1
binary already used by WePlay. It does not download anything or qualify the
renderer. The test configuration requests strict floating-point emulation,
16x anisotropic filtering and a 60-frame cap; it has no global negative mip
bias. Native Gold detail/shadow hooks and ReShade effects are not ported.

Preparation:

- Requires the exact Gold executable hash, the pinned embedded definitions
  hash and all 16 original instruction/data guards before patching.
- Copies only `Data` into a new directory, checking each copied file; it does
  not copy launcher account/download state. Links/reparse points, overlapping
  source/target paths and existing destinations are rejected.
- Retains the unmodified executable under `Backup`, applies the Gold default
  edits and recalculates the PE32 checksum. Optional debug/affinity blocks are
  not applied. The output hash for this exact default patch set is
  `9c6d2a32901b96dfa2ca8012e2f98d3676414ad2fbc350f54c8762a00e951e18`.
- Records original source hashes and explicit false capability/qualification
  flags in `gold-development.json`. Failed copies keep an incomplete marker.
- Does not launch the game or edit shared Options.ini, Profiles.xml, saves,
  ranks, titles or achievements. It creates no starter: a separate executable
  alone does not isolate the Windows Documents profile.

This is the upstream offline patch set, including its developer-mode changes.
It must not be admitted into WePlay multiplayer as a History profile. Future
Gold multiplayer needs a separately reviewed release policy for developer mode.

The regular History installer now returns a dedicated, localized Gold-adapter
message for a selected Gold layout. It does not install the History adapter or
create a managed instance for that source. Existing published packages and the
normal installed launcher were not changed by this source checkpoint.

## Observed differences and startup boundary

Gold does not import or contain the History `uplay_r1_loader.dll` name. Its
runtime loads the legacy `ubiorbitapi_r2.dll` and attempts a child launcher
start. The existing WePlay adapter is entered through the History Uplay loader;
copying it next to Gold would not establish its bootstrap, native ABI or hooks.

Gold's original Interface.bba SHA-256 is
`5f335b7ebdf0707079009abda7334ecf5d83cfd3039776d2e775f39718255215`;
History's original is
`766ae21ef06709309a68fb5bb792b0fd56373c668f02140122eef181917b0b94`.
Both have 284 readable file entries, all with verified decompressed size/CRC.
230 payloads are identical; 54 differ, including session creation/listing,
host lobby, network status, profile handling and castle forge. Existing History
archive pins and text replacements cannot simply be accepted for Gold.

Private startup experiments used a fingerprinted diagnostic EXE with all three
Gold profile-path references redirected to a new profile under `work/`.
Sixteen existing Gold/History save/options/profile files were backed up before
each attempted launch and their hashes/timestamps checked afterward. No shared
file changed in these checks. No old save was substituted.

The first setup attempt stopped before entry because its affinity-handle
permissions were insufficient. A subsequent guard rejected Frida's module-path
snapshot; native Windows path validation corrected that diagnostic check.
An all-socket-denied run did not reach a game window. A later loopback-only run
loaded the old Orbit DLL, attempted a child process (denied by the test guard)
and exited with code 0 before any observed game window or renderer log. Exit 0
here is **not** a successful playable startup. An additional child-call trace
was unresponsive and was stopped after PID, parent, path and hash validation.
Its diagnostics were retained; it is not qualification evidence.

Frida's unsigned helper caused a Windows Open File security warning. It was
not a Gold-unlock payload. No security warning was approved through computer
automation and no Windows security setting was changed. Frida is a private
diagnostic dependency, not a new Gold launcher dependency.

No playable Gold scene, DLC selection, castle forge, DXVK rendering, graphics
benchmark, Gold multiplayer or Gold/History match is established by this work.

## Remaining implementation and acceptance plan

1. **Gold startup and adapter.** Independently map the old platform bootstrap,
   object layouts, calling conventions and guarded unavailable/offline state.
   Preserve genuine ownership requirements; do not fabricate credentials or
   legacy sessions. Establish isolated, offline playable startup first.
2. **Gold UI/content port.** Build Gold-specific archive patches from its own
   originals. Check DLC/maps and castle forge in the actual game. Keep campaign
   progress and user saves intact.
3. **Transport and simulation.** Map Gold's RendezVous/PRUDP and native send,
   receive, session and simulation callbacks. Start with loopback. Do not relax
   executable, object, native-call or content guards to get past admission.
4. **Cross-edition contract.** Compare actual maps, simulation rules, save and
   command formats before defining a shared compatibility identity. Gold is
   currently different in both executable and archive content. Merely deleting
   these differences from the existing network ID would be unsafe.
5. **Graphics port.** Qualify Gold D3D9/DXVK, then SMAA, display selection,
   detail and shadows with Gold-specific guards. Render settings remain local
   to each PC and must not alter deterministic simulation.
6. **Acceptance.** Gold-to-Gold first, then Gold against Ubisoft History and
   Steam History in both host directions: map load, commands/building, AI,
   castle transfer, sustained replay checksums, save/load/resume and lobby
   reuse. Follow with deliberately enabled LAN/Internet tests. Only a passing
   matrix can justify enabling Gold in the normal installer and release feed.

The current technical evidence does not support a public Gold-compatible
release or a claim of complete compatibility. See the [verification receipt](verification/gold-20261110.json)
for the completed checks and retained limits.
