# WePlay rc4.08 für Windows / for Windows

- **Privater Raum:** Scheitert der Spielstart, beendet WePlay den für diesen Versuch gestarteten Raumdienst und gibt den LAN-Suchport wieder frei.
- **LAN-Suchport:** Der UDP-Suchport lässt sich im LAN-Tab ändern und auf `9112` zurücksetzen. Alle PCs im LAN verwenden denselben Port.
- **Diagnosebericht:** `S7-Diagnose.json` enthält bei einem fehlgeschlagenen nativen Spielstart die verfügbare Startstufe und den Prozess-Exitcode. Schlägt das Öffnen des LAN-Suchports fehl, enthält der Bericht auch den ursprünglichen Winsock-Code. Export über **Hilfe** oder in der **Einrichtung**.

- **Private rooms:** If the game fails to start, WePlay stops the room service created for that attempt and releases the LAN discovery port.
- **LAN discovery port:** Change the UDP discovery port in the LAN tab or restore the default `9112`. Use the same port on every LAN PC.
- **Diagnostic report:** `S7-Diagnose.json` records the available native startup stage and process exit code if the game fails to start. If WePlay cannot bind the LAN discovery port, the report also records the original Winsock code. Export it from **Help** or **Setup**.

[Launcher-Anleitung / Launcher guide](LAUNCHER-GUIDE.md)
