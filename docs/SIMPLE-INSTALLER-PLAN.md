# Einfacher Installer und gemeinsamer Starter

Plan vom 28.10.2026, auf Basis des Grafik-RC `2e19e20`.
Status: als RC2 umgesetzt. Der bisherige Grafik-RC bleibt unverändert.
Build und Bedienung: [RC2-START.md](RC2-START.md), [RC2-BUILD.md](RC2-BUILD.md).
Die abschließenden Testbelege werden in [RC2-QA.md](RC2-QA.md) festgehalten.

## Ziel für Spieler

Eine Datei `Setup.exe` installieren, anschließend die Verknüpfung
`Settlers 7 WePlay Edition` verwenden. Die Installation erledigt Kopie, Patch, Grafikprofil,
Laufzeitdateien und Verknüpfungen in einem Ablauf. Alle späteren Funktionen
sind im gemeinsamen Starter erreichbar. Deutsch und Englisch sind enthalten.

Im Normalfall muss kein Pfad eingegeben werden. Wenn die vorhandene
Spielinstallation nicht eindeutig erkannt wird, genügt einmal eine
Ordnerauswahl. Der vorgeschlagene Zielordner kann über „Ändern“ gewechselt
werden. Weitere Werkzeuge erhalten die gespeicherten Pfade automatisch.

## Installation

1. Sprache aus Windows übernehmen, mit sichtbarem Wechsel Deutsch/English.
2. Unterstützte History-Installation über Installationsregistrierung und
   bekannte Bibliotheksorte suchen. Kandidaten anhand der vorhandenen
   Versions-/Dateiprüfungen bestätigen. Keine Kontocaches durchsuchen und
   keine pauschale Suche über ganze Laufwerke.
3. Bei mehreren gültigen Treffern eine verständliche Auswahlliste anbieten;
   ohne Treffer „Spielordner auswählen“. Eine ungültige Version verständlich
   erklären, bevor eine Kopie angelegt wird.
4. Einen beschreibbaren Standardzielordner im Benutzerbereich vorschlagen,
   freien Speicher prüfen und „Installieren“ anbieten. Original-Spieldateien
   bleiben unverändert. Python und alle erforderlichen Laufzeiten mitliefern.
5. Kopieren, Patchen, Prüfen und Grafikeinrichtung automatisch abarbeiten.
   Jede Phase zeigt Fortschritt und eine klare Beschreibung; eine fertig
   kopierte Datei ist nicht gleich eine abgeschlossene Installation.
6. Erst nach vollständigem Erfolg die Startverknüpfung veröffentlichen.
   Abschluss: „Siedler 7 starten“. Kein zusätzlicher Profil-Aktivierungsstart.

Die normale Installation läuft ohne Administratorrechte. Die private
LAN-Freigabe wird bei Bedarf aus dem Starter eingerichtet und verwendet die
erforderliche Windows-Bestätigung. Ein abgelehnter Dialog ergibt eine
verständliche Meldung mit erneutem Versuch im selben Fenster.

## Gemeinsamer Starter

Eine Oberfläche mit „Spielen“, „LAN spielen“, „Einstellungen“ und „Hilfe“.
Grafik- und Namensauswahl werden gespeichert; sie erscheinen nicht bei jedem
Spielstart erneut als Pflichtdialoge. Der Starter verwaltet seine eigenen
Hilfsprozesse und kann während des Spiels in den Hintergrund gehen.

- Spielen: Voraussetzungen prüfen und die vorbereitete Kopie starten.
- LAN spielen: Namen festlegen und starten. Der gemeinsame LAN-Dienst wird
  automatisch verbunden; IP-, Netzwerk- und externe Raumauswahl entfallen.
  Die Partie wird genau einmal im Mehrspielermenü des Spiels gewählt.
- Einstellungen: Grafikmodus, Grafikkarte, Zielbildschirm, Auflösung,
  Fenster/randlos und unterstützte Qualitätsoptionen an einem Ort anbieten.
  Gültige, konservative Vorgaben wählen. Entfernte Bildschirme und geänderte
  Hardware vor dem Start auffangen.
- Hilfe: „Installation prüfen/reparieren“, „Kompatible Grafik verwenden“
  und Diagnosebericht im selben Programm. Technische Einzelstarter bleiben
  interne Entwicklungswerkzeuge.

Lokale Dienste und der Randlos-Controller bleiben an ihren tatsächlichen
Spielprozess gebunden. Eine LAN-Lobby mit anderen aktiven Spielern darf beim
Schließen des eigenen Spiels nicht versehentlich beendet werden. Der bestehende
unabhängige Koordinator und seine Lebensdauer bleiben berücksichtigt.

## Automatische Grafik mit überprüfter Eignung

„Automatisch (empfohlen)“ prüft die tatsächlich nutzbare Grafikunterstützung
und wählt ein passendes Profil. Der Grafikkartenname allein ist kein
Kompatibilitätsnachweis. Für DXVK ist ein begrenzter, separat beendbarer
Renderer-Probetest mit Zeitlimit vorgesehen; es wird kein großes Spiel im
Hintergrund zu diesem Zweck gestartet. Ein nicht erfolgreich geprüftes Profil
wird nicht ungefragt als geeignet aktiviert.

Profilaktivierung, Wasserkorrektur und Rückkehr zum Standardrenderer erfolgen
über die bereits vorhandenen, gesperrten und wiederherstellbaren Transaktionen.
Fehler bieten eine klare Aktion wie „Mit kompatibler Grafik erneut versuchen“.
Speichergrenzen und die vorhandenen Startschutzprüfungen bleiben wirksam.

LAN benötigt besondere Behandlung: Der Renderer gehört derzeit zum
Dateifingerabdruck. Der Starter muss Profilabweichungen vor dem Spielstart
erkennen und einen passenden, auf diesem Rechner geprüften Profilwechsel
direkt anbieten. Ist das Lobby-Profil auf dem Rechner nicht ausführbar, muss
die Oberfläche eine gemeinsame Standardgrafik-Lobby als Alternative erklären.
Ein automatischer Wechsel aller Teilnehmer erfordert eine eigene Abstimmung
vor Partiestart und gehört nicht stillschweigend zum ersten Installer.
Während laufender Partien werden weder Profile umgeschaltet noch
Inhaltsprüfungen gelockert.

## Technische Umsetzung

- Inno Setup erstellt eine einzige Setup-EXE mit Installation im Benutzerbereich,
  Verknüpfungen, Versionsverwaltung und Deinstallation. Lizenzbedingungen für
  den vorgesehenen Vertrieb werden beim Einführen des Build-Werkzeugs geprüft.
- Ein kompilierter C#-WinForms-Starter übernimmt die Oberfläche. Die dafür
  nötige Windows-Desktop-Laufzeit wird mitgeliefert; die bisherige reine
  .NET-Core-Serverlaufzeit reicht für WinForms allein nicht aus. Keine
  Laufzeitinstallation oder Kommandoeingabe durch den Spieler erforderlich.
- Die vorhandenen Python-Module für Kopie, Prüfsummen, Profile und LAN werden
  als interne Komponenten wiederverwendet. Der GUI-Prozess startet sie ohne
  sichtbares Konsolenfenster und verarbeitet begrenzte, strukturierte
  Fortschritts-/Fehlerereignisse. Pfade kommen aus der Installationsverwaltung.
- UI-Auswahl und Startvorbereitung werden getrennt: Ein gemeinsamer Aufruf
  übernimmt validierte Einstellungen und erzeugt Umgebung, Spielargumente
  und Fenstersteuerung unter der bestehenden Startsperre. Einfach
  `choose_graphics=False` zu setzen wäre falsch: Dieser Schalter überspringt
  aktuell auch Grafikargumente und im lokalen/manuellen LAN den Controller.
- Die Spiel-/ABI-/Dateiguards sowie Profiljournal und unveränderte
  Simulationsprüfungen bleiben bestehen. Fehlermeldungen erscheinen als
  verständlicher Text mit einer konkreten Folgeaktion; Details sind optional.
- Oberfläche über Sprachressourcen lokalisieren. Technische Fehlerschlüssel
  und interne Protokolle werden getrennt von angezeigten Texten gehalten.
- Setup und Launcher werden versioniert gebaut und vor öffentlicher Verteilung
  für eine echte Herausgebersignierung vorbereitet. Windows-Sicherheitsdialoge
  werden nicht umgangen.

## Wiederholung, Update und Entfernen

Erneutes Setup erkennt die registrierte Installation und bietet Aktualisieren
oder Reparieren. Die bisherige funktionsfähige Version bleibt bis zur
erfolgreichen Übergabe erhalten. Änderungen an geladenen Adaptern bleiben
gesperrt; ein abgebrochener Schritt muss fortsetzbar oder rücksetzbar sein.

Manuell angelegte alte RC-Kopien haben noch keine Installationsregistrierung.
Sie können optional einmal per Ordnerauswahl geprüft werden. Eine unbekannte
oder nicht sicher migrierbare Kopie führt zum Angebot einer neuen getrennten
Installation, nicht zu einem unkontrollierten Überschreiben.

Die Deinstallation kennt nur die eigenen angelegten Dateien und privaten
Firewallregeln. Spielstände und lokale Profile werden standardmäßig behalten;
fremde oder nachträglich hinzugefügte Dateien werden nicht pauschal entfernt.

## Reihenfolge und Abnahme

1. Auswahl von Darstellung trennen, Fortschritts-/Fehlerschnittstelle und
   Installationszustände einführen; vorhandene Prüfungen weiterverwenden.
2. Durchgehenden Ablauf Setup → automatische Vorbereitung → ein Starter
   bauen. Moderne Grafik und kompatibler Rückweg werden im Ablauf integriert.
3. Grafikeinstellungen, automatischen LAN-Aufbau und bedarfsweise private Netzwerkfreigabe
   in diese Oberfläche übernehmen; gespeicherte Werte beim nächsten Start nutzen.
4. Reparatur, unterbrochene Installation/Updates und Deinstallation vervollständigen.
5. Eine neue RC-Setup-EXE erst nach den folgenden Bedien- und Fehlerprüfungen abgeben.

Abnahmekriterien:

- Sauberes Windows ohne vorinstalliertes Python/.NET: eine Setup-Datei genügt.
- Automatisch gefundene Originalinstallation: keine Pfadeingabe. Ohne Treffer:
  höchstens eine geführte Quellauswahl; Ziel ist vorbelegt.
- Kein CMD-/PowerShell-Fenster und keine separat zu startende Aktivierung,
  Wiederherstellung, Prüfung oder Netzwerkfreigabe im normalen Ablauf.
- Normale Installation bis „Bereit“ mit höchstens drei Hauptaktionen, sofern
  die Quelle eindeutig gefunden und der vorgeschlagene Zielort beschreibbar ist.
  Ein späterer lokaler Spielstart erfolgt über dieselbe Verknüpfung und „Spielen“.
- Einstellungen bleiben über Neustarts erhalten; Entfernen eines Bildschirms,
  fehlende Grafikunterstützung und abgelehnte LAN-Freigabe sind geführt behandelbar.
- Falsche Spielversion, wenig Speicherplatz, laufendes Spiel und unterbrochenes
  Kopieren hinterlassen keine scheinbar fertige oder beschädigte Installation.
- Grafikprofilwechsel, Rücksetzung und Versionsabgleich funktionieren auch im
  LAN-Vorlauf; unpassende Teilnehmer werden vor dem Spiel verständlich abgefangen.
- Original-Spieldateien, persönliche Spielstände und Dateien außerhalb der
  verwalteten Installation bleiben bei Update und Entfernen erhalten.

Der einfachere Installer behebt die Bedienung. Ein D3D11-/64-Bit-Port,
FSR und der offene LAN-Desync bleiben davon unabhängige Arbeiten.

## Geprüfte Grundlagen

- [Inno Setup: einzelne EXE, Sprachen, Verknüpfungen und Deinstallation](https://jrsoftware.org/isinfo.php)
- [Inno Setup: Installation ohne erhöhte Rechte](https://jrsoftware.org/ishelp/topic_setup_privilegesrequired.htm)
- [Microsoft: Windows Forms](https://learn.microsoft.com/en-us/dotnet/desktop/winforms/overview/)
- [Microsoft: mitgelieferte .NET-Laufzeit](https://learn.microsoft.com/en-us/dotnet/core/deploying/)
