# RC3.14 — Global Lobby and Standalone Updates

The launcher now presents four distinct ways to play: **Solo**, **LAN**,
**Private Internet** and **Public Internet**, with recognizable symbols and
separate controls. Both Internet paths use the isolated EasyTier transport.

## Global public lobby

- The central WePlay directory is configured automatically. Browse games in the
  launcher or through **WePlay Online** inside S7. The visible launcher list
  refreshes every 15 seconds and preserves the selected game.
- Hosting and joining public games no longer require a Discord account or a
  linking code. Joining obtains access to that game's room; select the actual
  match inside S7 after connecting.
- **Also publish on Discord** belongs in Public Internet and remains optional.
  Linking Discord initially leaves publication off. Private Internet contains
  invitation sharing and does not publish games automatically.
- The service uses its neutral community address. Previous Discord publisher
  links were revoked during migration; use `/weplay-hilfe` for a fresh private
  linking code if you want optional Discord publication.
- Browsing starts no tunnel. Each new room connection still requires explicit
  EasyTier consent; game-port isolation and forwarding restrictions remain.

## Existing installations and standalone copies

- Setup recognizes complete standalone exports through their outer folder or
  App folder and updates them without the original installation or system Python.
  Close S7 and WePlay first; choose **Select existing standalone copy** if needed.
- Complete packages are staged and hash-verified before replacement. Interrupted
  package swaps and paired installation/standalone metadata can recover on retry.
- Failed preparations track their owned files; updates reserve room for archive,
  repair and save migration. Unknown or modified files remain protected.
- Complete exports remain movable with contained old-version cleanup pending.
  Starting **App/S7Launcher.exe** recognizes the bundled game before setup.

## Downloads and verification

Use **Setup.exe** or unpack **WePlay-RC3.14.zip** and run its Setup. The public ZIP
contains the installer, notes, signed update manifest and EasyTier corresponding
source; it contains no commercial game files. A first installation needs your
supported History Edition. Existing complete standalone copies need no original.
Standard Steam copies retain their existing Steam-client requirement.

Qualified before release: source regressions, bilingual launcher/layout checks,
package/signature checks, and standalone update/recovery tests. Two real local
Ubisoft game clients found and joined one another using the production directory
and encrypted join broker, with two pinned EasyTier processes.

That multiplayer check used same-PC native loopback and EasyTier without TUN.
It does **not** qualify two remote PCs, full Internet gameplay, a new Steam live
test or long-match/desync stability. Native game and EasyTier binaries are
unchanged. Earlier multiplayer save/autosave and crash fixes are retained.
The installer/helpers have no trusted Windows publisher signature.

See [service test limits](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/main/docs/PUBLIC-LOBBY-SERVICE.md),
[update recovery](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/main/docs/UPDATE-RECOVERY.md)
and [launcher flows](https://github.com/R4nd0/Settlers-7-WePlay-Edition/blob/main/docs/LAUNCHER-LOBBY-FLOWS.md).
