# Settlers 7: WePlay Edition

**The community fixes what Ubisoft left behind.**

![Settlers 7: WePlay Edition](assets/branding/weplay-logo.png)

A community LAN project for **The Settlers 7 History Edition** on Windows,
built on Eisenmonoxid's S7Patcher. Each player supplies their own supported game
installation. WePlay prepares a separate copy and keeps the original intact.

**[Download RC2.1 — Setup.exe](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/v2.0.0-rc2.1/Setup.exe)** · [Release notes and checksum](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/tag/v2.0.0-rc2.1)

One installer, automatic game detection, one English/German launcher for local
play, LAN and graphics. The destination-change repair error is corrected; Private game is now locked
off in LAN creation. Everyone must update their prepared copy to RC2.1.
This is an experimental RC; see the release notes for the remaining live-test
and multiplayer limits.

## Play together

1. Install the same current RC2.1 on every PC connected to the same trusted LAN.
2. For the first test, select Modern graphics (DXVK) on every supported PC.
3. On one PC, enter a name under LAN play and click Start LAN play. Wait for S7.
4. Start LAN play on the other PCs. Allow private LAN access where required.
5. In S7 Multiplayer, one player creates the match with **Private game automatically locked off**,
   and the others join it there.
   This is the only match selection; there are no IP addresses or extra rooms.

**Player guide: [English](docs/LAN-PLAYERS.md) · [Deutsch](docs/LAN-SPIELER.md)**

Discovery finds a shared local service; the actual match is chosen inside S7.
Native/Modern profile differences are guarded and known variants can be aligned
before starting. Different renderers have not been established as the cause of
the known desync. See the player guide for matching rules and troubleshooting.

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
- [RC2.1 checkbox verification](docs/RC2.1-QA.md)
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
