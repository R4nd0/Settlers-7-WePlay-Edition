# Settlers 7: WePlay Edition

**[Windows x64 — rc4.03](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/rc4.03/Setup.exe)** · **[Linux x64 — rc4.01](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/rc4.01/WePlay-rc4.01-Linux-x64.tar.gz)**

[Windows release notes](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/tag/rc4.03) · [Windows SHA-256 checksums](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/rc4.03/SHA256SUMS) · [Discord](https://discord.gg/j2eQgVzssc)

WePlay is a community launcher for **The Settlers 7 Gold Edition 1.12.1396, Ubisoft History Edition and Steam History Edition**. One Windows installer prepares a separate game copy with its own profile. You supply the original game files.

## Install or update

### Windows

1. Close WePlay and the game, then run **Setup.exe**.
2. For an update, select your existing WePlay folder. For a standalone export, select the entire export folder or its **App** folder.
3. In WePlay, select your original game or existing WePlay copy. Use **Install game** or **Update / repair**. If setup reports a long path, use **Change…** to select a shorter writable location. **Help / diagnostics** is available on the setup screen.

Requires Windows 10 build 19045 or newer, or Windows 11, on x64. DXVK is required and needs a Vulkan-capable graphics driver. Allow about 7 GB for each game copy and extra space during repair.

### Linux

Extract the Linux archive to a Linux filesystem, then run `./WePlay.sh`.
Choose your original Ubisoft or Steam History Edition or an existing WePlay
copy, and prepare a new folder. Install Wine 11 with 32-bit support and a
32-bit Vulkan driver for DXVK. Linux multiplayer uses matching Linux copies.
[Linux setup and compatibility](docs/LINUX.md)

## Play

- **Solo** for the campaign and skirmishes.
- **Multiplayer** over LAN or EasyTier, with private invitations and a public lobby. **Mixed multiplayer** across Gold, Ubisoft History and Steam History is available as a preview. Use the same WePlay version on every PC.
- **Graphics** through DXVK, with modern presets and optional ReShade effects.

[LAN guide](docs/LAN-PLAYERS.md) · [Internet multiplayer](docs/RC3-INTERNET.md) · [Graphics help](docs/GRAPHICS-START.md) · [Multiplayer saves](docs/RC3.11-MULTIPLAYER-SAVES.md)

**Modding:** [S7Packer 1.2](tools/s7packer/README.md) unpacks and rebuilds `.bba`
archives, including maps, for editing the contained files.

## Deutsch

Mit **[Setup.exe für rc4.03](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/rc4.03/Setup.exe)** installierst oder aktualisierst du WePlay für **Gold Edition 1.12.1396, Ubisoft History Edition und Steam History Edition**. WePlay verwendet deine Originaldateien, um eine getrennte Spielkopie mit eigenem Profil anzulegen.

Schließe WePlay und das Spiel vor dem Update. Wähle im Installer den bisherigen WePlay-Ordner oder den vollständigen Standalone-Export. Im Launcher kannst du die vorhandene Kopie über **Aktualisieren / reparieren** übernehmen. Bei einem zu langen Zielpfad wähle über **Ändern…** einen kürzeren Ordner. **Hilfe / Diagnose** steht direkt in der Einrichtung.

Benötigt werden Windows 10 ab Build 19045 oder Windows 11 (x64), ein Vulkan-fähiger Grafiktreiber und rund 7 GB Speicherplatz für die getrennte Kopie. Bei einer Reparatur wird vorübergehend mehr Platz benötigt.

[LAN auf Deutsch](docs/LAN-SPIELER.md) · [Update und Wiederherstellung](docs/UPDATE-RECOVERY.md)

**Linux:** Das [rc4.01-Linux-Paket](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/rc4.01/WePlay-rc4.01-Linux-x64.tar.gz) entpacken und `./WePlay.sh` starten. Es unterstützt History Edition unter Wine oder Proton. Die gemischten Gold-/History-Räume sind eine Windows-Funktion. [Linux-Anleitung](docs/LINUX.md)

## Credits

[Release archive](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases)

Thanks to **[Eisenmonoxid](https://github.com/Eisenmonoxid)** for [S7Patcher](https://github.com/Eisenmonoxid/S7Patcher) and [S7Packer](https://github.com/Eisenmonoxid/S7Packer). An independent community project maintained by **R4nd0 / WePlay Community**. [Credits and third-party notices](CREDITS.md) · [MIT license](LICENSE)
