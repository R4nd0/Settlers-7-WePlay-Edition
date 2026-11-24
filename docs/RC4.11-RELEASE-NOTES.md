# WePlay rc4.11 für Windows und Linux

- **LAN und Interneträume.** WePlay wartet vor dem Beitritt auf den passenden Raumdienst und wiederholt die Aufnahme einmal, falls die erste Antwort auf dem Transportweg verloren geht. Nach einem Koordinatorneustart prüft die LAN-Suche die neue Raumkennung erneut. Abgebrochene Beitritte geben reservierte Plätze frei.
- **Verbindungsverlust und Wiederherstellung.** Der Launcher meldet Ausfälle des Tunnels oder des Raumkoordinators und lässt das Spiel geöffnet. Er meldet auch, wenn der Koordinator wieder erreichbar ist. Kurzzeitige Statusfehler nach dem Spielende des Hosts trennen verbleibende Gäste nicht sofort.
- **Grafik.** Die DXVK-Prüfung erkennt und zeigt Grafikkarten zuverlässiger an. Randloser Start wird anhand des gewählten Bildschirms geprüft. Falls ReShade-SMAA beim Start nicht verfügbar ist, kann WePlay auf die Kantenglättung des Spiels zurückgreifen.
- **Einrichtung, Updates und Hilfe.** Verbesserungen an Reparatur, Update-Übernahme und Fehlerbehandlung. Unter Windows lässt sich `S7-Diagnose.json` aus **Hilfe** oder **Einrichtung** für den Support speichern.

Windows unterstützt Gold, Ubisoft History und Steam History. Gemischter Mehrspieler ist eine Vorschaufunktion. Das Linux-Paket unterstützt die History-Editionen unter Wine oder Proton. Verwende beim Mehrspielerspiel zu jeder Plattform passende WePlay-Kopien derselben Version.

## English

- **LAN and Internet rooms.** WePlay waits for the matching room service before joining and retries admission once if the first reply is lost in transit. LAN discovery checks the new room identity again after a coordinator restarts. Abandoned joins release their reserved slots.
- **Connection loss and recovery.** The launcher reports loss of the tunnel or room coordinator and leaves the game open. It reports when the coordinator becomes reachable again. Temporary status errors after the host exits their game give remaining guests time to reconnect.
- **Graphics.** The DXVK probe identifies graphics cards more reliably. Borderless startup checks the selected display. WePlay can fall back to game antialiasing when ReShade SMAA is unavailable at launch.
- **Setup, updates and help.** Improvements to repair, update migration and error handling. On Windows, save `S7-Diagnose.json` from **Help** or **Setup** to share with support.

Windows supports Gold, Ubisoft History and Steam History. Mixed multiplayer is a preview feature. The Linux package supports both History Editions under Wine or Proton. Use matching WePlay releases for your platform when playing multiplayer.

[Launcher-Anleitung / Launcher guide](LAUNCHER-GUIDE.md) · [Linux setup](LINUX.md)
