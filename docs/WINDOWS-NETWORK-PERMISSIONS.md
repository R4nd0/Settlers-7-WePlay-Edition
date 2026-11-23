# Windows-Netzwerkfreigaben in WePlay

Die Einrichtung bietet **Windows-Netzwerkfreigaben für diese Kopie einrichten**
an. WePlay fordert die Windows-Bestätigung nach dem Anlegen und Prüfen der
Spielkopie an. Eine abgelehnte Freigabe lässt die fertige Kopie bestehen und
wird für den jeweiligen Schritt angezeigt.

| Freigabe | Wann WePlay sie anfordert | Was erhalten bleibt |
| --- | --- | --- |
| Windows-Netzisolation für Gold, Ubisoft History und Steam History | Bei gewählter Netzwerkeinrichtung oder beim ersten Spielstart, falls die Prüfung die Regeln noch nicht bestätigt | Die Windows-Filter gelten für den konkreten Spielpfad. Auf demselben Pfad prüft WePlay sie vor dem Start. |
| Privates LAN für Ubisoft History und Steam History | Bei gewählter Netzwerkeinrichtung oder über die LAN-Freigabe in WePlay | Die Regeln gelten für die vorgesehenen Programme der Kopie im privaten Windows-Netzprofil. WePlay entfernt die von ihm verwalteten Regeln beim Bereinigen dieser Kopie. |
| LAN-Spielverkehr zu zugelassenen Mitspielern | Beim LAN-Start, wenn das Heimnetz und der Koordinator feststehen | Die zeitlich begrenzten Windows-Filter werden für die laufende Sitzung eingerichtet und danach freigegeben. |
| EasyTier-Adapter und Firewall-Regeln für private und öffentliche Internetpartien, einschließlich gemischter Räume | Beim Öffnen des konkreten Raums. Die Regeln hängen vom erzeugten Adapter, der zugeteilten Adresse und dem Spielpfad ab. | WePlay beendet den Adapter und entfernt die zugehörigen Regeln nach der Sitzung. |
| Zustimmung zum Internetspiel und zur Sichtbarkeit eines Raums | Beim Verbinden oder Veröffentlichen einer Partie | Die Zustimmung bezieht sich auf den ausgewählten Vorgang. Die separate Discord-Verbindung lässt sich im Launcher verwalten. |
| Installations- oder Update-Ziel außerhalb des eigenen Benutzerbereichs | Windows entscheidet beim Zugriff auf den ausgewählten Ordner | Die Berechtigungen des Zielordners bleiben unter Windows-Verwaltung. Die Standardinstallation verwendet den Benutzerbereich. |

WePlay speichert die Auswahl für die nächste Reparatur. Erstellt eine Reparatur
eine neue Kopie oder ändert sich der Pfad, richtet WePlay die pfadgebundenen
Regeln für die neue Kopie ein. Windows kann die Änderung erneut bestätigen
lassen. Die Netzwerkauswahl speichert weder Administratordaten noch ein
Windows-Zugriffstoken.
