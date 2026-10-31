# Private Ubisoft standalone copy

The supported Ubisoft Connect History Edition can be prepared as a private,
portable WePlay copy. The export includes the game, release launcher, Python,
.NET and the existing local adapter. The adapter loads no Ubisoft launcher DLL.
This export is a private backup containing proprietary game files; it must never
be uploaded as a public WePlay release asset.

Copy the **entire exported folder** to a writable local folder on the other
Windows PC, then run **WePlay-Starten.cmd**. No Setup or second source-game
installation is needed for this already prepared copy. Windows x64 10 build
19045 or newer is required. LAN/Internet setup may still request the normal
Windows network permission. Use the same WePlay version on all players' PCs.

The first start verifies the game and bundled runtimes, binds the launcher to
the new absolute location, probes graphics on that PC and opens the usual
launcher. It never follows the exporting PC's old absolute game path. Each PC
should start from the pristine export, which contains no player identity,
account cache, Discord pairing, network room, saves or selected hardware.
Saves created after first use remain intact when relocating the whole folder.

Always close WePlay and S7 before copying or moving. Use WePlay-Starten.cmd
again after moving. If a subsequent managed update has prepared a different
game instance, create a fresh export before another relocation. Launcher-only
updates can reuse the game; a future update that changes game content may
require selecting the original supported game source again.

## Build locally

Use a clean, unpacked release App with no State or Instances directory and
the original supported Ubisoft game as source:

```powershell
python -B tools/installer/standalone.py --build --app <release-App> --source <original-game> --output outputs/<new-private-folder>
```

The destination must not exist. Only the separate destination is modified.
The existing installer retains executable/archive hashes, payload checks and
native ABI guards. The release App and its Package are copied unchanged; the
relocation helper lives outside them. An incomplete export has no published
starter and retains EXPORT-INCOMPLETE.txt.

`WePlay-Starten.cmd` uses the bundled Python and desktop .NET runtime, so it does
not rely on a system Python/.NET installation. For verification without opening
the launcher, use that Python to run `standalone.py --root <export> --check-only`.
This check initializes the local binding and is therefore run on a separate
verification copy, rather than the pristine distributable backup.

Fixture tests cover relocation with a stale absolute path, preservation of
saves, graphics selection, corrupted game/runtime rejection, package mismatch
and refusal to adopt external managed storage. These checks do not establish
startup on a second physical PC or multiplayer synchronization.
