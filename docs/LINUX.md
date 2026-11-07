# Native Linux tools · RC3.19 preview

RC3.19 retains RC3.17's native Linux tools and the
[RC3.18 logistics and Internet corrections](RC3.18-LOGISTICS-INTERNET.md),
and includes the matching new lighting resources. Use RC3.19 on every peer.
The new recommended profile and ReShade controls are Windows launcher features;
new Linux graphics gameplay has not been qualified. Earlier Linux gameplay
measurements below describe RC3.17; later NEO changes used local Windows clients.
Actual game qualification used Ubisoft History Edition on Ubuntu 24.04 under
WSL2/WSLg, WineHQ 11 and WineD3D. Mint, Linux Steam/Proton and hardware Vulkan
gameplay remain unqualified. See the measured results below and the
[machine-readable verification receipt](verification/linux-20261106.json).

The installer, launcher, local service and join-cryptography helper run as Linux
processes. The original 32-bit History Edition and the fingerprinted native
adapter run through Wine or Proton. No Windows Python or Windows .NET runtime
is needed. The public package contains no game installation, profile or saves.

## Requirements and launch

- Linux x86-64, Python 3.10 or newer and the distribution's Python Tk package.
  The native .NET runtime and its notices are included.
- Wine 11 or newer with its i386 runtime and classic 32-bit prefix support. New
  Wine copies use an isolated win32 prefix and initially a 1280×720 window.
  Imported graphics settings remain unchanged. The native ABI
  boundaries were verified under Wine 11. Older Wine releases are not
  qualified by this work. Obtain Wine from
  your distribution or the [official WineHQ instructions](https://gitlab.winehq.org/wine/wine/-/wikis/Debian-Ubuntu).
- Your own supported Ubisoft or Steam History Edition files and space for an
  independent copy. Steam retains its genuine client and ownership checks.
  The Proton path requires the real Linux Steam installation; live Steam/Proton
  qualification is still pending.
- WineD3D is the default renderer. Optional DXVK uses the same pinned x32 DLL
  as Windows and requires a suitable Vulkan driver. Include the distribution's
  32-bit OpenGL/EGL libraries with Wine; a working 64-bit desktop alone does
  not prove that the 32-bit renderer is available.

Extract the complete Linux tar archive onto a Linux filesystem, then run
`./WePlay.sh`. Select your supported game directory and an existing parent
directory for the new copy. The original installation is never modified.
Select the real directory rather than a symlink to a Steam library.
Under WSL, use its native Linux filesystem for the game copy. Direct PE
execution from Windows 9P/DrvFS mounts produced reproducible execute faults;
the launcher rejects these destinations before installation/startup.

For a prepared copy, its own `WePlay.sh` opens the native launcher. Run it as
your ordinary user. Only the Internet tunnel helper requests Polkit permission;
the game, launcher and game service are never elevated. Internet play requires
`pkexec`, iproute2, TUN support and a working graphical Polkit authentication agent.

CLI examples, with paths quoted:

```sh
./WePlay.sh install --source "/games/History Edition" --destination "/games/WePlay"
/games/WePlay/WePlay.sh verify
/games/WePlay/WePlay.sh runner --kind wine --executable /opt/wine-stable/bin/wine
/games/WePlay/WePlay.sh play --mode solo
/games/WePlay/WePlay.sh play --mode lan --player-name Player
/games/WePlay/WePlay.sh graphics dxvk
```

The CLI also exposes private/public Internet modes with explicit
`--allow-internet`. Private join invitations are read without echoing them or
putting them in process arguments. The desktop provides invitation copying and
public-game selection. See the qualification scope below; isolated transport
tests do not establish an actual Internet game.
CLI private hosting requires `--invitation-file` naming a new file; it is
created with mode 0600. Preserve its privacy and remove it after closing the room.

## Copies, updates and saved games

Each copy has an owned Wine/Proton prefix and private Wine home. Its document
links cannot point at an unrelated user's documents. A copy-wide nonblocking
lock prevents simultaneous launch, repair and profile migration.

For an update, select the old prepared copy as the source and a new destination.
Pristine repair inputs rebuild the executable/UI; every retained game asset is
checked. Native local saves, the stable local profile ID, player/runner settings
and recognized game documents retain their bytes and timestamps. Account
caches, the Wine registry, transient network configuration and unrelated
documents are not imported. The old copy is retained.

For backups, preserve the complete prepared copy or its complete owned game
profile. Multiplayer checkpoints need both the native save and their checked
roster index in `Profiles.xml`. Copying a single `.s7s` file alone can display
the save in the browser but cannot recreate a verified multiplayer lobby; an
incomplete checkpoint is rejected.

Move a complete private export as one folder. The launcher resolves its new
root and rebases only its own Wine document links; unexpected foreign links
stop startup. Interrupted preparations retain an incomplete marker and are not
playable. To uninstall, close its game and room, preserve any desired saves and
remove that separate copy. No system service or autostart entry is installed.

## Cross-platform file and network checks

The full local inventory still checks **Base, Server and Launcher**. The v2
network contract identifies all gameplay files under Base, together with the
explicit service/carrier protocol revision. Linux ELF helpers and Windows host
helpers can therefore differ without changing the game identity. Their local
integrity remains checked. There is no fallback to the old network ID: all
participants need RC3.19 and the same renderer/content profile.

Windows CPython uses zlib-ng 2.2.4, whose compressed streams differ from system
zlib on Linux. The Linux package therefore includes a hash-pinned native
zlib-ng compressor and its license. The wrapper also normalizes the gzip OS
byte to the verified release representation. `tools/linux/build_compressor.sh`
rebuilds it from the pinned official upstream archive; its interoperability
test uses a real Windows-generated vector.
All original, intermediate and final archive hashes remain enforced. A different
archive is rejected rather than accepted as a new Linux variant.

The native Linux EasyTier runtime is built from the same pinned source and both
reviewed patches as Windows. Its static musl target avoids Ubuntu 24.04 glibc
requirements that would exclude Mint 21.1. Receiver ACLs and default-deny
forwarding/IPv6 remain. The privileged worker uses inherited private pipes,
root-only temporary configuration and bounded heartbeat/EOF cleanup.

## Build and qualification

With the .NET 10 SDK available on Linux:

```sh
mkdir -p work
sh tools/linux/build.sh "$PWD/work/linux-build"
```

The transport additionally requires Rust stable, its
`x86_64-unknown-linux-musl` target, musl-tools, the C build tools, mold and protoc.
`tools/easytier/build-linux.sh` takes the pinned upstream source ZIP and a new
directory under work. It applies the existing patches and runs the relevant
packet-filter unit suites. Ship the matching corresponding-source archive and
all dependency notices with any distributed transport binary.
The release's [EasyTier corresponding source archive](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/v2.0.0-rc3.19/EasyTier-corresponding-source.zip)
contains both Linux and Windows build recipes and the reviewed patches.

`tools/linux/build_package.py` accepts the verified patch package, native server
publish directory and optional pinned native transport. It writes a new stage
and optionally a tar archive with explicit executable permissions. It never
uploads a release or includes a prepared game copy. The two Ubuntu CI jobs
exercise Python 3.10/3.12, native builds and the independent OpenSSL peer.

Measured evidence on 6 November 2026:

| Check | Result |
| --- | --- |
| Ubuntu 24.04 native service/protocol suite | 8,951 checks, including real HTTP/UDP and an independent OpenSSL peer |
| Windows service regression | 8,957 checks against actual BCrypt |
| Wine 11 x86 adapter boundaries | All 20 compiled synthetic suites pass in a fresh private win32 prefix; no skipped suites. Includes CPU 35, public-key/CNG 24, storage 94, connect 18, LAN membership 517 and network guard 79 checks |
| Windows native adapter regressions | 39 selected suite executions pass across ordinary, NEO and directory/NEO builds |
| Native installer and full byte-identical UI archive | Completed; updated Map Forge archive also matches the exact Windows pin |
| Linux locks, paths, profile migration, relocation, fresh installer imports and coordinator lifetime | All 20 Linux boundary tests pass; real-prefix Documents and owned home links are both covered |
| Real Linux game startup and solo world | Fresh native package installation, full verification, menu and running Forest Realm world observed under Wine 11/win32/WineD3D. An earlier fullscreen capture was black while the user could see the menu; windowed capture is usable. Independent D3D9 framebuffer/readback and visible RGB window also pass |
| Solo save/restart/load | Actual quick/automatic saves, complete game-process exit, cold menu load and continued saving pass under Wine 11 |
| Actual Windows/Linux matches | Linux host/Windows guest: 1,379 common native CRCs; reverse roles: 1,104. Zero mismatches, manual saves written; NEO on and off exercised |
| Cold multiplayer resume | Both processes restarted, roster/NEO restored, visible world and continued manual save: 1,200 common native CRCs, zero mismatches |
| Updated and relocated copy | 13 real game documents preserved through update, 15 through move, with exact bytes and nanosecond times. Cold GUI launch from a path with spaces loaded the migrated checkpoint; 4,281 common native CRCs, zero mismatches. The user reported stability until session end; Windows exited normally |
| Five-minute host autosave | Actual production slot 1001 generated with the unchanged scheduler. A repeated match produced 3,140 common native CRCs; both processes then restarted and loaded the complete save/profile through the real lobby and native transfer. Both visible worlds continued for 201 common CRCs; zero mismatches. Save and index are persisted with exact bytes/times |
| Renderer switching | Pinned DXVK install, verification and WineD3D restore passed on the real Linux copy; complete inventory and network identity returned unchanged. Vulkan gameplay was not tested |
| Coordinator lifetime | Reproduced service loss when the creating WSL process exited despite a live Windows lease. The detached Linux session fixes that trigger; real native service remained reachable for 12 lease samples and cleaned up itself after the last peer left |
| Mint 21.1 | Not run: VM is unavailable for this task |
| Managed Internet transport/privileged worker | Two isolated namespaces, six allowed services, two denied ports, EOF/heartbeat cleanup and TUN removal pass |
| Linux Steam/Proton and interactive Polkit consent | Unavailable in this environment; not qualified |

All six complete replay comparisons read every concatenated gzip member:
**11,305 common native CRCs, zero mismatches**. Five parser regressions cover
later-member mismatches, truncation and bounded decompression. From a source
checkout, run `python3 -B tests/linux/replay_checks.py --help` for the local
comparison tool; keep original replays private.

An earlier resumed-match disconnect caused a Windows access violation after
the Linux coordinator disappeared. The independently reproduced coordinator
lifetime defect is corrected as described above. Later reverse-role and extended
matches exited Windows normally; this does not promise recovery from arbitrary
service loss. Tests used one physical host with Windows and WSL, software
rendering and a bounded session. Separate-PC play, long-session endurance and
performance remain outside this qualification. Synthetic results and namespace
transport checks are separate from actual game observations.

Wine compatibility changes retain the original executable, instruction, object
and ABI guards. Wine 11's unimplemented process-group query has a narrow
single-group fallback only when complete CPU topology proves the original
affinity policy. Public-key parsing checks the exact canonical 96-character
encoding before using the basic Wine decoder, then validates the P-256 point
with real BCrypt. Windows retains those cryptographic checks as well. The HTTP
guard still forbids redirects; its Wine fixture proves that an actual local
302 target is not followed even though Wine cannot query that option back.

Run `tests/linux/wine_abi.py --wine /opt/wine-stable/bin/wine --tests
work/compiled-native --work work/linux-fixtures` after building the x86 fixtures
with `native/build.ps1 -GameDirectory -NeoLogistics`. The work directory must be
a fast Linux filesystem below repository `work/`; a successful run retains its
result/logs and removes only its generated synthetic prefix. This test never
starts the game or establishes multiplayer qualification.
