# Per-PC graphics choice and optional compatibility profile

The optional landscape-clarity presets and 16× D3D9 texture filtering, including
the matched real-game comparison and rollback, are documented in
[DXVK-QUALITY.md](DXVK-QUALITY.md). They preserve the existing cap, water fix,
GPU/output choice, simple menu and CPU/resource rules.

The shared local and LAN launchers show the graphics dialog before starting
the service or game. Cancelling starts neither. Check-only runs do not show
it. The existing launcher session lock is held through selection and gameplay.
The shared dialog and the separate full-quality research dialog now use English
labels, choices, hints and buttons. Hardware model names are retained verbatim.

`graphics_choice.py` invokes `Choose-Graphics.ps1` in a hidden PowerShell host.
The visible WinForms dialog lists names from `Win32_VideoController.Name`
without vendor-specific lists or device/account identifiers. The previous
choice is preselected. Removed cards fall back to automatic; duplicate model
names represent a model group, not a proven selection between identical cards.

Only `LocalData/graphics-choice.json` stores the choice. Selecting different
models on two PCs changes neither game files nor their compatibility ID.
The game receives a new child environment; the service and global environment
are unchanged. Source/package and installed-copy layouts use the same selector.

## Optional simple-menu test

The English graphics dialog includes **Simple main menu + VSync (lower CPU
usage, experimental)**. It is unchecked for fresh and legacy preferences.
An explicitly saved choice is preselected next time; Cancel neither changes
that record nor starts a service/game.

The existing `s7-graphics-choice-v1` record accepts an optional `simple_menu`
JSON boolean. Records without it mean false. Integers, strings, nulls, unknown
fields, duplicate JSON keys and arbitrary argument lists are rejected. Under
the existing launcher session lock, the validated flag yields only the fixed
game arguments `-vSynch -noMapInMainMenu`, or no extra arguments when false.
Inherited environment variables cannot enable this option. GPU model strings
are never placed in the game command line. Services/coordinators receive no
menu arguments, and local preferences do not alter the compatibility ID.

VSync is coupled explicitly because the measured low-CPU mode included it.
Native VSync follows the display refresh; it is not an arbitrary 60 FPS cap.
The dialog warns that the menu landscape and some castle/content previews are
disabled, and that longer matches and LAN with this option remain unverified.
This is not a new release default or a desync/gameplay-stability fix.

Two isolated native 1280x720 trials with the same four-core adapter, medium
quality and VSync measured median foreground CPU use of 1.5849 cores with the
menu world (120 foreground intervals) and 0.0890 without it (35 foreground
intervals, another 85 background). Peak private bytes were 1081 and 290 MiB.
The shorter foreground subset and absence of on/off/on replication limit the
comparison. Counter-only sessions took no screenshots and measured screen-loop
completions, not displayed FPS. Details and the later black-water workaround are in
[the graphics research notes](../tools/d3d11/README.md).

Automated checks cover legacy/default/true/false records, strict booleans,
cancellation, content-ID preservation, and exact argument routing in local,
manual-LAN and automatic-LAN launchers. The actual English native/DXVK/full
dialogs were rendered with fixture data for layout review without game starts.
These checks do not replace a real lobby/party test with the option enabled.
Earlier delivered r3 packages and installed test copies have not been updated
with this experimental option; the new graphics RC is described below.

## Display controls in the external menu

The shared English dialog now includes **Target display**, **Window mode**
(Windowed/Fullscreen (borderless)), and **Resolution**. **Use current game display settings**
keeps legacy behavior and is selected for older preferences. Unchecking it
enables an explicit selection, saved locally for the next launch. Cancel
persists nothing and starts no children.

The optional `display` record contains only a public GDI display name, numeric
width/height and one of the two fixed mode words. Windows supplies current
attached displays and their advertised 32-bit resolutions. Dimensions/pixel
count are bounded to at most 4K pixel area to limit render-target pressure in
the 32-bit game; this does not guarantee sufficient memory. No desktop display
mode, registry value or game archive is written by the selector. Fixed native
startup flags carry resolution and target position to the game. Both explicit
modes omit `-fullScreen`: the exact game's boolean parser always enables that
option when present, even with a following `false`. When needed, startup clears only
`Quality/FullScreen` in the shared game Options.ini. It preserves encoding,
comments and unrelated settings; duplicate/unknown values, running games and
concurrent file changes prevent a write. The setting remains the user's game
default. Legacy/current-game-settings selection does not read or change it.
Only the game receives these flags; existing launchers already isolate its
argument list from services and coordinators.

The old engine's primary/secondary adapter restriction limits the selector to
the first two attached displays in its primary-first GDI list. These are display
choices, separate from physical GPU filtering. A concrete DXVK GPU must match
the selected display's reported adapter; otherwise choose Automatic or the
connected GPU. Removed displays and unsupported resolutions are rejected again
immediately before launch. Missing display information permits keeping the
current game settings.

Borderless uses the selected display's current desktop resolution; its resolution
field is read-only. It does not upscale a smaller render target. The launcher
checks the original game hash, original Popen handle, PID and exact executable
path, then removes the frame and positions only that child's eligible top-level
game window. Startup dialogs and minimized windows are excluded. A background
worker checks ownership immediately before each change and makes no repeated
placement writes when the frame/bounds already match. It uses per-monitor DPI
coordinates, does not activate the window, and stops on child exit, target
removal/movement/resolution change or an API error. Failure leaves the game
running and prints a short message. Local, manual-LAN and automatic-LAN starters
close the controller after waiting for their own game. No desktop display mode
or primary-display setting is changed by this feature.

Classic exclusive fullscreen is deliberately absent from explicit selection:
on PC1/DXVK 3.1.1, requesting the physical primary DISPLAY3 at 1920x1080 instead
changed DISPLAY1 to primary and rendered there. The desktop restored after
normal exit, but that run failed target selection. In contrast, windowed
1280x720 visibly rendered on both physical displays, at captured window origins
647/360 and 3207/360. A separate 1920x1080 run started with saved FullScreen=1;
the source preparation cleared it and DXVK reported Windowed:true at 1920x1080.
That run exited 0 with temporary settings restored; no final screenshot was
captured for it. These short menu trials are not long-match or native-backend
qualification.

The source controller subsequently placed a real DXVK 2560x1440 window at 0/0
without a frame on DISPLAY3; the captured menu covered exactly that display.
The desktop stayed DISPLAY3 primary at 0/0 and DISPLAY1 secondary at 2560/0,
both 2560x1440. The user confirmed borderless output on screen 2 after stopping
Computer Use before its final capture. Both controllers recorded one placement,
no failure, regular game exit 0 and restored temporary quality/preferences.
The private memory observers reported no pressure or monitor failure in these
short runs (peak private memory 439/441 MiB). The second display's visual result
is user-reported, not a captured observation by the agent.

New preparation packages mark `display_selection: true` and include the display
helpers. Older packages without that optional capability remain accepted by
the installer. The new graphics RC can bundle the pinned upstream DLL/license
and package-level activation/restore commands. Activation verifies the package,
upstream hashes and matching newly installed launcher helpers before invoking
the existing locked/journaled profile transaction. It downloads nothing and
refuses older prepared copies instead of modifying their launcher files.
The current RC instructions are in [RC-GRAPHICS.md](RC-GRAPHICS.md). Previously
delivered r3 packages and existing copied launchers remain unchanged.

## Backend support

The optional DXVK profile supports explicit model selection using the chosen
device name. It also sets a 60 Hz producer-rate cap and keeps logs/cache under
LocalData. No hardware model is fixed in source. A model name alone does not
prove Vulkan feature/driver support. Unsupported devices still need a compatible
backend; Windows' device list is not a Vulkan capability test.

The original D3D9 backend remains automatic. Its `-screenNumber` option is
screen selection and is not an arbitrary GPU selector: static research found
primary/nonprimary handling that passes only adapter0/1 in the normal HAL path.
The UI does not pretend a DXVK filter can control this native backend. A
separate D3D11 compatibility implementation is being tested and can extend the
same selection boundary. Do not silently overwrite another D3D9 wrapper.

## Reversible DXVK experiment

`graphics_profile.py install --bundle <prepared-copy> --dll-source <x32-d3d9.dll>`
accepts only the pinned original DXVK3.1.1 32-bit DLL. `restore --bundle` removes
only the recorded profile and restores the exact previous compatibility
manifest. Existing unknown DLLs/configurations are rejected. Neither action
downloads files or touches the original game installation/system libraries.

Installation/restoration requires closed games, the session lock, executable
and launcher manifests, full file inventory, backups and a persistent recovery
journal. Startup checks the owned profile and established launch files without
rehashing all game archives. A graphics-profile installation changes the
compatibility ID and must not inherit a prior multiplayer/stability claim.
Both LAN peers still need compatible installed files; per-PC GPU choice is
separate from installing a graphics backend.

The current source launcher also supplies the fixed child-only setting
`d3d9.floatEmulation = Strict`. On PC1, a controlled Tanholm comparison showed
black water with DXVK's automatic setting, visible blue water with Strict,
and black water again after returning to Auto. Lowering WaterQuality alone
did not help. This is a shader-compatibility workaround for the observed case;
it does not replace textures or qualify other GPUs/maps, longer gameplay or
performance. Upstream's
[pinned configuration](https://github.com/doitsujin/dxvk/blob/v3.1.1/dxvk.conf)
documents Strict as more accurate and potentially slower.

This fixed value replaces inherited DXVK_CONFIG aliases only in the child's
environment. The recorded DLL/configuration, compatibility ID, restore journal
and parent environment are unchanged. Older packaged launchers do not gain
the workaround merely by having the same DXVK DLL installed; the updated
launcher source must be included. Previously delivered packages are unchanged.

The upstream standalone release archive is
[DXVK3.1.1](https://github.com/doitsujin/dxvk/releases/tag/v3.1.1), archive SHA256
`40565b4a724aadc4433fa4e010b4b23916d9b1f1baeee64e17186db94f54e608`;
x32/d3d9.dll SHA256
`265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa`.
Upstream uses the zlib/libpng license. No DLL is stored in Git. Windows support
and hardware requirements remain those of upstream; this experimental option
is not a guarantee for every older GPU. See upstream's
[Windows notes](https://github.com/doitsujin/dxvk/wiki/Windows) and
[configuration documentation](https://github.com/doitsujin/dxvk/wiki/Configuration).

## Evidence on PC1, 2026-10-28

The native D3D9 main menu was measured for eight minutes, with480/480 foreground
samples, the two-core mask0x5 and valid frame CSV in all four windows. The
collectors reported no lost ETW events or present-buffer overflows. There were
118 intervals over50ms, six over1s; window medians for process CPU rose from
0.919 to1.428 core equivalents. This reproduces repeated hangs, not one outlier.

A separate four-minute run added only private aggregate QPC timings around
existing network-pump stages. It reproduced frames up to1.818s, while the
longest whole-pump call was989us across4158 calls. The forced Echo maximum was
260us. This excludes these measured pump calls as the source of those long
frame intervals; it does not prove that all networking is free of defects.
The diagnostic adapter was subsequently removed from the CPU test copy.

The first DXVK run selected the integrated Radeon and produced about28Hz.
It was rejected as a useful performance comparison. With the discrete GPU
explicitly selected and the60Hz cap, all480/480 foreground samples in another
eight-minute run remained at window CPU medians0.733,0.734,0.751,0.781 cores.
The user independently reported that the main menu now looked smooth.

PresentMon did not provide a valid frame CSV for the discrete Vulkan path and
reported lost ETW events. Therefore no measured zero-stutter count, exact FPS
or displayed-frame claim is made for that run. The observer now reports event
loss explicitly and marks incomplete captures. Logical process I/O and page
fault counters were added; neither is a direct physical-disk counter. Z: is
a local HDD and can affect streaming, but this does not establish disk I/O as
the cause of the earlier issue, which was also reported before migration.

The real LAN desync remains a separate unresolved issue. This menu improvement
does not establish endgame stability or synchronized simulation checksums.
