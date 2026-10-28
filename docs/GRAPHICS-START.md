# Per-PC graphics choice and optional compatibility profile

The shared local and LAN launchers show the graphics dialog before starting
the service or game. Cancelling starts neither. Check-only runs do not show
it. The existing launcher session lock is held through selection and gameplay.

`graphics_choice.py` invokes `Choose-Graphics.ps1` in a hidden PowerShell host.
The visible WinForms dialog lists names from `Win32_VideoController.Name`
without vendor-specific lists or device/account identifiers. The previous
choice is preselected. Removed cards fall back to automatic; duplicate model
names represent a model group, not a proven selection between identical cards.

Only `LocalData/graphics-choice.json` stores the choice. Selecting different
models on two PCs changes neither game files nor their compatibility ID.
The game receives a new child environment; the service and global environment
are unchanged. Source/package and installed-copy layouts use the same selector.

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
