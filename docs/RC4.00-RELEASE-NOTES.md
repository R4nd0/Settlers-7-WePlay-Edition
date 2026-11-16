# The Settlers 7: WePlay Edition — RC4.00

> **ENTWURF — NICHT VERÖFFENTLICHT.** Veröffentlichung erst nach echtem
> gemischtem Sitzungsreload Gold ↔ Steam History und Gold ↔ Ubisoft History,
> Integration in die Produktionsadapter und Prüfung des fertigen Pakets.
> Die nachfolgende Matrix beschreibt den bisherigen Produktionsstand und wird
> nach diesem Nachweis überarbeitet. Downloadziele sind für den Release reserviert.

> **Verbindliches RC4.00-Ziel:** Ein Gold-Host spielt mit genau einem Gast aus
> Steam History oder Ubisoft History auf Monhaim Valley mit zwei Menschen und
> zwei nativen KIs. Der gemischte Speicher-/Ladepfad muss mit beiden Ausgaben aus
> demselben final installierten Paket belegt sein. Ein Solo-Paket wird nicht als
> RC4.00 veröffentlicht. Signierung und Veröffentlichung bleiben bis zu diesen
> Nachweisen gesperrt. [Bindung an den finalen Installer](RC4-RELOAD-RELEASE-GATE.md).

> **Aktualisierter Entwicklungsnachweis:** Ein neuer Host-Checkpoint und sein
> zweiter gemischter Reload sind inzwischen im privaten, bewachten Native162-
> Kandidaten gegen beide Original-History-Ausgaben nachgewiesen: Steam 1.824,
> Ubisoft 2.220 gemeinsame originale CRC-Blöcke ohne Abweichung, alle vier
> gespeicherten NEO-Marken wiederhergestellt und regulär geschlossene Gold-
> Ladeanzeige. Die Produktionsintegration und Prüfungen des ausgelieferten
> Pakets fehlen weiterhin. [Nachweise und Grenzen](NATIVE-SAVE-TRANSFER.md).

Ein gemeinsamer Windows-Installer für **Gold Edition 1.12.1396, Steam History
Edition und Ubisoft History Edition**. Alle drei Ausgaben verwenden denselben
zweisprachigen Launcher, eine getrennte Spielkopie, private Profile und die
geprüfte Plattformisolation. Steam oder Ubisoft Connect müssen für die vorbereitete
Kopie nicht laufen. Jeder Spieler benötigt eigene unterstützte Originaldateien;
der Download enthält keine Spieldaten.

**Der bisher gebaute Produktionskandidat bietet Gold nur als Singleplayer.**
RC4.00 wird erst nach Integration und Prüfung des nachgewiesenen gemischten
Speicher-/Ladepfads veröffentlicht. Die Matrix unten beschreibt weiterhin den
älteren Paketkandidaten; die privaten Versuchs-DLLs werden nicht übernommen.

[Release und Downloads](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/tag/v2.0.0-rc4.00)
· [Setup.exe](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/v2.0.0-rc4.00/Setup.exe)
· [Windows-ZIP](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/v2.0.0-rc4.00/WePlay-RC4.00.zip)
· [SHA256SUMS](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/download/v2.0.0-rc4.00/SHA256SUMS)

## Älterer Paketkandidat — keine RC4.00-Freigabe

| Funktion | Gold | Steam History | Ubisoft History |
| --- | --- | --- | --- |
| Gemeinsamer Installer, Launcher und Standalone | Ja | Ja | Ja |
| Kampagne, Gefecht, eigene Profile und Spielstände | Ja | Ja | Ja |
| DXVK, gemeinsame Grafikseite, längere Detail-/Sichtweite | Ja | Ja | Ja |
| ReShade-Effekte ohne GUI/Overlay | Ja | Ja | Ja |
| LAN, private EasyTier-Räume, öffentliche Lobby | Nein | Ja | Ja |
| Eigene Burgen und Multiplayer-Speichern/Laden | Nein | Ja, Grenzen unten | Ja, Grenzen unten |
| Optionale vollständige History-NEO5-Logistik | Nein | Ja | Ja |
| Gold ↔ History Crossplay | Nicht ausgeliefert | Nicht ausgeliefert | Nicht ausgeliefert |

## Vorgesehener RC4.00-Umfang nach Paketnachweis

| Gemischte Sitzung | Vorgesehener Umfang |
| --- | --- |
| Host / Gast | Gold-Host; ein originaler Steam- oder Ubisoft-History-Gast |
| Karte und Spieler | Monhaim Valley; zwei Menschen und zwei native KIs |
| Speichern / erneutes Laden | Neuer Host-Save, Neustart und gemeinsamer Reload mit beiden History-Ausgaben |
| Logistik | Optionale gemischte FIFO-/Einzelprioritäten-Funktionen, Maske 19; gespeicherter Zustand bleibt erhalten |
| Freigabe | Erst nach Original-CRC-Vergleich, fortgesetzter Logistik und regulärem Beenden im finalen Paket |

Diese Zielmatrix ist noch kein bestandener Pakettest. Vollständiges Gold-NEO5,
beliebige Karten/Saves, vier menschliche Spieler und neue Dauerzusagen für
getrennte PCs gehören nicht zu diesem begrenzten Nachweis.

Release-Bilder und Funktionskarten müssen diesen geprüften Umfang zweisprachig
wiedergeben: **rc4.00**, **Gold + Steam History + Ubisoft History** und nach dem
Paketnachweis **gemischtes Speichern und Laden / mixed save and reload**.
Der alte Solo-Entwurf ist kein Release-Asset. Keine Darstellung als vollständiges
Gold-NEO5 oder pauschal desync-freier Multiplayer; endgültige Bilder und ihre
Prüfsummen werden erst zusammen mit dem qualifizierten Paket eingefroren.

## Multiplayer

- **Local multiplayer over LAN** — History-Spieler finden ihre lokale Gruppe
  über die LAN-Karte. Das eigentliche Spiel wird anschließend im S7-Multiplayer
  erstellt beziehungsweise betreten.
- **Private multiplayer via integrated EasyTier VPN** — private Internet-Räume
  verbinden die beteiligten Rechner über den integrierten Tunnel. Der Host teilt
  seine Einladung nur mit den gewünschten Mitspielern.
- **Public lobby server for finding games, with multiplayer connections via
  EasyTier** — die öffentliche Liste dient der Spielsuche; der Spielverkehr läuft
  über den jeweiligen EasyTier-Raum. Discord ist optional.
- **Custom castles in multiplayer** — eigene Burgen werden im History-Multiplayer
  übertragen. Korrekturen an Burgdaten und gegenseitigen KI-Teambeziehungen bleiben
  enthalten.
- **Fixes for several causes of desync** — enthält unter anderem die Korrekturen
  für Lobby-Allianzen, wiederhergestellte KI-Werkstattsuchen und Kartograph-
  Vorlagennamen. Das ist keine Zusage, dass alle Desyncs beseitigt sind.
- **Save and load multiplayer games** — History behält manuelle Host-Saves,
  den Host-Autosave und die native Übertragung beim Wiederaufnehmen. Details und
  Kompatibilitätsgrenzen stehen weiter unten.
- **Cross-play between Gold Edition and History Edition** — nachgewiesener
  Entwicklungsstand, noch keine Produktionsfreigabe: Original-Gold und beide
  originalen History-Ausgaben liefen gemeinsam, setzten echte gespeicherte Welten
  fort und luden einen daraus neu geschriebenen Host-Checkpoint ein zweites Mal.
  Der experimentelle FIFO-/Einzelprioritäten-Port ist kein vollständiges NEO5.
  Native162 wird nicht als private DLL in den Installer kopiert; die normale
  Adapter-, Launcher- und Paketintegration steht noch aus.

Die History-Pfade sind enthaltene Preview-Funktionen mit bisherigen lokalen und
teilweisen Zwei-PC-Nachweisen. RC4.00 begründet keine neue Freigabe langer
EasyTier-Partien auf getrennten PCs. Frühere separate-PC-Desyncs sind nicht
pauschal gelöst. Alle Teilnehmer brauchen dasselbe aktuelle Paket und passende
Spielinhalte; vor einem Hostwechsel beide Spiele neu starten.

## Gameplay und NEO

**Alle 14 regulären KI-Gegner einschließlich Maid Marielle:** Quellstand
`56ea811` integriert den geprüften nativen Katalogfilter für Gold, Steam History
und Ubisoft History. Die 33 versteckten Kampagnenvarianten/Platzhalter bleiben
versteckt. Für den finalen Installer müssen die normalen Produktionsadapter neu
gebaut, integriert und geprüft werden; ältere Installer erhalten den Fix nicht
automatisch. Das ist **keine** pauschale Freigabe sämtlicher Kampagnen- oder
Kosmetik-Freischaltungen. [KI-Katalog und Nachweise](AI-CATALOG-UNLOCK.md).
Die [separate Produktions-Buildprüfung](receipts/rc400-ai-production-rebuild-20261115.json)
trennt das einfache History-Directory-Profil vom vollständigen Release-Profil
mit NEO und ResumeCatchup. Sie ersetzt keine Prüfung des final installierten Spiels.

**Fixes for several issues causing undelivered goods or stalled production**:
History enthält die bisherigen Logistik-, Werkstatt-Abhol- und Wiederaufnahme-
Korrekturen. Die NEO-Abholkorrektur verhindert den nachgewiesenen Stillstand durch
einen Auftrag mit noch fehlendem Eingangsgut. Das ist keine allgemeine Garantie
für jede Produktionskette und wird nicht als neuer Gold-Logistikfix beworben.

**Optional NEO Logistics: FIFO job processing with transport priorities removed**
steht in History als Option in der Kartenkonfiguration zur Verfügung, zunächst
ausgeschaltet. Der Host wählt sie vor Spielbeginn; ein Kartenwechsel setzt sie
zurück. Nach dem Start bleibt die Wahl fest und ein kompatibler Save stellt sie
wieder her. Der ältere Solo-Kandidat zeigt in Gold keine NEO-Option. Der
RC4.00-Zielpfad für gemischte Sitzungen verwendet die oben getrennt beschriebene
FIFO-/Einzelprioritäten-Maske 19; deren Produktionsbedienung ist noch zu prüfen.

NEO verwendet für unterstützte Lager-Transportaufträge das Erstellungsalter
(FIFO). **Gruppenprioritäten** sind deaktiviert; ausdrückliche **Einzelprioritäten
von Spielern und KI bleiben wirksam** und werden mitgespeichert. „Transport
priorities removed“ bedeutet daher nicht, dass sämtliche Prioritäten verschwinden.
History-NEO5 umfasst außerdem geeignete Lagerübergaben nach nutzbarer Straßen-
Gesamtdistanz und die Werkstatt-Abholkorrektur. Es gibt keine zugesagte
Lieferbeschleunigung oder neue x20-Qualifikation.

## Interface & Startup

- **Removed non-functional interface elements** — bereinigte Menüs und entfernte
  tote Online-/Social-Routen. Gold bietet nur die zugelassenen Solo-Funktionen;
  seine unfreigegebenen Multiplayer-Menüpunkte bleiben verborgen.
- **Faster startup by skipping trailers and the main-menu map** — der schnelle
  Start überspringt Trailer und die Hintergrundkarte. Funktionen wie die
  Burgenwerkstatt benötigen weiterhin die dafür vorgesehene Hintergrundkarte.
- Sichtbare Launcher-Version: **rc4.00**. Technische Update-Metadaten verwenden
  intern `2.0.0-rc4.0`, Sequenz **29**; diese Ziffern erscheinen nicht als
  Versionspräfix im Launcher.
- Verbesserungen für Join/Cancel, Grafik-Save/Discard, sichere Paketübernahme und
  Reparatur vorhandener Standalone-Kopien sind enthalten.
- Der Gold-Tutorial-Guard für fehlende Save-Metadaten ist jetzt auch mit den
  exakten Produktions-Interface-Pins verbunden. Alle vier UI-Kompositionen und
  der gebündelte Python-Interpreter erzeugen den geprüften Stand; das ersetzt
  noch keine Freigabe der nativen Crossplay-Produktionsintegration.
  [Paketprüfung](receipts/rc400-gold-ui-package-20261115.json).

## Graphics

- **Modernized graphics and rendering pipeline via DXVK** — der geprüfte x86-
  DXVK-Pfad modernisiert die Darstellung des ursprünglichen 32-Bit-Spiels.
- **Increased level of detail (LOD) and extended draw distance** — gemeinsame
  Optionen für Detail-/Schattenweite, Qualitätsprofile, Anzeige, Filterung und
  die vorhandenen verfassten Effekte. Mehr Details können mehr GPU-Leistung kosten.
- ReShade **6.8.0 ohne GUI, Startbanner, Statistik-Overlay oder Overlay-Hotkeys**
  ist für alle drei Ausgaben gebündelt. Effekte und Atmosphärenanbindung bleiben
  erhalten. Es gibt keinen Download oder Rückfall auf die alte GUI-DLL.
- Gold behält DXVK 3.1.1 mit der erforderlichen strikten Float-Emulation für Wasser.
  Tone, SMAA, Kontaktverschattung und Mittsommer sind verfügbar. Das nachgewiesen
  fehlerhafte Gold-**Maximum** bleibt gesperrt. History behält sein vorhandenes
  Maximum und die native Rendererauswahl; Gold hat keinen freigegebenen nativen
  Fallback. Vulkan-Unterstützung ist für DXVK erforderlich.
- Keine neue FPS-, Flimmerfreiheits- oder Performance-Garantie.

## Neu installieren — alle drei Editionen

1. Windows 10 ab Build 19045 oder Windows 11, x64, eigene unterstützte Spieldateien
   und ausreichend Platz für eine vollständige getrennte Kopie bereitstellen.
   Für moderne Grafik muss die GPU Vulkan unterstützen. Python und .NET sind dabei.
2. **Setup.exe** herunterladen oder das Windows-ZIP vollständig entpacken und
   dessen Setup starten. Den gemeinsamen WePlay-Launcher installieren.
3. Im Launcher den Originalordner von **Gold 1.12.1396**, **Steam History** oder
   **Ubisoft History** auswählen. Keine fremden EXEs oder Dateien verschiedener
   Ausgaben mischen. Nicht unterstützte Fingerprints werden abgelehnt.
4. Einen Speicherort für die eigene WePlay-Kopie wählen. Die Originalinstallation
   bleibt erhalten. Nach abgeschlossener Prüfung über **Solo** starten und unter
   **Grafik** passende Optionen wählen. Gold umfasst Basiskampagne, Chroniken und
   Gefechte; History zusätzlich die Multiplayer-Karten.
5. Für einen privaten Standalone-Export immer den **gesamten Export** behalten und
   verschieben. Start über `App/S7Launcher.exe` oder den enthaltenen Starter.
   Keine Plattformclients, zusätzliche Python-/NET-Installation oder erreichbare
   Originalinstallation nötig. Der öffentliche Installer enthält weiterhin kein
   Spiel; private Exporte mit Spiel-/Profilinhalten nicht öffentlich hochladen.

## Von einem älteren RC aktualisieren

1. Profile und Saves sichern. WePlay und alle S7-Spiele schließen. Das automatische
   Update prüft signierte Metadaten und den vollständigen Installer; erst die
   ausdrücklich gewählte Installation ersetzt die Anwendung.
2. Alternativ das neue **Setup.exe** direkt ausführen und denselben WePlay-
   Anwendungsordner wählen. Für eine vorhandene portable Kopie **Vorhandene
   Standalone-Kopie auswählen** benutzen und den äußeren Ordner mit
   `standalone.json` oder dessen `App` wählen. Keine neue Unterkopie darin anlegen.
3. Die vorhandene Spielkopie im Launcher bei Bedarf aktualisieren/reparieren.
   Unterstützte Einstellungen und Spielstände werden übernommen; die neue Kopie
   wird erst nach erfolgreicher Prüfung aktiv. Ein Inhaltsupdate benötigt
   vorübergehend Platz für eine weitere vollständige Kopie. Unbekannte oder
   geänderte Dateien werden nicht als sichere Originale übernommen.
4. **Jeden History-Mitspieler aktualisieren**, auch mit NEO aus. Alte Einladungen
   neu erstellen. Bei einem abgebrochenen Update Setup erneut gegen denselben
   Ordner ausführen; keine einzelnen DLLs oder Archive verschiedener RCs mischen.

Die Update-Manifeste sind mit dem bestehenden WePlay-Schlüssel signiert.
Die Windows-Programme sind nicht Authenticode-signiert. WePlay ändert keine
Windows-Sicherheitsrichtlinien.

## History: LAN, EasyTier und öffentliche Spiele

**LAN:** Im vertrauenswürdigen lokalen Netz die LAN-Karte verwenden. Danach im
nativen S7-Multiplayer ein Spiel erstellen oder betreten, Karte/Slots abstimmen,
Ready wählen und als Host starten.

**Private Internet:** Host erstellt einen privaten Raum, stimmt der beschriebenen
virtuellen Verbindung zu und teilt die Einladung. Gäste fügen sie ein und stimmen
ebenfalls zu. Erst wenn Raum und Dienste bereit sind, das Spiel in S7 betreten.

**Public Internet:** Öffentliche Liste öffnen oder einen öffentlichen Raum
erstellen. Einen Eintrag auswählen, den EasyTier-Verbindungsaufbau abwarten und
anschließend das eigentliche S7-Spiel betreten. Der Verzeichnisserver vermittelt
die Suche; er simuliert oder hostet keine Spielwelt. Discord-Veröffentlichung ist
eine getrennte, freiwillige Option.

Die Liste allein baut keinen Tunnel auf. Während einer Partie müssen Host-PC und
Launcher verbunden bleiben. NAT/Firewall/Relay-Verfügbarkeit können Verbindungen
verhindern. Der Tunnel begrenzt Verkehr auf vorgesehene Spiel-/Lobbyports und
Antworten. Windows kann für Adapter/Firewall erhöhte Rechte verlangen; das Spiel
läuft ohne Administratorrechte. Weitere Einzelheiten:
[LAN](LAN-SPIELER.md), [Internet](RC3-INTERNET.md),
[öffentliche Lobby und Testgrenzen](PUBLIC-LOBBY-SERVICE.md),
[Join-Korrektur](RC3.21-INTERNET-JOIN.md).

## Speichern, Laden und Kompatibilität

**Solo:** Editionseigene Profile und passende native Spielstände verwenden.
Profil, Save-Ordner und Zeitstempel zusammen sichern. Eine Gold-Welt ist keine
freigegebene History-Welt und umgekehrt. RC4.00 verspricht keine allgemeine
Konvertierung älterer oder editionsfremder Saves.

**History-Multiplayer:** Der Host speichert im Spielmenü; der Host-Autosave läuft
alle fünf Spielminuten in einem eigenen Slot. Nach dem vollständigen Neustart
wählt derselbe Host **Multiplayer → Spiel laden**. Menschen treten ihren
ursprünglichen Slots bei und wählen Ready. Den tatsächlichen Transfer abwarten,
bis Start freigegeben ist. Ein kompatibler NEO-Save stellt seine Wahl und die
gespeicherten Einzelprioritäten wieder her. Keine leere Ersatzliste erzwingen.

Ältere Saves ohne erforderliche WePlay-Multiplayer-Metadaten sind nicht unterstützt.
Einzelne `.s7s` ohne zugehöriges Profil sind keine unterstützte Migration.
Kartograph-Spielstände benötigen dieselbe lokale Vorlage auf allen PCs.
Vor jedem Versuch mit älteren Saves ein Backup behalten. Lokale History-
Wiederaufnahmen sind belegt; das ersetzt keine neue Steam-/separate-PC-/Internet-
NEO5-Dauerfreigabe. Bei langem Lobbytransfer nicht mehrfach starten.

**Gold/History gemischt:** Die jüngste private Testfolge setzte eine gespeicherte
Welt tatsächlich gemeinsam fort. Beide menschlichen Baustellen erhielten ihre
Waren und wurden fertiggestellt; der Host schrieb bei LogicTurn 700 einen neuen
originalen Checkpoint und die Simulation lief weiter. Genau dieser unveränderte
Checkpoint wurde danach mit Original-Steam beziehungsweise Original-Ubisoft
erneut gemeinsam geladen: 1.824 beziehungsweise 2.220 gemeinsame originale
CRC-Blöcke ohne Abweichung. Alle vier gespeicherten NEO-Marken wurden auf beiden
Clients wiederhergestellt; beide Welten und HUDs waren sichtbar. Der Ubisoft-
Lauf belegte außerdem Warenlieferung und Bauabschluss für beide KI-Spieler.
Das waren bewachte Loopback-Versuche auf Monhaim Valley mit zwei Menschen und
zwei KIs, keine Freigabe beliebiger Saves/Karten, vollständigen NEO5 oder
EasyTier-Dauerbetriebs. Die normalen Produktionsadapter und der Launcher müssen
diesen Ablauf erst noch integrieren. [Aktueller Reload-Nachweis](NATIVE-SAVE-TRANSFER.md).

## English release summary

RC4.00 combines supported Gold 1.12.1396, Steam History and Ubisoft History in
one Windows installer. Each uses its own verified copy and isolated profile,
without requiring Steam or Ubisoft Connect at runtime. Supply your own supported
game files. The current production candidate still exposes Gold as single-player;
RC4.00 publication waits for integration of the qualified mixed-reload path.
That older solo candidate will not ship as RC4.00. The release requires a Gold
host and one original Steam or Ubisoft History guest, two humans and two native
AI players on Monhaim Valley, with actual mixed save/reload proof from both
History editions in the same final installed package. Mixed FIFO/individual
priorities use feature mask 19, separate from full History NEO5. Final bilingual
feature images must match that qualified scope; the earlier solo draft is obsolete.

History includes **Local multiplayer over LAN**, **Private multiplayer via
integrated EasyTier VPN**, and a **Public lobby server for finding games, with
multiplayer connections via EasyTier**. It retains **Custom castles in multiplayer**,
**Fixes for several causes of desync**, and **Save and load multiplayer games**.
These remain preview features; separate-PC Internet endurance and universal
desync freedom are not claimed.

History also retains **Fixes for several issues causing undelivered goods or
stalled production** and **Optional NEO Logistics: FIFO job processing with
transport priorities removed**. NEO starts off and is selected by the host before
the match. Group priorities are removed; explicit individual player/AI priorities
remain effective and are saved. Full History NEO5 includes road-distance handoffs
and the workshop pickup correction. The older Gold solo candidate has no NEO
option. The intended mixed RC4.00 path uses the separately qualified FIFO and
individual-priority mask 19; its production UI still requires verification.

All editions include **Removed non-functional interface elements**, **Faster
startup by skipping trailers and the main-menu map**, **Modernized graphics and
rendering pipeline via DXVK**, and **Increased level of detail (LOD) and extended
draw distance**. ReShade 6.8.0 now has no compiled GUI/overlay while preserving
effects. Gold Maximum remains disabled because of verified UI defects; Gold
requires its qualified DXVK path. History retains native rendering and Maximum.

**Cross-play between Gold Edition and History Edition** has now passed a bounded
private save/second-reload sequence with both original History editions. The
second reload produced 1,824 common original CRC blocks with Steam and 2,220 with
Ubisoft, without a mismatch, with all four saved NEO marks restored. These are
loopback tests on Monhaim Valley with two humans and two AI players. Production
adapter, launcher and package integration remain required before RC4.00 ships.
Full Gold NEO5, arbitrary-save/map support, effective x20 and separate-PC EasyTier
qualification remain open. The private Native162 DLLs are not release payloads.

**Install:** Run Setup, select your supported edition and a new WePlay copy
location, then wait for verification. Python/.NET are included. Windows 10 build
19045+/11 x64 is required; DXVK needs Vulkan. For an update, close WePlay/S7 and
target the existing app folder. For a complete standalone export choose its outer
folder or App using the installer's existing-copy selector. Keep the whole export
together. Update every History peer and recreate invitations. A content upgrade
temporarily needs room for another full copy. Original installations stay intact.

**Play online:** LAN finds the local group; private Internet uses a shared
invitation; public Internet selects a directory entry. Accept the explained
EasyTier connection, wait for readiness, then create/join the native S7 match.
The host PC and launcher must remain connected. The directory is not a game-world
server. Discord is optional. NAT, relays and firewalls may prevent connections.

**Resume History:** Keep the host profile, saves and timestamps together. The
same host uses Multiplayer → Load game after restart; players rejoin original
slots, choose Ready and wait for native transfer. Compatible NEO state restores
the selected policy and individual intents. Missing metadata, edition conversion
and arbitrary loose-save migration are unsupported. Keep backups and matching
Map Forge templates. Mixed Gold/History resume remains unavailable in released
packages until the required final RC4.00 package evidence exists.

## Nachweise und weiterführende Hilfe

[Plattformisolation](PLATFORM-STANDALONE.md) ·
[Gold-Grafik und tatsächliche Tests](RC3.28-GRAPHICS-PARITY.md) ·
[ReShade ohne Overlay](RESHADE-NO-OVERLAY.md) ·
[NEO-Verhalten](NEO-LOGISTICS-MAP-OPTION.md) ·
[Multiplayer-Saves](RC3.11-MULTIPLAYER-SAVES.md) ·
[Grafikoptionen](VISUAL-QUALITY.md) ·
[Grafikmessungen und Grenzen](GRAPHICS-RC319-MEASUREMENTS.md) ·
[Update-Recovery](UPDATE-RECOVERY.md) · [Credits](../CREDITS.md)

Maintainer: **WePlay Community / R4nd0**. Community preview; keine offizielle
Ubisoft-Veröffentlichung. Die Download-Prüfsummen und `Verification.json` gehören
zum jeweiligen Release-Asset; experimentelle Nachweise sind gesondert gekennzeichnet.
