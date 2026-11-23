# WePlay rc4.10 für Windows / for Windows

- **Netzfilter-Reparatur.** Lässt Windows bei der Netzisolationsprüfung in Stufe 11 unerwartet eine lokale IPv4-TCP-Verbindung zu, erneuert WePlay die vier Regeln dieser Spielkopie und versucht den Start einmal erneut. Auch der zweite Start durchläuft die Netzisolationsprüfung. `S7-Diagnose.json` lässt sich über **Hilfe** oder in der **Einrichtung** speichern.
- **Grafikwiederherstellung.** WePlay sichert die letzte bestätigte Grafikkonfiguration. Scheitert der Start mit neuen Einstellungen frühzeitig, stellt der Launcher die vorherige Konfiguration wieder her. Beim Wechsel aus dem randlosen Vollbild kommt die vorherige Fensterauflösung zurück.
- **Weitere LOD-Stufe.** `+400 %` setzt die Modelldetail-Entfernungsgrenzen auf das Fünffache des Originals. Bei aktivierter Kopplung folgt die Schattenreichweite derselben Stufe.

- **Network filter recovery.** If Windows unexpectedly allows a local IPv4 TCP connection during the stage 11 isolation check, WePlay refreshes the four rules for that game copy and retries once. The retry runs the same isolation check. Save `S7-Diagnose.json` from **Help** or **Setup**.
- **Graphics recovery.** WePlay keeps the last confirmed graphics configuration and restores the previous settings if the game fails early with new settings. Switching out of borderless fullscreen restores the previous windowed resolution.
- **Additional LOD step.** `+400%` sets model detail distance thresholds to five times the original. Shadow distance follows the same step when linking is enabled.

[Launcher-Anleitung / Launcher guide](LAUNCHER-GUIDE.md)
