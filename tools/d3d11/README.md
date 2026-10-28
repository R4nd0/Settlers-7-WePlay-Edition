# D3D11 experiment: gameplay blocked pending memory and menu fixes

Updated 2026-10-28. This is an isolated research tool, not a qualified graphics
upgrade. **The starter currently refuses every game start.** `--preflight` can
validate the copy and service without starting a game or writing the shared
Documents/Options.ini. There is no command-line override for the qualification
gate. No renderer from this experiment belongs in the LAN release yet.
The research graphics dialog uses English throughout; its quality indices,
allowlisted values and launch-blocking policy are unchanged by the translation.

The D3D11 wrapper repeatedly failed a real map load with Out of memory. The
native D3D9 fallback has not passed gameplay qualification either. Separate
native-menu measurements reproduced stalls; the user clarified that a later
reported system block could not be assigned unambiguously to a particular run.
Reducing graphics quality and adding a warning do not establish that either
problem is fixed. The separate shared DXVK investigation is described in
[GRAPHICS-START.md](../../docs/GRAPHICS-START.md); it is not deployed by this tool.

## Observed results

The separate `work/d3d11-review/game` copy initially reached the menu through
the official x86 dgVoodoo2 2.87.6 D3D9-to-D3D11 wrapper. Its watermark and local
d3d9.dll were visible; Windows D3D11/DXGI were loaded, with no D3D12 module.
The configuration selected `d3d11_fl11_0`, one hardware adapter and a 60 FPS
limit. This translates the existing renderer; it adds no native lighting model,
post-processing pipeline or FSR integration.

An initial 120-second menu observation had 120/120 foreground samples, median
CPU use of 0.766 core equivalents and private bytes of 1869.5 to 1878.3 MiB.
It used the baseline four-physical-core adapter, not the separate two-core DXVK
candidate. PresentMon 2.6.0 lost 2,169,558 ETW events and produced no frame CSV:
**there is no valid FPS/frame-time measurement or proof of smoothness here.**

Five fresh local skirmish attempts on **Schlacht um Tanholm**, with one AI,
failed with visible Out of memory errors near 3708–3751 MiB private bytes:

| Native settings | Wrapper settings | Result |
| --- | --- | --- |
| High, all effects | 8x MSAA, 16x filtering, 1024 MB virtual VRAM | OOM |
| High, all effects | Standard game AA, otherwise same | OOM |
| Medium textures, remaining high | Standard AA, 1024 MB | OOM |
| All medium, only shadows enabled | Standard AA, 1024 MB | OOM |
| All medium, only shadows enabled | Standard AA, 512 MB | OOM |

256 MB produced a minimum-GPU-memory startup warning; that warning was not
accepted, and the startup was stopped. Menu text/terrain artifacts were also
observed during later D3D11 runs. More GPU VRAM and disabling MSAA alone did not
solve this process-address-space failure.

The original EXE is already large-address-aware. Direct PE inspection found
Settlers7R.exe, Vision90, vBase90, vGeom90, VBaseUI90, VisionEnginePlugin,
SpeedTreeRT, mss32, binkw32 and the local adapter to be x86. The user has only
the game files, not original game/engine sources or a complete SDK. A normal
x64 build therefore cannot be produced here. Changing an EXE header does not
convert its machine code or middleware. See Microsoft's
[process interoperability documentation](https://learn.microsoft.com/en-us/windows/win32/winprog64/process-interoperability).

After removing the wrapper, the short native D3D9 launch produced 26 memory
samples, peak private bytes of 1051 MiB, minimum free 32-bit address space of
2682 MiB and a largest free region of at least 2046 MiB. Its exit code was zero.
Window capture repeatedly failed with `FrameArrived timed out`, so these samples
do not prove a working menu or map. The user subsequently confirmed the menu
had hung again. There was no measurement of host CPU/GPU pressure at that
instant; the later idle snapshot cannot explain the earlier overload.

## Safeguards implemented, not a guarantee of stability

- The default launcher and its game-spawn boundary reject gameplay while
  `GAMEPLAY_QUALIFIED` is false. Qualification requires a code change supported
  by new runtime evidence; there is no user-profile or CLI bypass.
- The known wrapper can be removed with a fixed-file, restartable journal in
  LocalData/native-fallback-v1. Both manifests retain integrity checks. Unknown
  intermediate files or changed backups cause refusal, not an inventory refresh
  that silently accepts unrelated modifications. This prepares a fallback; it
  does not qualify D3D9 performance. Original game/engine/platform binaries are
  unchanged.
- Native quality values are capped at medium; additional effects and forced
  MSAA are disabled. Remembered memory pressure or an abnormal game exit selects
  low quality, no effects and no AA on the next future qualified start. Saved
  profiles and automated choices cannot bypass the caps.
- The combined dialog shows view range, textures and other native quality
  settings. GPU forcing, wrapper filtering and FPS limits are disabled in the
  native fallback because this tool cannot enforce them there. FSR is absent.
  The preflight action is labelled as a check, not a game start.
- The original in-game graphics panel is hidden and disabled, with a separate
  notice directing the player to the startup dialog. The final panel was visibly
  checked under D3D11, including a Sound-tab round trip; Sound remained usable.
  All other BBA entries were verified unchanged and the patched Lua parsed.

`memory_guard.py` is a read-only sentinel intended for a future qualified start.
It uses only the original Popen child HANDLE, confirms its executable path and
x86 architecture, and never reopens or terminates a game by numeric PID. It
checks private bytes plus free and largest contiguous regions in the 4 GiB
address space via [VirtualQueryEx](https://learn.microsoft.com/en-us/windows/win32/api/memoryapi/nf-memoryapi-virtualqueryex).
No game memory, account data, paths or packet contents enter the summary.

Warning thresholds are conservative policy values, not measured allocation
guarantees: 2816 MiB private bytes, 768 MiB free address space or a largest free
region of 64 MiB; critical classification starts at 3328/256/16 MiB respectively.
Three consecutive pressure samples persist recovery and produce one warning
to save and restart. **The sentinel does not kill an ongoing match or save it
automatically. A large allocation can fail before a polling warning arrives.**
An unavailable monitor aborts only its newly created child during startup;
monitor failure during play warns and requests recovery for the next start.

To avoid making an overloaded system worse, counters are polled every 2 s,
the full address scan is cached for 10 s, and periodic summaries are written
only every 30 s, plus startup, exit and the first warning. A full scan has a
50 ms wall-time budget and a region-count limit. Exceeding it reports monitor
failure instead of repeating an expensive scan. In a small private x86 fixture
reserving 64 MiB of virtual space, three full scans had a median of 3.44 ms
and maximum of 29.11 ms; 20 cached reads had a median of 0.002 ms. These are
fixture measurements, not game overhead or smoothness measurements.

Host memory is checked as well as the game's address space. The sentinel reads
available physical RAM and system-wide remaining commit through
[GetPerformanceInfo](https://learn.microsoft.com/en-us/windows/win32/api/psapi/nf-psapi-getperformanceinfo),
converting page counts with the reported page size. It does not mistake the
64-bit helper's own virtual address space or its per-process commit allowance
for the game's or system's available memory. Before any future qualified game
spawn, both available physical RAM and remaining system commit must be at least
4096 MiB. This is a conservative experiment policy, not the game's official
minimum RAM specification or a guarantee against subsequent memory pressure.
The check is repeated immediately before creating the game child.

During play, host-pressure warning thresholds are 1536 MiB available physical
RAM or 2048 MiB remaining commit; critical classification uses 768/1024 MiB.
These share the existing three-sample warning/recovery behavior and bounded
polling schedule. Summaries retain minimum host headroom. No process is closed
to make room, and CPU/GPU stalls without memory pressure remain separate issues.

## Reproduce the copy and preflight only

Obtain the author's original package from the
[2.87.6 release](https://github.com/dege-diosg/dgVoodoo2/releases/tag/2.87.6).

ZIP SHA256: `9afa493396feec7108f42a2d97661eef96bbaac5addf0dfb5cfbc3001cd76a0a`

x86 D3D9.dll SHA256: `060cb161eb7a0c608a520185f0b334db325ad71d69016bc26060c9d5b9651f7d`

```powershell
python tools/d3d11/adapters.py
python tools/d3d11/prepare.py --source work/existing-native-test-copy --output work/new-d3d11-copy --archive work/dgVoodoo2_87_6.zip --adapter 1
work/new-d3d11-copy/Runtime/python/python.exe -B tools/d3d11/launch.py --bundle work/new-d3d11-copy --archive work/dgVoodoo2_87_6.zip --preflight --adapter 1
```

Preparation requires a full-inventory-verified native-D3D9 test bundle and a
new, non-overlapping output. It copies files, excluding LocalData/profiles.
It pins the official package/DLL, builds the guarded graphics-panel lock and
verifies exactly three inventory differences: D3D9.dll, dgVoodoo.conf and
Interface.bba. This stage reproduces the research input, not a usable release.
Preflight subsequently removes the known failing wrapper through the journal,
rechecks critical engine/launch files and checks the temporary local service.
It does not start the game or update the shared native preferences file.

Omitting `--adapter` opens the combined dialog. DXGI enumerates names and
one-based ordinals without hardcoded hardware. Cancel starts no game or service.
An outer graphics lock spans the dialog and launcher lifecycle; the existing
bundle session lock and running-S7 checks prevent deployment over loaded files.
Reparse points and unknown manifests/files are rejected. Runtime code comes
from the fingerprinted own copy, not the concurrently edited common launcher.

For future qualified gameplay, only allowlisted native [Quality]/[Effects]
keys in the redirected Documents/Options.ini are changed. Unrelated sections
and encoding survive, concurrent edits cause refusal and the original is backed
up privately. This preferences file is shared by S7 copies on the same PC;
preflight and today's blocked gameplay path leave it untouched.

No system GPU preference, registry, firewall or security setting is changed.
No game executable, account cache, game data or third-party runtime is committed.
The early D3D11 adapter ordinal changed the content ID, another reason this
experiment must not be treated as a cross-PC LAN upgrade. No admission guard
is bypassed and no multiplayer/simulation-sync result is claimed.

## Verification and open work

```powershell
python -B -m unittest discover -s tools/d3d11 -p 'test_*.py' -v
```

28 automated tests cover isolated copying, corruption/link rejection, settings
encoding/preservation, unsupported values, AA separation, restartable fallback,
foreign intermediate refusal, owned-child monitoring, unqualified-start refusal,
fragmentation thresholds, persistent warning/recovery, scan caching/time budgets
and avoidance of per-poll disk writes. They do not establish in-game stability.
The host regressions cover memory pressure with a healthy game address space,
separate RAM/commit startup refusal before Popen, page-to-byte conversion and
sticky host-pressure recovery. The actual Windows API read succeeded without
starting a game; low-memory cases were injected in tests rather than consuming
the user's RAM.
The pinned archive/DLL and original EXE/LAA flag were checked directly.

Next qualification must use a controlled foreground run with trustworthy frame
intervals and CPU/GPU/host-memory evidence, then real map loading and longer
gameplay. Re-enable no backend on the strength of a menu screenshot or a zero
exit code. Optional post-processing and FSR remain later work.

### Subsequent isolated DXVK comparison

After the main chat released the shared test slot, the pinned DXVK 3.1.1 profile
was installed into this same isolated copy. The shared profile installer passed
its full inventory checks. The separate pilot selected the discrete GPU by name,
retained the existing 60 Hz cap and used medium native settings, standard game
AA and only shadows among the optional effects. No game executable or adapter
was replaced and the public gameplay gate was not bypassed or reopened.

The menu was visibly rendered and the same **Schlacht um Tanholm** map loaded
fully with one AI, where the earlier D3D11 trials had failed. Native geometry,
buildings and the game HUD were visible. The water surfaces appeared black;
this remains an unresolved rendering defect. A successful map load does not
qualify the image or longer gameplay. The physical GPU appeared in DXVK's
selected-device section; only finding a device in its enumeration was not
treated as selection evidence.

Across 248 sentinel samples, peak private memory was 1605 MiB, minimum free
32-bit address space was 1781 MiB and the largest free region stayed at least
1741 MiB. Minimum host available RAM was 2503 MiB and remaining system commit
17350 MiB. No pressure threshold or monitor failure was recorded; the longest
measured probe was 45.307 ms. The own launcher later recorded game exit 0.
These aggregate values include startup/background/shutdown; they are not an
all-foreground benchmark and provide no displayed-FPS or zero-stutter claim.
A separate 20-second passive probe had 20 samples, 11 foreground, and median
process use of 1.058 core equivalents. Its networking command-turn counter
remained zero; no simulation-progress or working multiplayer claim follows
from the rendered map/HUD.

The user initially reported a further game/PC hang, then clarified that the
most recent blocking main-menu window may have belonged to the other chat's
test. **The system block cannot be attributed to this DXVK run from that report.**
It also cannot be excluded solely from these memory counters. Keep the native
main-menu and DXVK map observations separate; capture failures and game hangs
are not interchangeable evidence. The live interaction was stopped with the
physical Escape key; no further UI action was taken afterward.

DXVK logged no shader compilation error and no error-level record for this
run, only its built-in limiter warning. That does not establish shader
correctness. Black river-shaped surfaces are consistent with a water rendering
path issue; missing source texture files are not established. Compare water
quality alone first in a future controlled restart, then investigate one shader
or depth-compatibility setting at a time. No such fix was applied in this run.
The pilot's finally block restored its allowlisted native graphics settings
when the current values still matched the test profile. Private observations
remain under work/d3d11-review and are not distributed or committed.

### Native VSync and menu-world isolation

Following the user's explicit cross-chat coordination request, graphics/CPU
tests were serialized with the main chat. No other S7 instance or heavy build
was scheduled during these measurements. The graphics copy retained its known
older four-physical-core adapter and process mask 0x55. It did not receive the
new network adapters or service builds.

Static inspection of the exact game/engine images verified the presence-only
`-vSynch` startup parameter through the native D3D9 presentation-parameter
construction: false selects IMMEDIATE, true selects ONE. Use `-vSynch` without
an additional `1`. Microsoft's
[D3DPRESENT documentation](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dpresent)
defines ONE relative to the adapter refresh; this is not an arbitrary 60 FPS
limit. The host reported 144 Hz on the discrete display path and 59 Hz on the
integrated path. Native arbitrary-GPU selection remains unverified.

Two fresh native runs used 1280x720, identical medium quality/effects and
`-vSynch`, with no wrappers. Each had a 60-second warmup followed by 120
one-second passive intervals. The second added only `-noMapInMainMenu`.
No screenshots were taken; foreground activation used window metadata and
accessibility text only. Thus the scene mode was requested by the startup
parameters and traced in original scripts, rather than visually verified in
these sessions. The screen-loop counter is not displayed FPS.

| Native startup mode | Foreground intervals | Median CPU (cores) | Median screen-loop completions/s | Peak private memory (MiB) |
| --- | ---: | ---: | ---: | ---: |
| VSync, normal menu world | 120/120 | 1.5849 | 144.029 | 1081 |
| VSync, no menu world | 35/120 | 0.0890 | 143.756 | 290 |

The other 85 no-menu-world intervals were background: median CPU 0.0876 cores
and screen-loop rate 143.927/s. Foreground and background were not pooled for
the table. Neither run contained a zero-loop interval in its recorded window
or triggered memory-pressure thresholds. VSync is consistent with a 144 Hz
producer limit on this host, while CPU remained high with the menu world.
The shorter foreground subset and lack of a matched on/off/on replication
limit the comparison. These short windows do not rule out the reported longer
hangs or establish their cause. Do not compare their CPU numbers directly to
the main chat's two-core-adapter baseline as an identical A/B experiment.

The initial VSync observer attempt rejected its own overlarge code-byte read
before collecting any loop samples. Verification was corrected to use chunks
of at most four bytes, preserving file hashes, image sizes, loaded-module paths
and ASLR-adjusted runtime-byte guards. Only the subsequent 120 valid intervals
are reported. No game function was called and no game memory was written.

The original boot script explicitly sets its menu-map flag false when
`Opts.noMapInMainMenu` is present. This is a promising **menu resource-saving
option**, not a general game or LAN fix. Original customization scripts warn
that castle previews and some privilege/item views require the running menu
world. Their behavior and lobby admission must be checked before exposing this
as a supported option. These trials changed no release default; the later
shared-source integration is described below.

The subsequent shared-source integration exposes this exact fixed combination
as an optional **Simple main menu + VSync** test checkbox, default off for new
and legacy preferences, with English warnings. It uses only a validated local
JSON boolean and fixed game arguments, preserving content IDs and cancellation.
See [GRAPHICS-START.md](../../docs/GRAPHICS-START.md). It does not reopen this
research launcher's gameplay gate, update delivered packages, or establish
working multiplayer/long-gameplay with the new option.

Both owned games exited normally with code zero. After each run, the
allowlisted native graphics settings were verified against the pre-test backup;
after the final close, no S7 process remained. The original installation and
system display/driver settings were unchanged. The public gameplay gate stays
closed, the black-water issue remains open, and longer gameplay is unqualified.

The pinned [DXVK 3.1.1 configuration](https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf)
documents automatic pipeline lifetime tracking for 32-bit applications, which
frees pipeline libraries more aggressively. Keep it at its default for the next
comparison. Its D3D9 available-texture-memory reporting option does not raise
the game's address-space ceiling; it is not used as a memory-safety guarantee.

The author's [redistribution terms](https://dege.freeweb.hu/dgVoodoo2/ReadmeGeneral/)
permit unmodified components with a specific game/mod, require the original ZIP
for standalone hosting, and prohibit inclusion in general-purpose launchers.
This repository contains no dgVoodoo binaries or copied game/shader assets.

### Black-water isolation and shader workaround, 2026-10-28

After an explicit test-slot release, fresh isolated DXVK 3.1.1 runs used the
RX 9070 XT, 1920x1080 windowed mode, the same Tanholm map/starting camera and
one normal King Konradin AI. All used medium native settings, standard AA,
only optional shadows, the 60 Hz DXVK cap, `-vSynch` and `-noMapInMainMenu`.
Game/engine/adapter fingerprints were unchanged. The existing four-core
research adapter was retained, so these are not the main chat's network tests.

| Trial | WaterQuality | DXVK float emulation | Observed river |
| --- | ---: | --- | --- |
| Medium baseline | 1 | Auto | Black |
| Low-water comparison | 0 | Auto | Black |
| Shader comparison | 1 | Strict | Blue water, visible shoreline/reflection |
| Return to baseline | 1 | Auto | Black again |
| Integrated source launcher | 1 | Strict | Blue water, visible shoreline/reflection |

Only the fixed `d3d9.floatEmulation = Strict` child-environment override changed
between the three medium A/B/A trials. Logs confirmed the effective Strict
setting and its absence in the return-to-baseline run. This implicates shader
floating-point compatibility in this observed defect; it does not establish
the exact failing shader instruction or require replacement texture files.
No vendor/depth-format spoofing or driver setting was changed. The pinned
[upstream configuration](https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf)
describes Strict as a more accurate, potentially slower emulation mode.

The low-water, Strict and final baseline runs respectively recorded 62, 83
and 111 sentinel samples, with private-memory peaks of 1523, 1584 and 1517 MiB.
None recorded pressure or monitor failure. These different-duration windows
include loading and are not performance comparisons. The earlier medium
baseline did record a monitor failure after 45 samples; its memory aggregates
are incomplete and cannot support a safety claim. Its exact exception was not
captured. A diagnostic-only error record was added for subsequent trials;
no protection threshold or scan budget was relaxed, and no later exception was
recorded in those three runs. The first startup-only attempt was slow/white,
later responded, and was closed before a map observation; it is excluded from
the water comparison.

All of these owned games closed normally with exit zero. Allowlisted native
graphics values were restored and checked after each run. Private evidence
stays under work/d3d11-review. The shared source start helper now supplies the
fixed Strict value after verifying the active owned profile and stripping all
inherited DXVK_CONFIG aliases. It leaves the immutable installed profile and
restore transaction unchanged. Existing packages have not been rebuilt.
The final live run used that shared `launch_environment` function without a
private shader override. Its effective-configuration log confirmed Strict and
the same river visibly rendered correctly, including after dismissing the
achievement popup. Its 131 sentinel samples recorded a 1526 MiB private peak,
at least 1783 MiB free 32-bit address space, a largest free region of at least
1719 MiB, no pressure and no monitor failure. These are short-run aggregates,
not a guarantee against future memory failures. It exited zero, restored the
previous graphics values, and a host process check found no remaining S7 or
owned helper runtime. The 60 graphics profile/choice/launcher tests passed;
their fixtures verify launch/rollback boundaries, not rendered image quality.
This water workaround does not reopen the D3D11 gameplay gate, establish
working LAN/checksum synchronization, qualify other maps/GPUs or prove that
the previously reported system hangs are fixed. FSR remains unimplemented.
