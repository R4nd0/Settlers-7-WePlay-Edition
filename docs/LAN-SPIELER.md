# Gemeinsam im LAN spielen

[English](LAN-PLAYERS.md) · [Aktuellen RC2.1 herunterladen](https://github.com/R4nd0/Settlers-7-WePlay-Edition/releases/tag/v2.0.0-rc2.1)

Alle verwenden dieselbe aktuelle Setup.exe und ihre eigene unterstützte
Settlers 7 History Edition. Keine IP-Eingabe, kein zusätzlicher Serverstarter
und keine zweite Lobbyauswahl im WePlay-Starter.

## Die erste Partie

1. **Alle PCs ins gleiche vertrauenswürdige Heimnetz bringen.** LAN-Kabel und
   WLAN können zusammenspielen, wenn der Router den Austausch erlaubt.
   Gast-WLAN, Geräteisolation, getrennte Netze oder manche VPN-Verbindungen
   können die Suche verhindern. Für dieses vertrauenswürdige Netz das
   Windows-Profil **Privat** auswählen; dafür gilt die WePlay-LAN-Freigabe.
   [Microsofts Anleitung zum Netzwerkprofil](https://support.microsoft.com/en-gb/windows/experience/connectivity-networking/essential-network-settings-and-tasks-in-windows).
2. **Auf jedem PC denselben RC2.1 installieren.** Setup.exe starten, das
   Originalspiel erkennen lassen und die getrennte Kopie fertig einrichten.
   Danach genügt die eine WePlay-Verknüpfung. Oben kann Deutsch oder Englisch
   ausgewählt werden.
3. **Für den ersten Test denselben Grafikmodus wählen.** Auf unterstützten
   PCs wird Moderne Grafik (DXVK) empfohlen. Grafikkartenmodell, Auflösung und
   Bildqualität müssen nicht gleich sein. Hinweise zu anderen Renderpfaden
   stehen weiter unten.
4. **Einen PC zuerst starten.** In WePlay **LAN spielen** öffnen, den Namen
   eingeben und **LAN-Spiel starten** anklicken. Warten, bis S7 im
   Mehrspielermenü angekommen ist. Bei blockiertem Zugriff im Starter
   **Privaten LAN-Zugriff erlauben** auf dem betroffenen PC verwenden und den
   zugehörigen Windows-Dialog bestätigen. Die Firewall bleibt eingeschaltet;
   für ein normales gemeinsames LAN ist keine Router-Portweiterleitung nötig.
5. **Die anderen PCs genauso starten.** WePlay verbindet sie automatisch mit
   der vorhandenen Gruppe. Im Spiel erstellt ein Spieler unter **Mehrspieler**
   die eigentliche Partie; die anderen treten dort bei. Das ist die einzige
   Partiewahl. In RC2.1 bleibt **Private Sitzung automatisch aus und gesperrt**: Private
   Sitzungen werden ausgeblendet und sind über den aktuellen LAN-Ablauf nicht
   betretbar. Im Starter gibt es keine zusätzliche Raumliste.

WePlay während der Partie geöffnet oder minimiert lassen. Der PC mit dem
gemeinsamen LAN-Dienst bleibt eingeschaltet und erreichbar, bis alle fertig
sind. S7 anschließend normal beenden; WePlay verwaltet die Hintergrunddienste.

Die Spieloption **Privates Spiel** ist unabhängig vom Windows-Netzwerkprofil
**Privat**: Die Spieloption bleibt aus, während das vertrauenswürdige Heimnetz
in Windows das Profil Privat verwendet.

## Wie die Suche funktioniert

WePlay fragt im lokalen Netz nach einem vorhandenen gemeinsamen Dienst.
Antworten werden anhand ihrer tatsächlichen Netzwerkquelle und einer Abfrage
des laufenden Dienstes geprüft. Auch der vorbereitete Spiel-/Patchstand wird
verglichen. Es gibt keine pauschale Adresssuche und keinen öffentlichen
Matchmaking-Server.

Ohne vorhandenen Dienst startet ein PC automatisch einen. Weitere PCs nutzen
die vorhandene Gruppe. Eine bereits aktive Gruppe hat Vorrang. Gleichzeitige
Erststarts werden mit einer einheitlichen Reihenfolge und wiederholten Prüfungen
abgeglichen. Bei unpassenden Dateien oder konkurrierenden aktiven Gruppen gibt
es eine Meldung statt einer stillen Aufteilung in getrennte Listen.

**Der gemeinsame Dienst ist nicht die eigentliche Partie.** Sein PC kann ein
anderer sein als der Gastgeber einer S7-Partie. Das Verlassen seiner eigenen
Partie beendet nicht absichtlich die Verbindung der anderen. Ausschalten oder
Trennen dieses PCs unterbricht sie jedoch. Ein Wechsel dieses Dienstes auf einen
anderen PC während laufender Partien ist nicht unterstützt. Ohne aktive Spieler
endet der Dienst normalerweise nach etwa einer Minute.

## Sind unterschiedliche Renderpfade spielzerstörend?

**Das ist nicht nachgewiesen.** Der bekannte Desync ist weiterhin offen.
Es gibt keinen bestätigten Befund, dass native Grafik gegenüber DXVK ihn
verursacht hat oder dass das Entfernen des nativen Pfades ihn beheben würde.

Die aktuelle Schutzprüfung vergleicht vollständige vorbereitete Dateisätze.
Kompatible native Grafik und moderne DXVK-Grafik haben unterschiedliche
Fingerabdrücke. Deshalb werden solche Unterschiede nicht ungeprüft in dieselbe
Gruppe gelassen. Der Starter kann einen bekannten Grafikprofilunterschied vor
dem Spielstart an die vorhandene Gruppe anpassen. Wird der nötige Modus nicht
unterstützt oder weichen andere Dateien ab, hält er den Beitritt an.

Für den ersten Test daher auf allen geeigneten PCs **Moderne Grafik (DXVK)**
verwenden und einen PC zuerst starten. Bei gleichzeitigen Erststarts mit
verschiedenen Profilen nach der Meldung den LAN-Start erneut versuchen; dann
kann der Starter das sichtbare Gruppenprofil abgleichen. Keine DLLs von Hand
ersetzen und keine verschiedenen RC-Pakete mischen.

Es gibt **einen aktuellen RC2.1-Download**. Kompatible Grafik im Installer ist
ein Hardware-Rückweg, keine zweite alte RC-Veröffentlichung. Die gleichen
Abgleichregeln gelten auch dort. Ein PC ohne Unterstützung für das benötigte
Gruppenprofil kann dieser Gruppe in dieser Konfiguration nicht beitreten.

## Wenn es nicht klappt

- **Freunde/Partien fehlen:** Haben alle LAN-Spiel starten angeklickt? Gleiches
  Heimnetz und gleicher RC2? Privaten LAN-Zugriff auf den betroffenen PCs
  erlauben und Gast-WLAN/Geräteisolation prüfen.
- **Andere Version/Grafik oder mehrere Gruppen:** Spiele beenden, den leeren
  Dienst etwa eine Minute auslaufen lassen und mit demselben unterstützten
  Profil einen PC zuerst starten. Laufende Gruppen nicht zusammenführen.
- **Benötigte Grafik nicht unterstützt:** Ein übereinstimmendes unterstütztes
  Profil ist erforderlich. Die Dateiprüfung soll nicht umgangen werden.
- **Späterer Desync:** Suche und Beitritt haben funktioniert, aber die
  Simulation bleibt ein offener Fehler. Erfolgreiche Suche garantiert keine
  stabile lange Partie.

Drei oder mehr echte Teilnehmer, große Endspielnetze und lange Sitzungen sind
für diesen kombinierten RC noch nicht neu qualifiziert.
[Testumfang](RC2-QA.md) · [Technische LAN-Details](AUTOMATIC-LAN.md)
