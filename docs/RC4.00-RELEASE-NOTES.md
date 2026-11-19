# The Settlers 7: WePlay Edition — RC4.00

**Community-Vorschau / Community preview** · MixedCandidate7 · Quelle / source [`a7d0349`](https://github.com/R4nd0/Settlers-7-WePlay-Edition/commit/a7d03497ae84c4654e2a75c75800674cce291616)

RC4.00 vereint **Gold Edition 1.12.1396, Ubisoft History Edition und Steam History Edition** in einem Windows-Installer. Der gemeinsame Launcher bereitet eine getrennte Spielkopie mit eigenem Profil vor. Jede Person benötigt unterstützte Originaldateien. Für die vorbereitete Kopie sind Steam und Ubisoft Connect zur Laufzeit nicht erforderlich.

- **Gemischter Multiplayer** ist als Vorschaufunktion enthalten. Die unterstützte Zielbesetzung auf Monhaim Valley besteht aus je einem Client jeder Edition und einem KI-Gegner.
- **DXVK** ist der feste Grafikpfad für alle drei Editionen und benötigt Vulkan. ReShade-Effekte bleiben ohne Overlay verfügbar. Der Startpfad nutzt bis zu vier verfügbare physische Kerne mit je einem logischen Prozessor.
- **NEO Logistics** nutzt in gemischten Sitzungen FIFO und Einzelprioritäten. History-interne Sitzungen verwenden NEO6 mit den Korrekturen für Lagerübergaben und deterministische Verbraucherauswahl.

**Installation:** `Setup.exe` ausführen und die jeweilige Originaledition auswählen. Windows 10 ab Build 19045 oder Windows 11 (x64) und ein Vulkan-fähiger Grafiktreiber sind erforderlich. Für ein Update WePlay und das Spiel schließen. Vor gemischten Speichertests Spielstände und Profile sichern und alle Teilnehmer auf dieselbe Paketversion bringen.

### English

One Windows installer covers supported **Gold 1.12.1396, Ubisoft History Edition and Steam History Edition**. Each uses a separate game copy and profile with its own original game files. DXVK and Vulkan are required. The launcher uses up to four available physical CPU cores. Mixed multiplayer is available as a preview. Mixed NEO uses FIFO and individual priorities, while History-only games use NEO6.

Run `Setup.exe` to install. Close WePlay and the game before updating. Back up profiles and saves before testing mixed saves, and use the same release on every client.

[Grafikeinstellungen / graphics settings](VISUAL-QUALITY.md) · [Grafikmessungen / graphics measurements](GRAPHICS-RC319-MEASUREMENTS.md) · [Internet-Verbindung / Internet join](RC3.21-INTERNET-JOIN.md)

Maintainer / Herausgeber **WePlay Community / R4nd0**
