# Settlers 7: WePlay Edition

**The community fixes what Ubisoft left behind.**

## [Download Installer — RC3.15 · Setup.exe](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/v2.0.0-rc3.15/Setup.exe)

Windows 10/11 x64 · English / Deutsch · Install or update an existing WePlay copy.

[Download ZIP](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/v2.0.0-rc3.15/WePlay-RC3.15.zip) · [Release notes and SHA-256 checksums](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/tag/v2.0.0-rc3.15) · [Discord community](https://discord.gg/j2eQgVzssc)

**RC3.15 still contains a later multiplayer-resume desync, including with NEO
disabled. Its cause has now been identified and corrected in newer source:
[uninitialized AI workshop rotations](docs/MULTIPLAYER-RESUME-WORKSHOP.md).
The RC3.15 downloads above do not contain that correction. Keep separate backups
of important saves and their host profile.**

Play **The Settlers 7 History Edition** solo, on LAN, in private Internet rooms
or through the global public lobby from one Windows launcher. WePlay prepares
its own copy and preserves your original installation. Each player supplies
their own supported game files; the public installer/ZIP contains no game data.

RC3.15 includes the new **NEO-Logistics** map option, restored individual
priorities when loading NEO multiplayer saves, a correction for a resumed-world
AI terrain-search stall, and the replacement for the crashing in-game Invite
button. The global lobby, standalone/update recovery, multiplayer castles,
manual multiplayer saves and five-minute host autosave remain included.

## Install or update

1. Download **Setup.exe**, or fully extract the ZIP and run its Setup. Close
   WePlay and S7 before updating. Python and .NET are included.
2. For an ordinary update, choose the existing WePlay application folder.
   For a complete private standalone export, use **Select existing standalone
   copy** and select its outer folder or its **App** folder.
3. For a first installation, select your supported Ubisoft Connect/Uplay or
   Steam History Edition installation. The launcher prepares a separate copy.
   An existing prepared copy can be repaired and updated from its retained
   files after the original folder has been removed.
4. Install the **same RC3.15 build on every participant's PC**, even when NEO
   is off. Its NEO3 session data is incompatible with older adapters.

Requirements: Windows 10 build 19045 or later / Windows 11, x64, your own
supported History Edition and enough disk space for the separate game copy.
A game-content update temporarily needs room for another copy. Modern graphics
uses DXVK and requires Vulkan support; Native graphics remains available.
The supported Steam edition still requires its running Steam client.

Automatic updates verify signed metadata and the complete installer. The
launcher offers a visible update action; checking alone does not install it.
If that handoff fails, run Setup directly. The preview's Windows executables
are not Authenticode-signed; WePlay does not disable Windows security policies.

## Choose how to play

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
- Production scheduling and pathfinding otherwise retain their native behavior.

With NEO off, ordinary native comparison and group controls are used. This is
an experimental policy choice, **not a new measured delivery-speed guarantee**.
The earlier laboratory FIFO benchmark used a different build.

Single-player Grassland Mesa tests with three hard AIs at native x20 passed
start/save/restart/load with NEO on and off; the final on-run restored all 25
recorded individual intents exactly. Two local multiplayer clients restored
both an older four-intent save and a newer 13-intent save exactly before play.
One final resume run reached turn 1600 on both clients, saved again at turn 1200
and closed normally, with **600 common native checksums and no mismatch**.

**That turn-1200 desync is corrected in source after RC3.15.** The workshop AI
used an uninitialized rotation table after load, including with NEO disabled.
The fixed source passed the previously failing checkpoint and a second cold
reload, with 600 and 550 matching common checksums and normal exits. The
RC3.15 downloads still contain the old adapter.
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
without WePlay multiplayer metadata and Map Forge templates are unsupported.
Missing or inconsistent enabled NEO state is rejected rather than replaced
with an empty priority list. Keep a backup before using an older checkpoint.

The separate AI terrain-search correction prevents the demonstrated hang from
an extreme finite angle producing enormous search bounds. A two-client/two-AI
resume run exercised the correction, passed the next autosave and produced
**3924 matching common native checksums**, with normal exits. This is distinct
from the later NEO checkpoint desync above. The faster transfer-pacing prototype
remains opt-in for development; RC3.15 retains normal pacing, so a lobby wait
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

## Changelog and compatibility

| Release | Main additions |
| --- | --- |
| **RC3.15** | Optional NEO map policy and saved-priority restoration; AI terrain-stall fix; in-game invitation replacement; known later multiplayer reload desync documented. |
| RC3.14 | Global public lobby, four launcher play cards, direct standalone launcher binding and transactional standalone/update recovery. |
| RC3.13 | Public directory, EasyTier isolation and explicit connection consent. |
| RC3.12 | Independent copy repair/maintenance and taller launcher menus. |
| RC3.11 | Multiplayer saves/resume and five-minute host autosave. |
| RC3.7–3.10 | Multiplayer castles, lobby-alliance correction and logistics crash fix. |

Use matching builds on all peers and restart both games before switching hosts.
WePlay supports the fingerprinted Ubisoft Connect/Uplay and Steam History builds;
other executables are rejected. Existing Steam/Ubisoft tests do not qualify every
new NEO feature. Same-PC tests do not establish physical-PC LAN, Internet or
extended/endgame stability. Native aggregate checksums cover selected simulation
state, not every byte. [RC3.15 notes](docs/RC3.15-NEO-LOGISTICS.md) ·
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
