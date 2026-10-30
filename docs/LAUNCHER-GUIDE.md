# WePlay: Welcher Startweg ist richtig?

Diese Anleitung beschreibt die überarbeitete Oberfläche im Entwicklungsstand.
Der veröffentlichte RC3.5-Download verwendet teilweise noch die bisherigen
Beschriftungen; Verbindungsablauf und Spielauswahl sind gleich.

| Du möchtest … | Startweg | Was passiert? |
| --- | --- | --- |
| Kampagne, gegen KI oder im Kartographen spielen | **Allein spielen** → **S7 allein starten** | S7 startet mit deinen Einstellungen; andere PCs werden nicht verbunden. |
| Mit Freunden im selben Heimnetz spielen | **Im selben Netzwerk** → **LAN-Spiel öffnen** | WePlay findet die LAN-Gruppe automatisch. Alle starten diesen Modus; IP-Eingaben sind nicht nötig. |
| Mit Freunden an anderen Orten spielen | **Über das Internet** → **Internetspiel öffnen** | Einer öffnet einen WePlay-Raum, die anderen verbinden sich mit seiner privaten Einladung. |

Alle Mitspieler brauchen dieselbe WePlay-Version und ihre eigene unterstützte
S7-Installation. Der Spielername lässt sich vor dem Start auf der LAN- oder
Internetseite ändern.

## Internet: Raum zuerst, Partie danach

Der **WePlay-Raum verbindet eure Rechner**. Er erstellt noch keine Karte oder
S7-Partie. Die **Partie** legt ihr anschließend unter Mehrspieler im Spiel an.

**Du lädst ein:**

1. Unter **Internet spielen** die Auswahl **Ich lade Freunde ein** verwenden.
2. **Raum öffnen + S7 starten** wählen. Bei Bedarf die Windows-Netzwerkfreigabe
   bestätigen. Normalerweise bleiben die Verbindungseinstellungen auf Automatisch.
3. Im Starter **Einladung + Anleitung kopieren** wählen und den Text selbst
   privat an deine Mitspieler schicken, zum Beispiel über Discord.
4. In S7 unter **Mehrspieler** eine Partie erstellen. **Private Sitzung**
   ausschalten, damit die Mitspieler in eurem Raum die Partie finden.
5. Wenn alle beigetreten und bereit sind, im Spiel starten.

**Du wurdest eingeladen:**

1. Unter **Internet spielen** die Auswahl **Ich habe eine Einladung** verwenden.
2. Die vollständige Raumeinladung einfügen, entweder mit Strg+V oder **Aus
   Zwischenablage einfügen**. Auch der vom Starter erzeugte Einladungstext funktioniert.
3. **Raum beitreten + S7 starten** wählen. Die Verbindungseinstellungen kommen
   aus der Einladung; du musst keine Adresse auswählen.
4. In S7 **Mehrspieler** öffnen, die Partie des Erstellers auswählen und beitreten.
   Er muss seine Partie zuerst im Spiel erstellt haben.

Das ist keine doppelte Auswahl derselben Lobby: Im Starter verbindest du dich
mit der Gruppe; in S7 wählst du ihre konkrete Partie. Im Heimnetz entfällt die
Raumeinladung: Alle starten den LAN-Modus, dann erstellt einer die S7-Partie.

Der Raumersteller lässt seinen PC und WePlay verbunden. Beendet er sein eigenes
S7, hält WePlay den Raum für verbundene Mitspieler offen; nach deren Verlassen
schließt er automatisch. Ein unbenutzter Raum lässt sich im Starter schließen.
Als Gast verlässt du nur deine Verbindung. Während S7 läuft, ist dieser Knopf
gesperrt; das Spiel zuerst regulär beenden. Das Launcherfenster lässt sich während
des Spielens in den Infobereich minimieren.

## Was macht Discord?

Discord hilft beim **Finden von Mitspielern** und kann optional **Partien anzeigen**.
Es ist kein zusätzlicher Spielmodus. Die Verbindung läuft über den Internetraum.
Der Bot verteilt keine Raumeinladungen und verbindet euch nicht automatisch.

| Link oder Code | Zweck |
| --- | --- |
| Discord-Serverlink | Öffnet die WePlay-Community. |
| Discord-Verbindungscode | Verknüpft den Starter mit der optionalen automatischen Spieleanzeige. Kommt aus `/weplay-hilfe` → **Launcher verbinden**. |
| WePlay-Raumeinladung | Verbindet Mitspieler mit dem Raum des Erstellers. Kommt aus dessen Starter und gehört nur an die eingeladenen Spieler. |

Zum Spielen oder Beitreten ist keine Discord-Verknüpfung nötig. Wer eigene
öffentliche Partien automatisch anzeigen möchte, öffnet **Automatische
Spieleanzeige einrichten / verwalten**. Dort stehen die Einrichtungsschritte.
Das Verknüpfen aktiviert die Anzeige zunächst; der Haken lässt sich vor dem
Raumstart wieder entfernen. Die Anzeige gilt für eigene öffentliche Partien beim
Öffnen eines Raums. Sie veröffentlicht keine Raumeinladungen oder Verbindungsdaten.
Der Ersteller schickt die private Einladung weiterhin selbst.

**Private Sitzung** in S7 betrifft die Auffindbarkeit der Partie in eurer Gruppe.
Das Ausschalten veröffentlicht allein nichts auf Discord. Dafür muss die
optionale Spieleanzeige zusätzlich verknüpft und eingeschaltet sein.

## Was bedeuten die erweiterten Verbindungsoptionen?

**Automatisch** ist der empfohlene Weg. Öffentliche EasyTier-Knoten helfen beim
Verbindungsaufbau und können nötigenfalls Daten weiterleiten. Du brauchst keinen
eigenen Server. Ein solcher Knoten ist keine öffentliche S7-Lobby.

**Direkt über eigene erreichbare Adresse** ist für selbst eingerichtete
Verbindungen. Der Ersteller braucht eine von außen erreichbare numerische Adresse.
Dieser Modus verzichtet auf öffentliche Vermittlung und Weiterleitung; manche
Routerverbindungen erlauben ihn nicht. Gäste übernehmen den Modus aus ihrer Einladung.

Unter **Grafik** bedeutet Automatisch, dass WePlay moderne Grafik bei vorhandener
Unterstützung auswählt. Kompatible Grafik ist der Ausweichmodus bei Grafikproblemen.
**Speichern** übernimmt die Einstellungen für den nächsten Spielstart.

Weitere Details: [Internetverbindung](RC3-INTERNET.md),
[Discord-Anzeige](DISCORD-INTEGRATION.md), [LAN](LAN-SPIELER.md).
