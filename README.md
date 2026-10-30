# Settlers 7: WePlay Edition

**The community fixes what Ubisoft left behind.**

Play **The Settlers 7 History Edition** locally, with friends on your home network,
or in private Internet rooms — from one English/German Windows launcher.
WePlay detects your supported game installation, prepares a separate copy and
keeps the original intact. Each player needs their own game installation.

**[Download RC3.5 — Setup.exe](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/v2.0.0-rc3.5/Setup.exe)** · [Release notes and checksum](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/tag/v2.0.0-rc3.5) · [Join WePlay on Discord](https://discord.gg/j2eQgVzssc)

![WePlay RC3.5 launcher in English](docs/screenshots/launcher-en.png)

*English launcher preview, rendered from the packaged RC3.5 application with demonstration data.*

## What you can do

| Feature | Available in RC3.5 |
| --- | --- |
| Local play | Start your prepared game with remembered graphics settings and player name. |
| LAN multiplayer | Discover your LAN group automatically; create or join the actual match inside S7. |
| Internet rooms | Create a private room or join by invitation, using integrated EasyTier. No separate VPN installation, EasyTier account or rented WePlay server is needed. |
| Discord community | Find players, share room invitations and optionally report your public games to the board in #🎮multiplayer. |
| Graphics | Native or Modern (DXVK) graphics, GPU/display selection where supported, window modes and quality settings. |
| Updates | Automatic checks at launcher startup, a visible version/install button and a manual Updates tab. Downloads are verified against a signed manifest. |
| Windows integration | Built-in helpers for discovery, firewall configuration, update installation and cleanup; no PowerShell scripts or execution-policy changes in normal use. |
| Achievement notifications | Repeated achievement popups are suppressed; the achievement catalogue and rewards remain available. |

## Install or upgrade

1. Download **Setup.exe** from the RC3.5 release. Close S7 and WePlay before upgrading.
2. Run Setup; for an upgrade, use your existing WePlay application folder.
3. Select your original supported History Edition when asked and let WePlay prepare or update its separate game copy.
4. Install the **same RC3.5 build on every player's PC** before playing together.

You need Windows x64 (Windows 10 22H2/build 19045 or newer, including Windows 11),
your own supported History Edition and space for a separate game copy.
Python and .NET are bundled. Modern graphics requires a compatible Vulkan GPU;
Native graphics is also available.

Saves and settings are preserved during managed updates. New releases appear
automatically when you open the launcher; install them with **Install update**.
Older launchers need this Setup once. If RC3.4's old update handoff is blocked by
your script policy, run RC3.5 Setup directly — **do not change the policy**.

Windows asks for administrator consent when network adapters or firewall rules
need changing; the game runs without elevation. This preview's installer and
helpers do not yet carry a trusted Windows publisher signature. Windows may
warn or block them; WePlay does not disable SmartScreen, App Control or other
security policies.

## Play over the Internet

A WePlay room connects the PCs; the actual match is created afterwards inside
S7. Discord is optional and does not distribute private room invitations.
For the guided development interface, see the [launcher walkthrough (Deutsch)](docs/LAUNCHER-GUIDE.md).

1. Everyone opens **Internet play** and enters their player name.
2. The creator selects **Create room and start** and approves the Windows network permission prompt.
3. Use **Copy invitation**, or **Copy for Discord** to include joining instructions. Share that private invitation only with your intended players.
4. Friends paste it into **Paste your private invitation** and choose **Join room and start**.
5. Inside S7, open **Multiplayer**. One player creates the match and the others join it there.

Keep **Private session** off so the other room members can find the match.
That setting alone does not publish anything on Discord.

The default **No own server · public-assisted** mode uses public EasyTier nodes
for discovery/NAT traversal. Traffic connects directly where possible and may
use a relay when needed. **Direct connections only** avoids public bootstrap,
STUN and relay nodes, but needs a reachable numeric endpoint and will not work
with every NAT/firewall combination.

**Keep the creator PC and WePlay connected while guests use the room.** If the
creator finishes their own game first, WePlay keeps the room open for active
guests. **Close Internet room** closes an idle room. During play, closing the
launcher window keeps it in the system tray.

[Internet room guide](docs/RC3-INTERNET.md)

## Discord: find players and show public games

Join **[the WePlay Discord server](https://discord.gg/j2eQgVzssc)**. The launcher
opens this community with **WePlay on Discord** and suggests invitation code
`j2eQgVzssc`. The community link and a private game-room invitation are different
things. **Copy for Discord** copies a room invitation and instructions; you
choose where to paste/send it.

For the optional automatic game board:

1. In **#🎮multiplayer**, run **`/weplay-hilfe`** and choose **Launcher verbinden**.
2. Paste the private connection code into **Connect to Discord** in the launcher's Internet tab. This code is not a bot token.
3. Review **Automatically show my public games in #🎮multiplayer** before launch. Connecting initially enables it; you can turn it off or disconnect.

Connected launchers can report their own public-game titles, player counts/free
slots and open/full or start/loading signals. Private games and room invitations
are not published. A start/loading signal is not confirmation of an ongoing
simulation. **`/spiele`**, **`/weplay-status`** and **`/weplay-hilfe`** provide the
bot's game/status/help commands. Listings depend on connected launchers and the
external service; a Discord outage does not stop your game.

**The bot runs externally — your PC does not have to stay on to host the bot.**
The creator's PC still needs to stay connected for its actual Internet room.
[Discord setup and listing details](docs/DISCORD-INTEGRATION.md)

## Play on your home network

Connect all PCs to the same trusted private LAN. Open **LAN play**, enter a name
and select **Start LAN play** on each PC. Allow private LAN access if Windows
asks. In S7 **Multiplayer**, one player creates the match and the others join it;
Private game is locked off for LAN creation.

Use matching versions. If graphics profiles differ, start one PC first, then
the others so supported differences can be aligned. If no shared group appears,
check private LAN permission on each PC.

[LAN player guide: English](docs/LAN-PLAYERS.md) · [Deutsch](docs/LAN-SPIELER.md) · [Graphics help](docs/GRAPHICS-START.md)

## Current status

**RC3.5 is an experimental preview.** Two-PC LAN joins and match starts have
worked with either PC hosting. The owner reported **two three-hour sessions
without an observed desync**; the report did not specify the build, map or
transport. This is not a guarantee of desync-free play or qualification of all
Internet connections, three-or-more-player matches or large endgame networks.

RC3.5's Windows helpers, signed updater, real scoped firewall/adapter lifecycle,
separate-copy preparation and save-preserving cleanup have been tested. Those
checks do not establish multiplayer simulation stability.

## Credits

With love and thanks to **[Eisenmonoxid](https://github.com/Eisenmonoxid)**:
your **[S7Patcher](https://github.com/Eisenmonoxid/S7Patcher)** and
**[S7Packer](https://github.com/Eisenmonoxid/S7Packer)** laid the groundwork for
WePlay. ❤️ [Credits and licenses](CREDITS.md) · [MIT license](LICENSE)

<details>
<summary>Technical documentation and development</summary>

[Development and verification](docs/DEVELOPMENT.md) ·
[Windows helpers](docs/WINDOWS-HELPERS.md) ·
[Signed updates and publishing](docs/UPDATES.md) ·
[Native adapter](docs/NATIVE-LOCAL-START.md) ·
[Research and original patcher](docs/LEGACY-PATCHER.md)

Source builds use the .NET 10 SDK and the documented packaging toolchain.
Copied games, runtimes, credentials, agent journals and build outputs are not
source files and must not be committed. EasyTier corresponding source and build
instructions accompany the release.

</details>

This is an independent community project. Game names and trademarks belong to
their respective owners. Each player supplies their own supported game copy.
