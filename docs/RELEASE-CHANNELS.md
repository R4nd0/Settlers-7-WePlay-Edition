# Stable und Development

Der Windows-Launcher speichert die Auswahl unter **Updates → Release-Kanal**.
`rc4.14` liefert Stable als Standard. Der gewählte Kanal gilt über Neustarts
und Updates hinweg. Die Auswahl ändert sofort, welche Angebote der Launcher
prüft. Ist die installierte Version neuer als der Stand des gewählten Kanals,
bietet der Launcher dort erst wieder eine neuere Version an.

| | Stable | Development |
| --- | --- | --- |
| Git-Verlauf | `main` | `develop` |
| Tags | `rc4.14`, `rc4.15` | `rc4.15-dev.1`, `rc4.15-dev.2` |
| GitHub | Release mit Kennzeichnung als aktueller Stable-Stand | Prerelease |
| Signierter Kanal | `stable` | `preview` |

## Entwicklung und Freigabe

1. `develop` vom letzten Stable-Commit anlegen. Feature- und Debugzweige
   beginnen auf `develop` und gehen nach Review und Tests dorthin zurück.
2. Für eine Stable-Version einen Branch `release/rc4.15` von `main` anlegen.
   Geprüfte und zusammengehörige Änderungen aus `develop` übernehmen. Den
   exakten Installer bauen und Update, betroffene Spielpfade sowie die drei
   Hostrollen prüfen, wenn Änderungen Crossplay betreffen.
3. Den abgenommenen Commit nach `main` übernehmen und dort taggen. Die
   bereits geprüften Installer-Bytes veröffentlichen. `develop` anschließend
   mit `main` abgleichen. Kleine Stable-Korrekturen beginnen auf `main` und
   gehen nach der Veröffentlichung auch zurück nach `develop`.

Jeder veröffentlichte Build erhält eine neue, über beide Kanäle steigende
Update-Sequenz. Stable liest den aktuellen regulären GitHub-Release und
prüft den signierten Stable-Kanal. Development liest Preview-Manifeste.
Beide Kanäle speichern den höchsten geprüften Manifeststand separat. Für den
Anstieg von `rc4.13` auf `rc4.14` verweisen einmalig auch die bisherigen
Preview-Manifestnamen auf denselben signierten Installer. Bei späteren
Stable-Releases genügen die Stable-Manifeste.
