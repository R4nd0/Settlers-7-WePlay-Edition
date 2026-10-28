# Settlers 7: WePlay Edition

**The community fixes what Ubisoft left behind.**

![Settlers 7: WePlay Edition](assets/branding/weplay-logo.png)

A community LAN project for **The Settlers 7 History Edition** on Windows,
built on Eisenmonoxid's S7Patcher. Each player supplies their own supported game
installation. WePlay prepares a separate copy and keeps the original intact.

## Play together

1. Install WePlay and let it find your own S7 installation.
2. Choose your player name and graphics settings in the WePlay launcher.
3. Choose **Play on LAN**. WePlay finds the shared LAN automatically.
4. In S7, open **Multiplayer** and create or join a match. This is the only
   lobby selection; there are no IP addresses to type or extra rooms to choose.

Automatic discovery requires the same private local network and Windows private
firewall permission. Unknown game or patch versions are rejected instead of
being mixed. Developer mode is disabled in LAN games.

## Current status

**Experimental release candidate, not a guarantee of desync-free play.** Real
two-PC joins and match starts have worked with either computer hosting. A guest
command-delay bug has been corrected. A reverse-host desync remains under
investigation; three or more real clients and large endgame networks still need
qualification. Automatic discovery and the launcher do not fix simulation bugs.
For a first LAN start with different graphics profiles, start one PC first,
then the others; known renderer differences are aligned automatically. With
simultaneous first starts, an incompatible winning service may require retrying
LAN start. No disconnected second group is silently created to hide a mismatch.

- [Getting started with the desktop launcher](docs/RC2-START.md)
- [Automatic LAN, limits and verification](docs/AUTOMATIC-LAN.md)
- [Graphics choices and hardware checks](docs/GRAPHICS-START.md)
- [Two-PC verification](docs/TWO-PC-DELIVERY.md)
- [Development handoff](docs/DEVELOPMENT-HANDOFF.md)
- [Original patcher workflow and research history](docs/LEGACY-PATCHER.md)

## With love to Eisenmonoxid

Huge thanks, warm greetings and lots of love to **[Eisenmonoxid](https://github.com/Eisenmonoxid)**.
Your **[S7Patcher](https://github.com/Eisenmonoxid/S7Patcher)** and
**[S7Packer](https://github.com/Eisenmonoxid/S7Packer)** laid the groundwork for
this project. Thank you for opening up the tools, sharing the research and
helping keep this game alive. WePlay builds on your work with gratitude. ❤️

See [credits and licenses](CREDITS.md). The original MIT copyright notice is
preserved in [LICENSE](LICENSE).

## Development

Build the source with the .NET 10 SDK. The Windows launcher and preparation
installer live in `tools/desktop` and `tools/installer`. Read
[AGENTS.md](AGENTS.md) and the development handoff before changing the native
adapter. Never commit or distribute copied game files, local account data,
runtimes or private experiment outputs as source.

This repository is an independent community project. Game names and trademarks
belong to their respective owners.
