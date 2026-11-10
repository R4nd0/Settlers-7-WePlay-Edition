# Gold Edition integration

Status: development checkpoint, 2026-11-10. Gold/History crossplay is **not
implemented**. This checkpoint is not a Gold-compatible WePlay release.

The opt-in [local simulation candidate](GOLD-LOCAL-SIMULATION.md) now advances
beyond both the turn-zero wait and a subsequent turn-241 logistics wait. Actual
observations reach turn 12,304 and a new save contains turn 2,667. After a
client restart that save loads and advances to turn 7,920. A later local tester
check confirms completed construction and delivery; its checkpoint also loads.
The [offline saving correction](GOLD-OFFLINE-SAVING.md) passes actual manual
save, autosave and reload checks. The
[graphics-port candidate](GOLD-GRAPHICS-PORT.md) renders the world with actual
4096 shadows, extended detail/range and selectable Mittsommer lighting. Its
camera correction removes rotation caused by the cumulative turn counter.
The separate Maximum candidate also corrects the reproduced duplicate-castle
menu overlay by rendering effects before the UI. Complete effects qualification
remains pending.

The separate [transport checkpoint](GOLD-TRANSPORT.md) adds a passive Gold
packet decoder and an independently checked one-byte checksum. It does not
connect Gold to the History service or enable multiplayer admission.

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
bias. Separate native detail/shadow and effects candidates are described in the
[Gold graphics checkpoint](GOLD-GRAPHICS-PORT.md); this preparation command
does not install or qualify those candidates.

Preparation:

- Requires the exact Gold executable hash, the pinned embedded definitions
  hash and all 16 original instruction/data guards before patching.
- Copies only `Data` into a new directory, checking each copied file; it does
  not copy launcher account/download state. Links/reparse points, overlapping
  source/target paths and existing destinations are rejected.
- Retains the unmodified executable under `Backup`, applies the Gold default
  edits, repairs their loader relocations and recalculates the PE32 checksum.
  Optional debug/affinity blocks are not applied. The corrected output hash is
  `76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab`.
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

### Native startup follow-up

The first preparation output (`9c6d2a...`) retained relocation records for two
absolute operands replaced by the upstream edits, and omitted the new absolute
operand in the developer stub. Windows relocated this executable in actual
diagnostic runs, corrupting the new relative call and local-feature jump.
Preparation now clears only those two obsolete HIGHLOW records and adds the
new one. All other 291,964 HIGHLOW records, section headers and security flags
remain intact. An independent PE parser verified the checksum and the three
affected targets at five load bases. The old output must not be treated as a
verified startup artifact.

`native/build_gold.ps1 -OutputDirectory work/gold-native` builds a separate x86
`weplay_gold_startup.dll` and runs authored native ABI fixtures. This is a
startup component, not a Gold multiplayer adapter or a normal launcher payload.
Its explicit `GoldInitialize` entry must be called before the suspended game's
entry point, outside DllMain. It pins the corrected executable, PE identity,
original callsites and Gold string helpers; it uses independent Gold object
layouts, owner-thread checks and transactional instruction writes. It keeps the
old Orbit record unavailable with its module and all 22 functions null. Two
cloud-enumeration calls no longer wait for a callback from that absent session.
The local label uses the game's own ASCII/wide string allocation routines;
the legacy ticket remains empty. No Ubisoft credential or session is created.
The current fixed label is unsuitable for multiplayer identity.

`-IsolatedLabExecutable` is an explicit development-only build option confined
to `work/`; it adds one exact lab EXE hash to that build. Ordinary builds do not
accept it. The earlier laboratory EXE separately redirected three profile
references and namespaced the mutex. The newer adapter now performs those
changes in memory for the corrected production EXE, with original-byte guards
and rollback. Each canonical executable location gets a stable isolated
Documents/WePlay/Gold profile and a local mutex. Existing shared profiles are
not migrated. A standalone public starter remains unimplemented.

`tools/gold/profile_interface.py` is separately pinned to the original Gold
Interface.bba. It adds the native neutral "No Title" choice only for an empty
title catalog, labels the identity as a WePlay profile and restricts the obsolete
disconnect nag to network games. It does not report a connection as successful,
change an existing title catalog or remove multiplayer disconnect/desync checks.
All other archive payloads are verified unchanged. The ordinary preparation
command does not silently apply these experimental UI changes.

Actual tests have reached the local profile manager and main menu through the
native startup component, with DXVK at 1920x1080. A skirmish reached the 3D map,
but the observed turn counter remained zero. A delayed save created a file in
the isolated profile, appeared in the game's list and loaded back to the map.
The serialized LogicTurn is also zero; progression/resume is not qualified.
This is **not playable-startup or save/load qualification**. The Castle Forge opens
with `-mapInMainMenu`; its catalog, editing and persistence remain unqualified.
`-noMapInMainMenu` explicitly prevents Castle Forge startup. The native test
still used Frida to load/observe the DLL and guard the experiment; it did not
replace Gold startup functions. A later suspended-entry runner loaded the
native adapter without Frida and restored the original entry bytes in memory.
It reproduced turn zero and exited normally; the sixteen backed-up shared
save/options/profile files retained their file set, hashes and timestamps.
No complete graphics benchmark, DLC unlock,
Gold multiplayer or Gold/History match has passed.

### Graphics and profile follow-up

The user requires future tests to use the modern graphics path. The adapter
now requires the pinned x86 DXVK DLL next to the Gold executable. It loads that
absolute path before engine initialization, rejects conflicting or modified
renderers, pins the module and holds its file against replacement. Missing
DXVK fails initialization; it does not select Windows D3D9 as a fallback.

Fresh isolated profiles start at 1920x1080 in a window, with high Gold quality,
antialiasing, SSAO, parallax, shadows and sun shafts. Depth of field stays off.
Existing Options.ini files are preserved. The development profile was explicitly
updated and backed up because Gold had created it at 1024x768 with reduced
effects. The development runner now checks the requested settings and renderer
before creating the game process; the original profiles were not edited.

The factory probe and actual device creation require separate checks. An earlier
probe incorrectly treated two display adapters as failure because it required
exactly one adapter. Physical-only enumeration passed that narrow probe but
Gold then failed device creation with D3DERR_INVALIDCALL. Display enumeration
(`d3d9.enumerateByDisplays = True`) exposes both attached displays on the pinned
dedicated GPU and the actual Gold world renders at 1920x1080. Missing and foreign
DLL probes remain rejected. This establishes device creation and rendering,
with gameplay and performance qualification still tracked separately.
The relevant DXVK settings are documented in its [configuration reference](https://github.com/doitsujin/dxvk/blob/master/dxvk.conf).

DXVK translates the existing renderer. Gold-specific SMAA, shadow/detail hooks
and the full WePlay graphics port remain unqualified. See the
[graphics and profile receipt](verification/gold-graphics-profile-20261110.json).

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
for the original checkpoint and the [native startup follow-up](verification/gold-native-startup-20261110.json)
for the corrected output and subsequent checks.
