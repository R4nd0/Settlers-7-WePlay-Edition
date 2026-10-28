# D3D11 experiment: gameplay blocked pending memory and menu fixes

Updated 2026-10-28. This is an isolated research tool, not a qualified graphics
upgrade. **The starter currently refuses every game start.** `--preflight` can
validate the copy and service without starting a game or writing the shared
Documents/Options.ini. There is no command-line override for the qualification
gate. No renderer from this experiment belongs in the LAN release yet.

The D3D11 wrapper repeatedly failed a real map load with Out of memory. The
native D3D9 fallback has not passed gameplay qualification either: the user
reported another menu hang and system overload after stopping the live checks.
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

The next comparison is DXVK on this same isolated copy, at medium native
settings, with an explicit discrete GPU and the existing 60 Hz cap. The shared
profile installer verifies the entire inventory before changing files; the
first inventory pass was stopped before profile creation to avoid disk
contention with the main chat's two-client test. No new DXVK game was started,
and no new menu/map result is claimed. A private bounded pilot is prepared under
work/d3d11-review; the public gameplay gate remains closed.

The pinned [DXVK 3.1.1 configuration](https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf)
documents automatic pipeline lifetime tracking for 32-bit applications, which
frees pipeline libraries more aggressively. Keep it at its default for the next
comparison. Its D3D9 available-texture-memory reporting option does not raise
the game's address-space ceiling; it is not used as a memory-safety guarantee.

The author's [redistribution terms](https://dege.freeweb.hu/dgVoodoo2/ReadmeGeneral/)
permit unmodified components with a specific game/mod, require the original ZIP
for standalone hosting, and prohibit inclusion in general-purpose launchers.
This repository contains no dgVoodoo binaries or copied game/shader assets.
