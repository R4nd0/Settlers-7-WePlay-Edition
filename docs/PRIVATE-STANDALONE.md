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

## Private launcher fix candidate, 2026-10-31

A recipient reported "Ubisoft Game Launcher error code 1" after the local
adapter had logged successful startup. The Ubisoft executable also contains a
legacy Orbit client: its initializer reads the old launcher registration and
loads ubiorbitapi_r2.dll. Missing registration produces legacy error 1, which
the later facade constructor treats as fatal independently of UPLAY_Start.

The candidate retains an explicitly unavailable legacy API and null session
instead of initializing that obsolete client. A separate guarded constructor
predicate permits precisely that unavailable state after the verified local
bootstrap. The original legacy session check remains unchanged. No Ubisoft
registry entry, launcher DLL, credentials, fake session or save handle is
created. Three exact byte guards apply only to the Ubisoft profile with the
local connect probe; Steam uses its existing path. Hash, ABI, owner-thread and
rollback checks remain enabled.

Candidate DLL SHA-256:
`e6a8e204296a32f53358ab6f4e7e93d3a0df0b78a09e4edec9994399bb12df63`.
Synthetic validation passes 1,338 bootstrap, 708 Steam-profile, 94 platform,
78 network-guard, 48 castle-profile, 160 castle-wire, 24 CPU and 18 connect
checks. A live game start with this candidate remains unverified; the user
stopped computer operation before that check and then requested packaging.

The private standalone package pins its new DLL, installer-tool hash, bundle,
content manifest and launcher package ID together. It includes matching native
source and a qualification note. Use the same corrected copy on every player's
PC. Published RC3.7.1 assets, version/update sequence and release payload pins
are unchanged; this is not a published release or a claim of second-PC startup.
