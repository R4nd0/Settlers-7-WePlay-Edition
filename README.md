# Settlers 7: WePlay Edition

**The community fixes what Ubisoft left behind.**

**RC3.27 main-package fixes:** reliable native memory measurements, Gold water
configuration and a stable Gold camera. History also allows more time for the
first local-service start. [Changes and verification](docs/RC3.27-MEMORY-GOLD.md).

One Windows installer and launcher supports Gold
1.12.1396, Ubisoft History and regular Steam History, with mandatory platform
isolation and private profiles. Updates and repairs preserve that isolation.
Gold is single player. [Main-package guide](docs/RC3.26-STANDALONE-STANDARD.md).
The published RC3.25 downloads below precede these local package updates.

## [Download Windows Installer — RC3.25 · Setup.exe](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/v2.0.0-rc3.25/Setup.exe)

Windows 10/11 x64 · English / Deutsch · Install or update an existing WePlay copy.

[Download Linux x64 package](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/v2.0.0-rc3.25/WePlay-RC3.25-Linux-x64.tar.gz) · Native Linux launcher, installer and service; original game through Wine/Proton.
Actual qualification: Ubuntu 24.04/WSL2, WineHQ 11, Ubisoft History Edition and
WineD3D. Linux Mint, genuine Linux Steam/Proton and hardware Vulkan gameplay
remain unqualified. [Linux setup and verification](docs/LINUX.md).

[Download Windows ZIP](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/v2.0.0-rc3.25/WePlay-RC3.25.zip) · [Release notes and SHA-256 checksums](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/tag/v2.0.0-rc3.25) · [Discord community](https://discord.gg/j2eQgVzssc)

**RC3.25 fixes updates of existing standalone installations.**
RC3.24 could close an older launcher and then abort during installer preflight.
The compatibility fix is verified against the shipped RC3.23 backend and by a
real RC3.23-to-RC3.25 upgrade, with game profiles and saves preserved. Existing
launchers can update directly to RC3.25.
[Hotfix notes](docs/RC3.25-STANDALONE-UPDATE.md) | [Published verification](docs/verification/rc325-publication-20261109.json).

**RC3.24 improves launcher reliability and recovery.**
Public Join/Cancel, settings Save/Discard and interrupted operations are fixed.
Recovery preserves game copies and saves with verified backups; update downloads
are cancellable and interrupted owned jobs can be cleaned up safely. Lobby
refresh avoids repeated full-package hashing. Includes all earlier graphics and
lobby corrections.
[Release notes](docs/RC3.24-LAUNCHER.md) | [Published verification](docs/verification/rc324-publication-20261109.json).

**RC3.23 keeps multiplayer graphics local and adds standalone SMAA.**
Guests no longer inherit the host's renderer. The recommended preset uses
SMAA without extra ReShade effects. Camera DOF choices, negative mip bias and
distant shadow filtering are corrected; remaining flicker is still open.
**All participants must update to RC3.23 and update their game copy.**
[Release notes and verification](docs/RC3.23-GRAPHICS-LOBBY.md).

**RC3.22 fixes graphics saving and restarting after an ordinary game exit.**
ReShade's normal writes to its generated preset no longer prevent saving or
starting again. Saved graphics choices remain authoritative; DLL, shader and
add-on checks remain enforced. Includes the Internet fixes below.
[Cause, tests and scope](docs/RC3.22-GRAPHICS-SAVE.md) ·
[Published verification](docs/verification/rc322-publication-20261108.json).

**RC3.21 fixes Windows Internet joining after a room becomes visible.**
The guest now aligns its content through the public room status before normal
admission. This removes the premature HTTP 403 without opening the protected
configuration. A real guest joined the host on another PC; both players appeared
in the native multiplayer lobby with NEO on. RC3.23 additionally preserves each
PC's renderer and requires the current game content on both PCs.
[Cause, tests and scope](docs/RC3.21-INTERNET-JOIN.md).

**RC3.20 repairs public games missing from the global lobby.**
Public hosting now publishes its joinable room correctly on Windows and Linux.
The directory server also renews active public-host credentials correctly.
Update the host, restart WePlay and create a new public game. A real native host
was displayed and selected by a second local native client through the live
HTTPS directory; encrypted join handoff passed. Separate-PC Internet gameplay
remains pending. [Fix and verification scope](docs/RC3.20-PUBLIC-LOBBY.md).

**RC3.19 adds optional graphics improvements and a bright Midsummer sun orbit.**
The Windows launcher's **WePlay recommended (Modern)** profile combines longer
detail and shadow visibility, 4096 shadows, natural tone, smoother edges and
depth contact shading. Optional 8192 Ultra shadows offer more near detail.
An additional **ReShade Maximum (cinematic)** profile adds screen-space indirect
light/color bounce, occlusion, SMAA, bloom, morning haze and daytime heat shimmer.
Light and haze cover the full image; only heat refraction protects HUD controls.
Choose Mittsommer/Midsummer under Environment in the game for the bright
twilight/day cycle without a dark night. Existing preferences are preserved.
Update every peer: the lighting resources change the content fingerprint.
[RC3.19 notes and qualification limits](docs/RC3.19-GRAPHICS.md) ·
[Measured GPU costs](docs/GRAPHICS-RC319-MEASUREMENTS.md).

**RC3.18 fixes stalled NEO workshop pickups and Internet-room readiness.**
The blue weaving mill resumes wool collection and cloth production without a
production toggle. Eligible intermediate warehouse handoffs prefer shorter total
road distance; FIFO and explicit individual priorities remain effective. Internet
rooms use a replacement relay and publish their invitation only after connection
and coordinator checks pass. Create a new invitation after updating.
[Release notes and test limits](docs/RC3.18-LOGISTICS-INTERNET.md).

RC3.17 introduced native Linux tools and Windows/Linux multiplayer. Its tests
covered both host roles, NEO on/off, cold multiplayer resume, solo saves/load,
updating and moving the Linux copy. Six complete replay pairs contain
**11,305 common native checksums, zero mismatches**. The user reported stability
until the extended match ended. A real five-minute host autosave passed a
complete-process restart, native transfer and resumed play on both clients.
A reproduced WSL coordinator-lifetime defect is corrected, with actual native
service continuity and last-peer cleanup verified. See the
[verification receipt](docs/verification/linux-20261106.json) for scope and limits.

The RC3.16 correction for lost Kartograph variant names also restores variant multiplayer
checkpoints. A native string-lifetime error made the guest load standard
6-point rules while the host required 25 points. The corrected Grassland Mesa
variant passed fresh play and a complete-process restart, six further game
minutes and another save: **1200 common native checksums, zero mismatches**.
The reported early desync itself was not reproduced; Steam/separate-PC and long
matches remain unqualified. Keep backups and identical templates on all peers.
[Cause, guards and evidence](docs/MULTIPLAYER-MAP-VARIANTS.md).

Play **The Settlers 7 History Edition** solo, on LAN, in private Internet rooms
or through the global public lobby from the WePlay launcher. WePlay prepares
its own copy and preserves your original installation. Each player supplies
their own supported game files; the public installer/ZIP contains no game data.

RC3.17 also retains the workshop-load correction and the **NEO-Logistics** map
option, restored individual priorities when loading NEO multiplayer saves, a correction for a resumed-world
AI terrain-search stall, and the replacement for the crashing in-game Invite
button. The global lobby, standalone/update recovery, multiplayer castles,
manual multiplayer saves and five-minute host autosave remain included.

## Install or update

1. Download **Setup.exe**, or fully extract the ZIP and run its Setup. Close
   WePlay and S7 before updating. Python and .NET are included.
2. For an ordinary update, choose the existing WePlay application folder.
   For a complete private standalone export, use **Select existing standalone
   copy** and select its outer folder or its **App** folder.
3. With RC3.26, select your supported Gold 1.12.1396, Ubisoft History or regular
   Steam History installation. The launcher prepares a separate isolated copy.
   An existing prepared copy can be repaired and updated from its retained
   files after the original folder has been removed.
4. Use the same current main package on each History participant's PC. Multiplayer
   requires matching NEO5 game content on all peers, including Windows peers
   and when NEO is off.

Requirements: Windows 10 build 19045 or later / Windows 11, x64, your own
supported Gold or History Edition and enough disk space for the separate game copy.
A game-content update temporarily needs room for another copy. Modern graphics
uses DXVK and requires Vulkan support; Native graphics remains available.
RC3.26 requires neither Steam nor Ubisoft Connect. The older published RC3.25
downloads above still require the Steam client for Steam History; Gold support
and the mandatory isolation contract begin with RC3.26. Gold is single player.

Automatic updates verify signed metadata and the complete installer. The
launcher offers a visible update action; checking alone does not install it.
If that handoff fails, run Setup directly. The preview's Windows executables
are not Authenticode-signed; WePlay does not disable Windows security policies.

## Choose how to play

History provides the cards below. Gold provides **Solo**, Help and Updates.

| Launcher card | How it works |
| --- | --- |
| **Solo** | Start your copy, then choose the campaign or a skirmish. |
| **LAN** | Discover the group on your trusted home network; create/join the actual match in S7 Multiplayer. |
| **Private Internet** | Create a private EasyTier room or paste an invitation. Share its invitation only with your intended players. |
| **Public Internet** | Browse all advertised games in the central public directory or host a public room. Discord is optional. |

The central directory also appears in S7's **WePlay Online** browser. Joining
a directory entry connects the relevant room; select the actual match in S7
afterwards. The public list refreshes while visible. Browsing starts no tunnel.
The host's launcher and PC must remain connected while guests use the room.

Before a new EasyTier connection, the launcher explains the virtual network,
visible addresses and remaining risks and asks for explicit consent. The managed
tunnel restricts traffic to required game/lobby ports and replies, denying other
services and forwarding. Public bootstrap/NAT traversal and, if needed, relays
help connect PCs. Direct-only mode requires a reachable endpoint and does not
work with every NAT/firewall. Windows may request permission for its adapter or
firewall changes; the game runs without elevation.

[LAN guide](docs/LAN-PLAYERS.md) · [LAN auf Deutsch](docs/LAN-SPIELER.md) ·
[Internet guide](docs/RC3-INTERNET.md) · [Public service and test limits](docs/PUBLIC-LOBBY-SERVICE.md)

## NEO-Logistics: optional per match

In S7's **map setup**, tick **NEO-Logistics** before starting a skirmish or
multiplayer match. It starts **off**; changing the map resets the choice.
The multiplayer host chooses it, the lobby displays it, and the choice stays
fixed for that match. Loading a compatible save restores its recorded policy.

With NEO enabled:

- Supported warehouse transport requests are selected by **order creation age**
  (FIFO); equal-age requests retain the native stable order.
- **Group priorities have no effect.** Their panels stay visible but their
  ranking/drag controls are disabled.
- Explicit **individual player and AI priorities remain effective** and are
  stored and restored with their task identities.
- Eligible intermediate warehouse handoffs prefer the shortest total usable road
  distance. Stock-source selection retains its native behavior.
- A production task with an unfinished input no longer blocks workshop pickup.

With NEO off, ordinary native comparison and group controls are used. This is
an experimental policy choice, **not a new measured delivery-speed guarantee**.
The earlier laboratory FIFO benchmark used a different build.

Single-player Grassland Mesa tests with three hard AIs at native x20 passed
start/save/restart/load with NEO on and off; the final on-run restored all 25
recorded individual intents exactly. Two local multiplayer clients restored
both an older four-intent save and a newer 13-intent save exactly before play.
One final resume run reached turn 1600 on both clients, saved again at turn 1200
and closed normally, with **600 common native checksums and no mismatch**.

**The reproduced turn-1200 workshop-load desync is fixed in RC3.15b.** Three
completed runs with two local Ubisoft clients and two hard AI passed: the old
failing save, another cold load of the newly written save, and an NEO-off control.
All **1750 common complete native checksums matched** (600 + 550 + 600), with
normal exits. Both peers restored all 13 or 18 saved individual intents before
their first simulation step. Only the seven invariant AI angles are initialized;
saved search work, priorities, RNG and native desync checks remain intact.
This does not generally explain older separate-PC/PC2-host desync reports.
[Cause and comparison results](docs/MULTIPLAYER-RESUME-WORKSHOP.md).
Separate-PC/Steam NEO resume, populated human
individual-priority controls and a delivery benchmark of the final policy
remain unqualified. [Full NEO behavior and evidence](docs/NEO-LOGISTICS-MAP-OPTION.md).

## Save and continue multiplayer

The host can use **Save game** in the game menu. Host autosave runs every five
minutes of game time in its own slot; manual saving does not postpone it.
After restarting, the same host selects **Multiplayer → Load game**. Other
humans join in their original slots and select **Ready**. The host waits for
**Start** to become available while the real checkpoint transfers.

Keep the host profile, save folder and save-file timestamps together. A lone
`.s7s` copied into another profile is not a supported migration. Older saves
without WePlay multiplayer metadata are unsupported. RC3.16 preserves the
Map Forge variant name and loads variant saves with their original local
template. Keep identical templates on participating PCs; missing host templates
are rejected before lobby creation. See the
[variant investigation and verification](docs/MULTIPLAYER-MAP-VARIANTS.md).
Missing or inconsistent enabled NEO state is rejected rather than replaced
with an empty priority list. Keep a backup before using an older checkpoint.

The separate AI terrain-search correction prevents the demonstrated hang from
an extreme finite angle producing enormous search bounds. A two-client/two-AI
resume run exercised the correction, passed the next autosave and produced
**3924 matching common native checksums**, with normal exits. The workshop
initialization in RC3.15b addresses the proven source of invalid workshop
rotations and the later load desync. The faster transfer-pacing prototype
remains opt-in for development; RC3.15b retains normal pacing, so a lobby wait
can still take minutes.

[Save/load guide](docs/RC3.11-MULTIPLAYER-SAVES.md) ·
[Terrain-stall correction](docs/MULTIPLAYER-RESUME-TERRAIN.md) ·
[Transfer timing](docs/MULTIPLAYER-RESUME-TRANSFER.md)

## Invitations and Discord

S7's **Invite / Einladen** button opens the launcher invitation flow instead of
the unavailable Ubisoft overlay. A private host can copy its room invitation;
a public host copies the shared directory code; LAN players see joining guidance.
Copying does not post to Discord or connect another PC automatically.
[Invitation behavior and test scope](docs/INGAME-INVITATIONS.md).

**Also publish on Discord** belongs in **Public Internet / Public lobby**.
For the optional game board, run `/weplay-hilfe` in **#🎮multiplayer**, obtain a
private launcher-linking code, paste it into **Connect to Discord**, then enable
publication. Private room invitations are not published. `/spiele` and
`/weplay-status` show board/status information. The externally hosted bot does
not require your PC to stay on; your own game room still does.
[Discord instructions](docs/DISCORD-INTEGRATION.md).

## Standalone, recovery and uninstall

A **complete private standalone export** contains your owned game copy and the
launcher/runtime. Move the **whole export**, then start **App/S7Launcher.exe**
or **WePlay-Starten.cmd**. A complete export needs no original game directory
or separately installed Python/.NET. This does not remove Steam's client
requirement. Do not confuse the public installer ZIP with a private full-game
export, and do not upload private exports, saves or profiles to GitHub.

For a failed update, close the game/launcher and rerun Setup against the same
application or standalone folder. Keep the complete folder together; do not mix
individual files from different RCs. The launcher can repair a recognized owned
copy without rediscovering the removed original. Incomplete or changed copies
are rejected safely. Previous managed content stays available until replacement
verification succeeds; busy cleanup is retried later. Never delete retained
copies or recovery inputs to force an update through.

Back up saves/profile data before uninstalling. Use Windows **Installed apps →
Settlers 7: WePlay Edition → Uninstall** for a normal installation, with WePlay
and S7 closed. Managed cleanup preserves personal/unknown files; the original
installation is untouched. Private portable exports are separate folders: retain
their saves before removing them yourself.

[Standalone details](docs/PRIVATE-STANDALONE.md) ·
[Update recovery](docs/UPDATE-RECOVERY.md) · [Graphics help](docs/GRAPHICS-START.md)

Unreleased development: [longer settler/tree detail and visibility, higher-resolution dynamic shadows and extended shadow range](docs/GRAPHICS-LOD-SHADOWS.md).

Unreleased development: [Mittsommer lighting without a dark night and optional gentle ReShade tone](docs/MIDSUMMER-LIGHTING.md).

Unreleased development: [smoother edges, Ultra shadows, natural lighting and geometry contact shading](docs/VISUAL-QUALITY.md).

## Changelog and compatibility

| Release | Main additions |
| --- | --- |
| **RC3.16** | Preserve Kartograph template names and reload variant multiplayer checkpoints; local 25-point cold-load test passed. |
| RC3.15b | Correctly initialize restored workshop-AI search rotations; reproduced load desync fixed, including repeated cold load and NEO-off control. |
| RC3.15 | Optional NEO policy and saved-priority restoration, terrain-stall guard and invitation replacement; its original assets predate the workshop fix. |
| RC3.14 | Global public lobby, four launcher play cards, direct standalone launcher binding and transactional standalone/update recovery. |
| RC3.13 | Public directory, EasyTier isolation and explicit connection consent. |
| RC3.12 | Independent copy repair/maintenance and taller launcher menus. |
| RC3.11 | Multiplayer saves/resume and five-minute host autosave. |
| RC3.7–3.10 | Multiplayer castles, lobby-alliance correction and logistics crash fix. |

Use matching builds on all peers and restart both games before switching hosts.
The RC3.26 Windows main package supports fingerprinted Gold 1.12.1396,
Ubisoft History and Steam History builds; other executables are rejected.
Existing Steam/Ubisoft tests do not qualify every
new NEO feature. Same-PC tests do not establish physical-PC LAN, Internet or
extended/endgame stability. Native aggregate checksums cover selected simulation
state, not every byte. [RC3.16 notes](docs/RC3.16-MAP-VARIANTS.md) ·
[Steam scope](docs/STEAM-COMPATIBILITY.md).

## Credits and licenses

With love and thanks to **[Eisenmonoxid](https://github.com/Eisenmonoxid)**:
**[S7Patcher](https://github.com/Eisenmonoxid/S7Patcher)** and
**[S7Packer](https://github.com/Eisenmonoxid/S7Packer)** laid the groundwork. ❤️

[MIT license](LICENSE) · [Credits and third-party notices](CREDITS.md).
NEO's save reader includes Mark Adler's puff 2.3 (zlib 1.3.1); puff and S7Packer
notices are included in the ZIP's **Licenses** folder and installed package.
Python, .NET, DXVK and EasyTier retain their licenses/notices. Matching EasyTier
corresponding source accompanies the release. **SHA256SUMS** and per-asset hash
files are available on the release; signed update metadata authenticates Setup.

Public maintainer: **R4nd0 / WePlay Community**. This independent community project
does not own the game's names or trademarks. Each player supplies a supported copy.

<details>
<summary>Development and verification</summary>

[Development](docs/DEVELOPMENT.md) · [Native adapter](docs/NATIVE-LOCAL-START.md) ·
[Signed updates/publishing](docs/UPDATES.md) · [Windows helpers](docs/WINDOWS-HELPERS.md).
Source builds use .NET 10 and the documented packaging toolchain. Game data,
runtimes, private configurations, credentials and test recordings stay outside Git.

</details>
