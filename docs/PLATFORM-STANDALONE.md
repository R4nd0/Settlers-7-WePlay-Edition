# Plattformunabhängiges WePlay unter Windows

Diese Laufzeit unterstützt die bekannte Gold Edition 1.12.1396 sowie die
History Edition von Ubisoft und Steam. Für die vorbereitete Kopie werden weder
Steam noch Ubisoft Connect benötigt. Die Originalinstallation bleibt erhalten.
Der Installer erstellt eine eigene Kopie aus den vorhandenen Spieldateien.
Ein WePlay-Installationspaket enthält keine Spieldateien oder Plattformkonten.

Start über den WePlay-Starter. Bei der Steam-Ausgabe bleibt die reguläre,
gepackte EXE auf Platte unverändert. Ihr exakt geprüfter Spielcode wird vor
dem ursprünglichen Programmeinstieg im eigenen Prozess bereitgestellt.
Die Plattformgrenzen enthalten keine Steam-/Ubisoft-Anmeldung, Besitz-Tickets
oder Verbindung zu den Plattformclients. Unbekannte Programmstände werden
abgewiesen.

Beim ersten Start einer Kopie benötigt die Windows-Netzisolation eine
Administratorbestätigung. Vier dauerhafte, ausschließlich auf ihren EXE-Pfad
begrenzte Filter sperren Nicht-Loopback-Verbindungen für IPv4 und IPv6.
Globale Firewall-Einstellungen und Regeln anderer Anwendungen bleiben erhalten.
Der Starter prüft die Filter vor dem Spielstart. Die Spiel-EXE darf keine
Unterprozesse starten. Direkte Starts außerhalb des WePlay-Starters werden
bereits beim Laden abgewiesen.

Im WePlay-LAN gelten zusätzliche, kurzlebige Freigaben ausschließlich für
die zugelassenen privaten Mitspieler und UDP-Spielports 3074, 6200, 9103 und
9111. Öffentliche Ziele und andere Protokolle bleiben gesperrt. Diese Freigaben
überstimmen keine Sperren anderer Firewall-Anbieter. Ohne Aktualisierung des
Starters verfallen sie; sein Ende schließt die zugehörige Windows-Filtersitzung.
Die dauerhaften Sperren bleiben bestehen. Der Spielprozess läuft weiterhin mit
normalen Benutzerrechten.

Spielstände und Einstellungen bleiben innerhalb der Kopie:

| Ausgabe | Profilordner |
| --- | --- |
| History, Ubisoft oder Steam | `LocalData/ProfileRoot/THE SETTLERS 7 - History Edition` |
| Gold | `LocalData/ProfileRoot/Profiles` |

Lokale WePlay-Sicherungen unter `LocalData/saves` bleiben ebenfalls erhalten.
Reparaturen verwenden die mitgesicherten Originaldateien innerhalb der Kopie.
Eine neue Originalinstallation ist dafür nicht erforderlich. Bei einem
verschobenen Ordner wird die Netzisolation für den neuen EXE-Pfad eingerichtet.
Spielstände aus einem bereits benutzten Ordner niemals durch eine ältere
Sicherung ersetzen. Vor Kopieren, Verschieben oder Reparieren das Spiel schließen.

Die vorbereitete Spielkopie einschließlich Spielständen ist eine private
Sicherung. Öffentlich weitergegeben werden ausschließlich die Installationspakete.

## Prüfstand

Der native x86-Starter, die 355 unveränderten Audio-Weiterleitungen und die
Windows-Unterprozesssperre wurden mit echten Testprogrammen geprüft.
IPv4/IPv6-TCP und -UDP wurden gegen echte Empfänger auf demselben Rechner mit
einer ungefilterten Kontrollkopie verglichen. Loopback funktioniert mit den
Sperren weiterhin. Eine private Spielport-Freigabe und ihr Entzug ohne
Starter-Heartbeat wurden durch tatsächliche Zustellung geprüft.

Die neue Paketintegration wurde in getrennten Kopien tatsächlich gestartet:

| Ausgabe | Welt, Speichern, Kaltstart und Laden | Debugger mit regulärer EXE |
| --- | --- | --- |
| Steam History, reguläre gepackte EXE | Bestanden | Elf echte Stopps, in/over/out, lokale Werte und Hook-Wiederherstellung bestanden |
| Ubisoft History, reguläre EXE | Bestanden | Derselbe Test bestanden |
| Gold 1.12.1396 | Bestanden, mit dem mitgelieferten Python-Starter | Derselbe Test bestanden |

Alle sechs beobachteten normalen Spielintervalle bestätigten die Windows-Sperre
im tatsächlichen Spielprozess vor dem ursprünglichen Programmeinstieg.
IPv4 und IPv6 wurden gegen eigene Nicht-Loopback-Empfänger geprüft, während
die Loopback-Kontrolle funktionierte. Insgesamt 1.733 Prozess-/Socket-/Modul-
Beobachtungen fanden keine Plattformclients, Plattformclient-Module oder
Nicht-Loopback-Verbindungen. Dies sind periodische Beobachtungen; die
Durchsetzung beruht zusätzlich auf den vier vorab geprüften Windows-Filtern.
Es wurden keine Paketnutzdaten aufgezeichnet. Alle Intervalle endeten regulär
mit Code 0 und unveränderten gemeinsamen Profilen sowie EXE-Dateien.

Der erste Gold-Kandidat blieb am Ladeoverlay stehen: Der Ersatz des stillgelegten
Plattformchats enthielt keine `hide()`-Methode, die das originale Spiel beim
Aufbau der Spieloberfläche aufruft. Die korrigierte Seite erfüllt den gesamten
ursprünglichen Lebenszyklus ohne Plattformaufrufe. Ein Lua-Test mit dem wirklich
installierten Archiv reproduziert den alten Fehler und besteht mit der Korrektur.
Der folgende Einzelclientlauf erreichte die bedienbare Welt und speicherte eine
neue Partie. Ein getrennter Kaltstart bestand elf Debugger-Stopps und lud diese
Partie bis zur bedienbaren Welt. Beide endeten regulär mit Code 0.

Der kalte Import der Frida-Laufzeit wird vor dem kontrollierten Spieleinstieg
erledigt. So verbraucht der Debugger nicht dessen begrenzte Wartezeit mit seiner
eigenen Einrichtung. Die Zeitgrenzen und Schutzprüfungen bleiben erhalten.

Die [Prüfquittung](receipts/platform-standalone-20261112.json) enthält die
bestandenen Tests und die früheren Gold-Fehlversuche getrennt. Sie bewahrt auch
fehlgeschlagene Speichermonitor-Intervalle; diese zählen nicht als lückenlose
Speicherüberwachung. Die Netzwerkbeobachtung und Windows-Sperrprüfungen sind
separate Belege. Laufende Spielwelt-Breakpoints, längere Debuggersitzungen und
andere Lua-Zustände sind noch nicht qualifiziert.
Mehrspieler-Synchronisation und Gold/History-Crossplay haben eigene Prüfstände.
