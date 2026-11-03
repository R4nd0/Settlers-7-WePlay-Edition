# Private History Edition standalone copy

The supported Ubisoft Connect and Steam History Editions can be prepared as a private,
portable WePlay copy. The export includes the game, release launcher, Python,
.NET and the existing local adapter. The adapter loads no Ubisoft launcher DLL.
This export is a private backup containing proprietary game files; it must never
be uploaded as a public WePlay release asset.

Copy the **entire exported folder** to a writable local folder on the other
Windows PC, then run **App/S7Launcher.exe** or **WePlay-Starten.cmd**. No Setup or second source-game
installation is needed for this already prepared copy. Windows x64 10 build
19045 or newer is required. LAN/Internet setup may still request the normal
Windows network permission. Use the same WePlay version on all players' PCs.

The first start verifies the game and bundled runtimes, binds the launcher to
the new absolute location, probes graphics on that PC and opens the usual
launcher. It never follows the exporting PC's old absolute game path. Each PC
should start from the pristine export, which contains no player identity,
account cache, Discord pairing, network room, saves or selected hardware.
Saves created after first use remain intact when relocating the whole folder.
The Steam edition still requires the installed, running Steam client and its
normal ownership check. The export does not replace the Steam SDK.

Always close WePlay and S7 before copying or moving. Use WePlay-Starten.cmd
again after moving. Starting App/S7Launcher.exe directly also performs the
binding from RC3.13 onward. Earlier exports require WePlay-Starten.cmd before
opening the EXE; their direct EXE start can incorrectly show initial setup.
RC3.13 checks the bundled instance before showing the launcher, including after
a whole-folder move. An incomplete or inconsistent export shows a standalone
error instead of requesting an original installation. Re-extract the complete
private backup to recover it; do not run the public Setup inside the export.

## Independent maintenance (RC3.12)

### Manual Setup and portable folders

**Unreleased installer changes:** future Setup builds show the destination
folder and a dedicated **Select existing standalone copy** button. They detect
an export next to Setup, its remembered application directory, or an existing
WePlay desktop/Start-menu shortcut that points to a portable copy. Other copies
can be selected by their outer folder or their App folder. Explicit `/DIR`
arguments from the signed updater take precedence; no drive-wide search runs.

Before replacing launcher files, Setup uses the old copy's bundled Python and
the current guarded adoption helper to validate and bind that copy. This also
handles an export that has never been started on this PC. A missing runtime,
incomplete export or failed integrity check stops installation before replacing
files. Setup refreshes the outer standalone helper and remembers the chosen
portable App directory. The launcher can then rebuild its game from its owned
copy and RepairSource, preserving saves and settings without the original.

All four EasyTier license/notice files are included in the pinned runtime
manifest. Earlier RC3.13 preparation omitted them from the manifest; updating
over RC3.12 left those old files behind and the strict inventory check rejected
the result. Future packages retain and verify these notices rather than
loosening the integrity check.

These changes are in development; the published RC3.13 Setup is unchanged.
Further source hardening gives a parallel-operation preflight a distinct busy
exit code, so Setup reports that an update is already using the copy rather than
suggesting it is incomplete. Runtime validation also requires all four notices
independently of the supplied lock-file inventory, preventing accidental omission
from being redefined as a valid runtime. These follow-ups have source-only checks;
they have not been deployed to running copies.
Verification passed with a complete prior RC3.12 private export: the actual
installer selected and adopted its initially unbound copy before replacement,
then the launcher backend rebuilt the owned game with original discovery
forbidden. Synthetic save data, player name and local profile ID were retained;
the export descriptor advanced and a fresh bundled-Python backend reported the
updated copy ready. The missing-runtime counterexample stopped before copying.
All 74 focused regressions passed. These were installation/content checks,
without launching a world or proving that a saved match resumes correctly.
See the [sanitized verification record](verification/standalone-installer-update-20261103.json).

RC3.13's separately launched Setup reuses a registered installation directory;
it does not automatically find an unregistered private standalone export and
its directory page is hidden. Running it from Downloads can therefore install
another launcher rather than update the portable copy. Prefer **Updates →
Install update** in the standalone launcher: its signed updater explicitly
passes that copy's exact `App` directory to Setup. Start an old unbound export
through WePlay-Starten.cmd first, or apply the matching launcher-start fix.

For a manual offline update, close WePlay and S7 and explicitly select the
portable application directory using Setup's directory argument, for example:

```text
Setup.exe "/DIR=D:\Games\WePlay-Standalone\App"
```

The target is the export's **App** folder, not its outer folder and not the
original game installation. Keep the whole export, including standalone.json,
Instances and RepairSource. A newly prepared complete private ZIP can instead
be extracted into a new folder and started directly, without running Setup.

After successful preparation, the original Ubisoft or Steam game installation
is no longer needed by WePlay. Detection selects the owned WePlay instance;
repair and game-content updates prepare a replacement from that instance.
The Steam client and its normal ownership check remain required for Steam.
This maintenance support starts with RC3.12; RC3.11 predates the change.

New copies retain the exact original executable and interface archive under
`RepairSource`, with an inventory of the retained game assets. Repairs rebuild
the interface, adapter, service, launcher helpers and runtimes from the verified
package and local inputs. They preserve saves and compatible player settings,
publish the verified replacement before cleaning up the old instance, and update
the private export descriptor so a subsequent whole-folder move still works.

Existing copies can migrate using their `ArchiveStage` backup. The patcher keeps
the original archive payload and directory; recovery reconstructs its header and
requires the exact supported original SHA-256 before patching. An absent or
modified backup is rejected. Missing or damaged unchanged game assets still need
a healthy private backup or original source; the public patch package contains
no replacement game assets. Never remove `RepairSource` from a prepared copy.

The launcher starts with a taller window and expands to fit the selected menu,
bounded by the current monitor's usable height. Scrolling remains available on
small screens or after manually reducing the window size.

Verification: the final focused installer/backend/export suite passed 90 tests,
including source removal for both editions, generated-file repair, save
preservation, relocation after repair, legacy archive recovery and corrupted
backup rejection. A broader regression run passed 184 tests with 19 optional
platform checks skipped. Bounded content checks using both actual production
executables, the Steam API and the real interface archive also passed detection,
package-change repair and save-file preservation with original-source access
forbidden. These were content checks; no new game was launched. Two attempted
full-size copy checks were stopped because of slow file I/O and do not count as
completed verification.
The final launcher self-test passed both languages and its private IPC check;
all six menus fit without vertical overflow at the tested 1,376-pixel available
window height. The minimum-window fallback also rendered successfully.

When a complete newer standalone ZIP is unpacked over the same contained
instance, the helper verifies its full supplied game/runtime/payload before
refreshing the package binding. A stale package ID alone no longer asks for
the source installation. A known DXVK profile left by an earlier extraction is
preserved as a local backup and replaced only after checking the new native
baseline and exact renderer hashes; interrupted recovery can resume. Unknown
renderer changes and external managed instances remain rejected.

## Build locally

Use a clean, unpacked release App with no State or Instances directory and
the original supported Ubisoft or Steam game as source:

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
Synthetic validation passed 1,338 bootstrap, 708 Steam-profile, 94 platform,
78 network-guard, 48 castle-profile, 160 castle-wire, 24 CPU and 18 connect
checks. A live game start with this candidate remains unverified; the user
stopped computer operation before that check and then requested packaging.

The private standalone package pins its new DLL, installer-tool hash, bundle,
content manifest and launcher package ID together. It includes matching native
source and a qualification note. Use the same corrected copy on every player's
PC. Published RC3.7.1 assets, version/update sequence and release payload pins
are unchanged; this is not a published release or a claim of second-PC startup.

## Next-RC integration

Desktop packages now include the verified export helper and declare the strict
boolean `standalone_export` capability. The unavailable-legacy-API path also
has independently mapped Steam callsites, globals and original byte guards.
Both editions retain owner-thread, object, executable fingerprint and rollback
checks. Steam continues through its genuine SDK/ownership path.

The source pins and next-RC test package use adapter SHA-256
`44e48d22dfcc0d1f807c1ad82c2ae313962bca91d834daa35795eae853068289`.
This also includes the [castle consistency correction](MULTIPLAYER-CASTLES.md#desync-follow-up-2026-10-31).
Bootstrap/ABI checks pass 1,360; Steam-profile checks pass 732. Steam export
and adoption, existing-archive overlays, recovery interruption, corruption and
relocation have synthetic coverage. The existing public RC3.7.1 release and
the earlier private full ZIP are separate artifacts; updating these source
pins does not publish or silently replace either one.

The combined candidate reached the real Ubisoft main menu and connected local
login, with DXVK and the two-core safeguard. Its owned process loaded the
expected local adapter, reported both legacy-unavailable predicates, and loaded
no Orbit module. It exited normally with code 0; the memory sentinel reported
no pressure or monitoring failure. This tests the native startup path, not
relocation to another physical PC or a running multiplayer match.

Steam likewise reached the main menu, connected local login and an isolated
Fiery Chasm lobby with two AI slots. The original Steam API, expected adapter
and DXVK were loaded, with no Orbit module. It exited normally with code 0.
The engine refused to start the match without a second human client. The
Steam sentinel recorded memory pressure and a monitoring failure, so that run
does not establish clean memory behavior or multiplayer stability.

The RC3.8 follow-up subsequently completed two real, fresh mixed-edition local
matches with each edition hosting, beyond turns 3489 and 2858 respectively.
The complete loaded castle values matched on both clients. See the
[two-client qualification](MULTIPLAYER-CASTLES.md#rc38-two-client-qualification-2026-10-31)
for the laboratory isolation, memory-monitor failures and remaining limits.
