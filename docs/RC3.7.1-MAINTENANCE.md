# WePlay RC3.7.1 — Clear version, less disk clutter

RC3.7 brought your customized castles back to multiplayer. RC3.7.1 tidies up
the launcher around it: the right version on screen, and fewer old game copies
on your drive. The community keeps fixing what Ubisoft left behind.

## Correct version display

The RC3.7 launcher mistakenly displayed an old “RC3.5” label. Its actual build
was RC3.7. The header now reads the compiled program version directly, so this
release correctly displays **2.0.0-rc3.7.1** in both languages.

## Automatic cleanup of older game copies

- The existing WePlay storage location is reused. Selecting an existing owned
  game copy resolves to its storage container, avoiding nested copies.
- After a new copy has been prepared and verified, supported settings/saves
  transferred and the active installation switched successfully, WePlay cleans
  up unchanged game files belonging to its older managed copies.
- On startup, an already current and verified active game can be reused while
  old copies are cleaned up. Updating from RC3.7 does not require another full
  copy just to enable this launcher maintenance fix.
- Saves, personal files, unknown files and modified files are kept. Some small
  old folders can therefore remain. No original game installation is cleaned.
- Busy or unsafe copies remain tracked for retry. Close older games and use
  **Help → Remove older versions** if cleanup has been deferred.
- A failed preparation leaves the old active game intact. A new preparation
  still temporarily needs space for one additional full copy during the safe
  switch; this is not an in-place overwrite or a post-match rollback system.

## Install and update

Close S7 and WePlay, run **Setup.exe**, and upgrade the existing WePlay application
folder. Signed update checks also offer this release to RC3.7 and older supported
launchers. Existing supported settings and saves stay in the managed update flow.
Use the same release on all participating PCs.

Version **2.0.0-rc3.7.1**, preview update sequence **6**. The native adapter,
matchmaking service, DXVK payload and network revision **7** are unchanged from
RC3.7. Castle Restoration remains included; its [verification and limits](RC3.7-CASTLES.md)
still apply. Known desyncs remain open. This maintenance release introduces no
new multiplayer stability claim or new live-match qualification.

The release contains Setup.exe, its SHA-256 checksum, matching cryptographically
signed weplay-update.json and EasyTier corresponding source. The installer and
helpers still lack a trusted Windows Authenticode publisher signature; the signed
update manifest authenticates the download. No game files or private data ship.

## Deutsch

RC3.7.1 zeigt endlich die richtige Programmversion statt des alten RC3.5-Texts.
Nach erfolgreicher Einrichtung einer neuen Spielkopie werden unveränderte alte
WePlay-Spieldateien automatisch bereinigt. Auch bereits vorhandene Altversionen
können beim Launcherstart nach Prüfung der aktiven Kopie bereinigt werden,
ohne dafür eine weitere Vollkopie anzulegen.

Spielstände, eigene und veränderte Dateien bleiben erhalten. Belegte Kopien werden
später erneut berücksichtigt. Für eine neue Spielkopie wird während des sicheren
Wechsels vorübergehend weiterhin zusätzlicher Platz benötigt. RC3.7 und seine
Downloads bleiben unverändert; dieses Update hat eine eigene Version und Signatur.

Grüße, Dank und Liebe an [Eisenmonoxid und S7Patcher](https://github.com/Eisenmonoxid/S7Patcher)
für die großartige Vorarbeit!
