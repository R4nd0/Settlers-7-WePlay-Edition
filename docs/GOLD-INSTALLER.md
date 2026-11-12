# Gold single-player installer

The dedicated `WePlay-Gold-Setup.exe` installs the verified Gold quiet interface
and complete single-player campaign selection automatically. It is separate
from the existing History desktop installer and uses its own application ID,
directory, shortcut and native startup adapter. No game assets are distributed.

Select an unchanged Ubisoft Gold 1.12.1396 installation (or its S7Gold parent)
and a new WePlay destination. Setup copies only game Data, retains the original
EXE and Interface.bba in Backup, applies the guarded offline executable changes,
and builds the cleaned interface from the recipient's own original archive.
Both twelve-mission base campaign selection and the five Chronicles missions
are available without rewriting saved mission completion. Menu behavior is
documented in [Gold interface cleanup](GOLD-MENU-CLEANUP.md).

The Start Menu shortcut **WePlay Gold** launches the copied game using the
bundled Python x64 runtime. A desktop shortcut is optional. The starter verifies
the package and installed content, starts the owned x86 process suspended,
explicitly calls GoldInitialize through the guarded entry bridge, verifies the
startup-options No bytes and restores the original entry instruction. The
native adapter supplies the path-derived Documents/WePlay/Gold profile and
Gold's existing isolated mutex; original Gold/History profiles are not adopted.
Fresh local-profile setup remains part of the game's normal first start.

The pinned DXVK runtime is required. There is no renderer fallback; the native
adapter retains its renderer-file guards. Startup requires 4 GiB free physical
RAM and uses at most two allowed physical CPU cores, one logical CPU per core.
Only a child that fails before native initialization may be terminated, after
its PID/parent/path/hash ownership is verified. A running game in the selected
app directory blocks installer preparation; any running S7 game blocks an
additional game start. Existing Game destinations are never reused
or overwritten. Failed preparation remains in an unpublished staging directory.

This package exposes the local single-player startup path. It does not ship the
experimental account/peer services or grant Gold/History multiplayer admission.
Uninstall removes the installer-owned application files and shortcuts; the game
copy and isolated saved games are retained.

## Build

Use the exact already verified Gold LocalSimulation adapter, pinned official
CPython embeddable ZIP, DXVK DLL/license and a local Inno Setup compiler:

```console
python -B tools/gold/build_installer.py --stage work/gold-installer --output outputs/WePlay-Gold --adapter path/to/weplay_gold_startup.dll --python-zip path/to/python-3.14.10-embed-amd64.zip --dxvk path/to/d3d9.dll --dxvk-license path/to/LICENSE --compiler path/to/ISCC.exe
```

No download, original installation modification or game launch occurs during
build. The embedded interpreter imports and verifies the staged package before
compilation. The output contains Setup, its SHA-256 and concise installation
instructions. Sources, native adapter, renderer, definitions and runtime remain
inventoried separately from the recipient's game files.

The ordinary source command `tools/gold/prepare.py prepare` also installs the
clean interface/full campaign selection by default. It still creates no starter
and does not activate the native adapter; use Gold Setup for the complete flow.

Verified with 50 fixture tests and an actual fresh installation from unchanged
Gold files. The final Setup rejected an existing game and overlapping paths
before writing package files, and reported a failed preparation with exit code 2
without publishing a Game directory. Its installed starter reached the clean
main menu, the final base campaign mission and Rebel Realm with a fresh isolated
profile. The game exited normally; all 16 shared original options/profile/save
files retained their hashes and modification times. Uninstall removed the
runtime/starter while retaining the game manifest and unchanged isolated profile.
No world or multiplayer session was started in this installer acceptance test.

Evidence: [Gold installer receipt](verification/gold-installer-20261112.json).
