# WePlay rc4.06 für Windows / for Windows

- **Grafikmenü:** Die Auswahl der Grafikpipeline bestimmt die sichtbaren Optionen. Kantenglättung steht als einzelne Auswahl zur Verfügung. Die Vorlagen wählen SMAA, wo es verfügbar ist. Die Vorlage ReShade Maximum kombiniert SMAA mit hohen Modelldetails, Mittsommer-Farben und Ultra-Schatten.
- **SMAA in den DXVK-Kompatibilitätsmodi:** DXVK-Async und DXVK-GPLAsync prüfen SMAA beim Spielstart mit dem Grafikadapter. Falls die Prüfung scheitert, verwendet das Spiel seine Standard-Kantenglättung.
- **Modelldetails und Schatten:** Der LOD-Regler bietet Aus, +50 %, +100 %, +150 % und +200 %. Die Schattenreichweite kann derselben Stufe folgen. Die Schattenauflösung ist separat einstellbar.
- **Installation:** Bei Steam History, Ubisoft History und Gold bleiben vorhandene D3D9-Wrapper und DXVK-Konfigurationen in der Originalinstallation. WePlay richtet die Grafikpipeline in der separaten Kopie ein.

- **Graphics menu:** The selected graphics pipeline determines which controls are visible. Antialiasing has a single selector. Presets choose SMAA where available. The ReShade Maximum preset combines SMAA with high model detail, Midsummer color grading and Ultra shadows.
- **SMAA in the DXVK compatibility modes:** DXVK-Async and DXVK-GPLAsync check SMAA on the graphics adapter at launch. If the check fails, the game uses its standard antialiasing.
- **Model detail and shadows:** The LOD slider offers Off, +50%, +100%, +150% and +200%. Shadow distance can follow the same setting. Shadow resolution remains separate.
- **Installation:** For Steam History, Ubisoft History and Gold, existing D3D9 wrappers and DXVK configurations stay in the original installation. WePlay sets up the renderer in its separate copy.

[Grafikeinstellungen / Graphics settings](VISUAL-QUALITY.md) · [Windows-Netzwerkfreigaben / Windows network permissions](WINDOWS-NETWORK-PERMISSIONS.md)
