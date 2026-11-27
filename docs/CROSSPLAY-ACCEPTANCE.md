# Automatisierte Crossplay-Abnahme unter Windows

`tests/crossplay_acceptance.py` steuert die drei echten Spielclients der
vorbereiteten Testkopien. Es spielt je Hostrolle eine neue Partie mit Magd
Marielle auf Tal von Monhaim an, speichert über das Spielmenü, schließt die
Clients regulär und lädt den neu geschriebenen Spielstand in einem frischen
Raum. Beide Gäste müssen Datei und Spielstart bestätigen. Anschließend
vergleicht das Skript den Fortschritt der drei Clients anhand ihrer nativen
Simulationszüge.

Der Tester verwendet 1024 × 768 im Fenstermodus. Die deutsche und englische
Menüvariante wird über Windows-OCR erkannt. Die Maus und Tastatur müssen für
diesen lokalen Lauf frei bleiben. Das Skript beendet die Testfenster bei
Fehlern regulär und schreibt die Ursache in den Abnahmebeleg.

## Aufruf

Verwende die drei isolierten Kopien `1`, `2` und `3` für Gold, Ubisoft History
und Steam History. Der Vorbereitungsbeleg muss ihre EXE-Prüfsummen und die
Paket-ID des exakten `Setup.exe` enthalten. Starte einmal die temporären,
auf die Testkopien beschränkten Windows-Netzwerkregeln. Maßgeblich ist die
Bestätigung des Regelhelfers in `policy-ready-three.private.json`.

```powershell
rtk proxy python -B tests/crossplay_acceptance.py `
  --copies <Testkopien> --prepared <Vorbereitungsbeleg> `
  --setup <geprüftes-Setup.exe> --python <Python-des-Testpakets> `
  --runner <lokaler-Raumlaeufer.py> --receipt <neuer-Abnahmebeleg.json>
```

Der Raumerzeuger muss `--candidate launch-three --host 1|2|3` unterstützen.
Er schreibt `launched.private.json`, die darin genannte Raumspur und am
Ende `exit.private.json` in den Ordner der Kopien. Für `rc4.14` ist das der
bereits verwendete private Raumerzeuger `launch-rc414-stable.py`.

Für eine schnelle Gegenprobe lässt sich mit `--hosts 2` eine einzelne Rolle
prüfen und die Beobachtungszeit mit `--sample-seconds 10` verkürzen. Der
Standard prüft alle drei Rollen jeweils 60 Sekunden vor und nach dem Laden.
Die Übertragung des Spielstands wartet auf die echten Empfangsbestätigungen
und kann bei einem History-Host mehrere Minuten dauern. Nach einem
erfolgreichen Durchlauf löst das Skript die Bereinigung beim Regelhelfer aus
und prüft seine Bestätigung.
