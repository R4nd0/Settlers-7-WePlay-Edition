# S7 RC – Grafikstand vom 28.10.2026

Dieses Testpaket kombiniert Netzwerkrevision 3 mit der heutigen Grafikauswahl,
dem korrigierten Fenstermodus und randlosem Vollbild. Es enthält Python, .NET
und optional das unveränderte DXVK 3.1.1. Eigene Spiel-Dateien werden benötigt.

## Auf jedem Testrechner

1. Das ZIP vollständig in einen eigenen Ordner entpacken.
2. `Installieren.cmd` starten. Die eigene S7-History-Installation als Quelle und
   einen neuen, noch nicht angelegten Zielordner wählen. Eine getrennte Kopie
   braucht ungefähr 7 GB. Vorhandene Testkopien werden nicht überschrieben.
3. Für den heute getesteten Grafikpfad im entpackten Paket
   `Grafikprofil-aktivieren.cmd` starten und den Ordner der neuen Testkopie angeben.
   Vorher alle S7-Spiele schließen. Das aktiviert DXVK, das 60-Hz-Limit und beim
   Spielstart die Shaderkorrektur gegen das auf PC1 beobachtete schwarze Wasser.
4. In der Testkopie `Start-S7-Test.cmd` für einen lokalen Test oder
   `Start-S7-LAN.cmd` für LAN starten. Das Starterfenster geöffnet lassen.

DXVK benötigt eine passende Vulkan-Grafikkarte und einen passenden Treiber.
Falls das Spiel damit nicht startet, alle S7-Spiele schließen und im entpackten
Paket `Standardgrafik-wiederherstellen.cmd` für diese Testkopie verwenden.
Danach läuft der ursprüngliche D3D9-Renderer wieder automatisch. Es werden
keine Treiber installiert und keine Systembibliotheken ersetzt.

## Grafikmenü

Das gemeinsame Startmenü ist auf Englisch. Für eine eigene Anzeigeauswahl
`Use current game display settings` ausschalten. Dann Zielbildschirm,
`Windowed` oder `Fullscreen (borderless)` und Auflösung wählen.

Randloses Vollbild verwendet die Desktopauflösung des gewählten Bildschirms.
Im Fenstermodus ist die Auflösung wählbar. Die ersten zwei aktiven Bildschirme
werden unterstützt; Auflösung und Pixelzahl sind begrenzt. Bei DXVK entweder
die passende Grafikkarte des Zielbildschirms oder `Automatic` wählen.

`Simple main menu + VSync` schaltet die Menülandschaft und einige Vorschauen ab.
Für den heute getesteten einfachen Menüpfad diese Option einschalten.
Grafikkarte und Anzeigeauswahl werden je Testkopie lokal gespeichert.
Beim Start im Fenster/randlos wird der gespeicherte Vollbildwert der gemeinsamen
Spiel-Options.ini auf 0 gesetzt; andere Einträge bleiben erhalten.

## Gemeinsame LAN-Tests

Auf allen Rechnern dasselbe RC-Paket verwenden und dasselbe Grafikprofil
aktivieren. Unterschiedliche Renderer erzeugen unterschiedliche Dateistände.
Die konkrete Grafikkarte und Bildschirmwahl dürfen je Rechner verschieden sein.
Jeder Rechner bekommt eigene lokale Spielerkennungen; keine vorbereitete
Spielkopie und kein `LocalData` zwischen Geräten verteilen.

Ein Spieler erstellt im Starter eine LAN-Lobby, die anderen wählen diese aus.
Danach im Spiel eine öffentliche Mehrspielerpartie erstellen und beitreten.
Falls nötig `LAN-Netzwerk-freigeben.cmd` in der Testkopie verwenden; es fragt
Windows nach der privaten Freigabe dieser Kopie im lokalen Subnetz.

## Prüfstand und Grenzen

Fenster 1280×720 auf beiden PC1-Bildschirmen sowie 1920×1080 als Fenstermodus
sind geprüft. Randlos 2560×1440 ist auf dem Hauptbildschirm sichtbar geprüft
und auf Bildschirm 2 vom Nutzer bestätigt. Beide randlosen Controller meldeten
eine Anwendung ohne Fehler; beide Spiele endeten regulär mit Code 0.
Die Wasserkorrektur wurde auf PC1/Tanholm mit DXVK gegen die fehlerhafte
Auto-Einstellung verglichen. Andere Grafikkarten bleiben Testziele.

Das Spiel bleibt 32 Bit. Dieses RC liefert weder einen fertigen D3D11-Port noch
FSR. Es ist ein experimenteller Teststand; der bekannte LAN-Desync ist weiterhin
offen. Die kurzen Grafiktests belegen keine allgemeine Langzeitstabilität.

Originalinstallation unverändert lassen. Die Datei `Dateistand-pruefen.cmd`
in der Testkopie prüft die installierten Dateien. Weitere Netzwerkhinweise
stehen in `LIESMICH.txt`, Lizenzen und Herkunft in `RUNTIME-SOURCES.txt`.
