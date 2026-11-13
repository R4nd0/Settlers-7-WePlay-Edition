# RC3.26: gemeinsames Standalone-Hauptpaket

Der Windows-Starter bereitet Gold 1.12.1396 sowie die bekannten Ubisoft- und
Steam-History-Ausgaben im selben Hauptpaket vor. Standalone ist verpflichtend.
Ein Build ohne geprüfte Plattformlaufzeit oder Gold-Unterpaket wird abgewiesen.
Die reguläre gepackte Steam-EXE bleibt auf Platte unverändert.

Für alle drei Ausgaben werden weder Steam noch Ubisoft Connect benötigt.
Originalinstallationen bleiben unverändert. Das öffentliche Setup enthält
keine Spieldateien, Kontodaten oder privaten Profile. Eine bestehende geprüfte
WePlay-Kopie kann als Quelle dienen, auch wenn die Originalinstallation fehlt.

Gold zeigt im gemeinsamen Starter Einzelspieler, Hilfe und Updates. Kampagne
und Scharmützel werden im Spiel ausgewählt; Grafik und Profil werden dort
eingestellt. History behält seine bisherigen Spiel- und Grafikfunktionen.
Die Gold-Integration ist keine Freigabe für Gold/History-Mehrspieler.

## Isolation bei Installation, Reparatur und Update

Die bereits [geprüfte native Isolation](PLATFORM-STANDALONE.md) bleibt erhalten:
privates Profil unter `LocalData/ProfileRoot`, gesperrte Kindprozesse und vier
Windows-Filter pro EXE-Pfad für Nicht-Loopback-Verbindungen über IPv4/IPv6.
Vor dem ursprünglichen Spieleinstieg prüft der tatsächliche Spielprozess die
Windows-Sperre. Ohne bestätigte Sperre wird der Einstieg nicht freigegeben.
Ein neuer Kopiepfad benötigt seine eigene Windows-Administratorbestätigung.
Eine zusätzliche Windows-Firewall-Abfrage des Spiels nach Zugriff auf öffentliche
oder private Netzwerke kann beim Einzelspielertest mit **Abbrechen** geschlossen
werden. Sie ersetzt nicht die zuvor eingerichtete WePlay-Netzsperre. Das Spiel
kann dabei geöffnet bleiben.

Eine Reparatur erstellt eine neue, getrennte Instanz. Sie verwendet die
geprüften Originaldateien in der vorhandenen Kopie und die aktuelle Laufzeit
aus dem Hauptpaket. Private Spielstände und Profile werden übernommen;
unterschiedliche bereits vorhandene Zieldateien werden nicht überschrieben.
Die alte Auswahl wird erst nach erfolgreicher Prüfung der Ersatzkopie geändert.
Fehlgeschlagene Vorbereitungen und veränderte Sicherungen ersetzen keine
aktive Instanz. Die bisherige Kopie und ihre privaten Daten bleiben erhalten.
Auch ältere Gold-Kopien mit noch nicht umbenannter Original-Audiodatei können
übernommen werden. Dafür gilt dieselbe feste Originalprüfsumme; eine bereits
vorhandene, aber veränderte Originalsicherung wird nicht umgangen.

Die Kennung für notwendige Spielkopie-Updates enthält auch die Plattformgates,
Netzfilter-Helfer, Startwerkzeuge und das Gold-Unterpaket. Solche Änderungen
werden daher nicht als bloßes Oberflächenupdate übersprungen.

Der Starter merkt die Isolationsanforderung zusätzlich in seinem privaten
Zustand. Das neue Setup prüft sie vor dem Austausch des Pakets. Ein Hauptpaket
ohne Isolation für alle drei Ausgaben wird abgewiesen. Auch beschädigte
Kennzeichnungen einer vorhandenen Kopie erlauben keinen Rückfall auf eine
plattformgebundene Reparatur.

## Signierte Updates und ältere Starter

Ein vollständiger Release hat zwei signierte Manifeste mit identischer
Setup-Prüfsumme:

| Datei | Verwendung |
| --- | --- |
| `weplay-update.json` | Bestehende Starter mit dem bisherigen Signaturformat |
| `weplay-update-isolated.json` | Zusätzlich signierter Isolationsvertrag für Gold, Steam und Ubisoft |

Der neue Starter bevorzugt das zweite Manifest. Nach aktivierter Isolation
akzeptiert er kein Update ohne vollständigen Editionsvertrag. Die Prüfung
erfolgt vor dem Download und erneut in der Übergabe. Der kompilierte
Windows-Helfer und der PowerShell-Pfad prüfen den signierten Vertrag jeweils
unabhängig vor dem Warten auf alte Prozesse oder dem Start des Installers.
Signatur-, Prüfsummen-, Prozessbesitzer- und Wiederholschutz bleiben bestehen.

Das Signierwerkzeug akzeptiert nur ein Setup mit passender Buildquittung eines
vollständigen isolierten Hauptpakets. Es erzeugt beide Manifeste lokal und
veröffentlicht sie nicht selbst. Das bisherige Format ermöglicht älteren
Startern den Übergang auf dasselbe neue Hauptpaket.

## Build und Prüfstand

`tools/desktop/build_rc2.py` baut nun standardmäßig das gemeinsame RC3.26-Paket.
Es übernimmt die geprüfte Plattformlaufzeit und das Gold-Unterpaket aus dem
Basispaket. Beim ersten Übergang können `--platform-runtime` und
`--gold-package` ausdrücklich angegeben werden. Die aktuellen Gold-Werkzeuge
werden bei jedem Build neu aufgenommen; native Eingaben bleiben an ihre
geprüften Fingerabdrücke gebunden. Private Spielkopien gehören nicht in den
Build-Stage.

Der Windows-Build verwendet .NET 10.0.15 und SDK 10.0.404. Das ältere
Compilerpaket wurde aufgrund der aktiv gebliebenen Sicherheitsprüfung ersetzt;
es wurden keine Warnungen unterdrückt. Siehe die
[offiziellen Microsoft-Veröffentlichungsdaten](https://builds.dotnet.microsoft.com/dotnet/release-metadata/10.0/releases.json)
und den [Microsoft-Sicherheitshinweis](https://github.com/dotnet/runtime/security/advisories/GHSA-p3jp-9v83-76w9).

Die früheren tatsächlichen Welt-/Speicher-/Kaltladetests und jeweils elf
Debugger-Stopps pro Familie bleiben in der
[nativen Prüfquittung](receipts/platform-standalone-20261112.json) dokumentiert.
Die Hauptpaket-Prüfung unterscheidet davon die Update-Übergaben,
Oberflächentests und Reparaturen mit echten Spielkopien. Periodische
Socket-/Modulbeobachtungen sind keine Paketaufzeichnung. Ein Hauptmenü oder
ein bestandener Reparaturtest belegt keine Mehrspieler-Synchronisation.

Der echte private Windows-Setup-Test hat außerdem drei Paketwechsel geprüft:
Ein manipulierter Payload wurde vor der Veröffentlichung abgewiesen;
ein ausgelöster Fehler nach dem Umbenennen des neuen Pakets stellte das alte
Paket wieder her; der anschließende vollständige Updateversuch endete mit
Erfolg. Isolationsvertrag und Test-Spielstanddaten blieben in allen drei
Fällen unverändert. Ein vollständiger, strukturell gültiger Payload ohne
verbindlichen Isolationsvertrag wurde zusätzlich vor dem Anlegen eines
neuen Paketordners abgewiesen.

Die [Hauptpaket-Prüfquittung](receipts/rc326-standalone-standard-20261113.json)
enthält 293 bestandene automatisierte Tests und folgende echte Spielkopie-Tests:

| Ausgabe | Installation und beschädigte Reparatur | Erhaltener Spielstand geladen | Kernelisolation | Exit | Netz-/Modul-/Prozessbeobachtungen |
| --- | --- | --- | --- | --- | --- |
| Reguläres Steam History | bestanden | Forest Realm | 7 | 0 | 149, keine unerwarteten Befunde |
| Ubisoft History | bestanden | Waldreich | 7 | 0 | 280, keine unerwarteten Befunde |
| Gold 1.12.1396 | bestanden | Forest Realm | 7 | 0 | 748, keine unerwarteten Befunde |

Die gemeinsamen Windows-Documents-Profile sowie EXE und Adapter blieben nach
allen drei Läufen unverändert. Ohne Windows-Bestätigung für die Einrichtung
der Sperre wurde ein vorheriger Start vor dem Spieleinstieg abgewiesen.

Grenzen dieses Nachweises: Die Speicherwache meldete bei Ubisoft und Gold einen
Messausfall und setzte ihre konservative Wiederherstellungsmarkierung. Die
erfolgreichen Messungen zeigten jeweils Speicherdruck 0; Schutzgrenzen wurden
nicht geändert. Golds vorhandene DXVK-Konfiguration zeigte schwarze
Wasserflächen. Dieser Stand qualifiziert die Plattformisolation und den
Spielstandübergang, keine fehlerfreie Gold-Grafik oder Langzeitstabilität.
