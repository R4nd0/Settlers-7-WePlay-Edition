# S7Packer 1.2 — map archive tools

Eisenmonoxid's [S7Packer](https://github.com/Eisenmonoxid/S7Packer) unpacks and
creates Settlers 7 `.bba` archives, including `Maps.bba`. Release **1.2** contains
the **Enable Map Editing** commit from 2026-11-15:
[`2ccf4cb8f2d24013841a28b587a6052dad1ab88b`](https://github.com/Eisenmonoxid/S7Packer/commit/2ccf4cb8f2d24013841a28b587a6052dad1ab88b).

The complete `Source/`, project file, icon, README and MIT license are retained
unchanged in [upstream/](upstream/). [upstream.json](upstream.json) records the
revision, source hashes and official Windows release checksum. Upstream CI and
Git configuration are omitted.

## Windows executable

Download [Windows-x64.zip from release 1.2](https://github.com/Eisenmonoxid/S7Packer/releases/download/1.2/Windows-x64.zip)
and extract it to `outputs/S7Packer-1.2/`. The official self-contained executable
does not need an installed .NET SDK. Include [LICENSE.txt](upstream/LICENSE.txt)
and the [upstream README](upstream/README.md) when sharing it.

Verify the ZIP before extracting:

```powershell
Get-FileHash ./Windows-x64.zip -Algorithm SHA256
# f387b1c7fe74ac0dbee6394b6d7ff6e04a41dd7ca3932a027e3dc442acca953c
```

## Unpack, edit, repack

From the repository root, with a **copy** of the archive under `work/map-edit/`:

```powershell
./outputs/S7Packer-1.2/S7Packer.exe ./work/map-edit/Maps.bba
# Creates work/map-edit/Maps_Extracted/ next to the input archive.

# Edit the extracted files, then create a NEW archive:
./outputs/S7Packer-1.2/S7Packer.exe ./work/map-edit/Maps_Extracted/ ./work/map-edit/Maps-edited.bba
```

Keep archive outputs outside the directory being packed. S7Packer can create a
new archive without an existing template and supports adding and removing files.
It is an archive tool; editing the extracted map data requires a suitable editor
for each file format. A graphical terrain editor is not included.

Run the upstream program in a real terminal: its initial `Console.Clear()` fails
when output is redirected. It pauses for a keypress after each operation. For
automated checks, use a pseudoterminal and inspect its success message and
resulting files: failures can still return exit code zero. Use a fresh extraction
directory for each comparison.

## Build from the retained source

With a .NET 11 SDK and the native AOT build prerequisites for your platform:

```powershell
dotnet publish ./tools/s7packer/upstream/S7Packer.csproj -c Release -r win-x64 -p:UseNet11=true -o ./outputs/S7Packer-1.2-source-build
```

This follows the upstream release configuration, including System.IO.Hashing
11.0.0. The source project also exposes a .NET 10 target. The downloaded official
Windows binary is the executable used for the local round-trip verification.

## Integration boundary and verification

This is a standalone developer tool. The launcher's existing BBA replacement
engine and installer archive fingerprints are unchanged. Do not automatically
deploy repacked maps into installed games or mixed multiplayer content sets.

Local verification on 2026-11-19: the official ZIP SHA-256 matches GitHub's asset
digest, and the retained source files match the pinned Git commit. Synthetic
pack/unpack and edit/add/remove/repack checks are recorded in
[verification.json](verification.json), including an independent BBA reader
check. These are archive checks, not in-game map-loading or multiplayer results.

No game archives, extracted game files or executables are committed here.
