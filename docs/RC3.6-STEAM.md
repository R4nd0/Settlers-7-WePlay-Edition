# WePlay RC3.6 — Steam Preview

RC3.6 adds the identified Steam History Edition build alongside the existing
Ubisoft Connect build. Each player supplies their own original installation;
WePlay prepares a separate copy. Steam must be running for the Steam edition.
The original Steam executable and SDK remain in use. Unknown game builds are
rejected rather than patched with another edition's addresses.

## Changes since RC3.5

- Separate guarded Steam native profile, source detection and preparation,
  with the required Steam App ID supplied only to the game child process.
- Clearer Solo, LAN and Internet start choices, explanations of rooms versus
  in-game matches, and guidance for sharing invitations through Discord.
- Optional Clear landscape and Highest object detail graphics presets,
  including optional 16x anisotropic filtering with verified DXVK. Existing
  preferences do not silently acquire a different quality preset.
- Kartograph initialization correction when the simple main menu is enabled,
  plus the memory preflight introduced after RC3.5. The Steam Cartographer has
  not been qualified by a real editor test.
- Continued removal of personal maintainer details and identifying support
  links. Attribution uses WePlay Community and R4nd0.

DXVK 3.1.1, automatic LAN discovery, private Internet rooms through EasyTier,
Discord invitation tools, compiled Windows helpers and signed update checks
remain included. Two physical cores without SMT siblings remain the default;
the local 2/4/8-core comparison showed no advantage from extra cores in the
tested 60-FPS configuration. No eight-core experimental build is shipped.

## Install or upgrade

Close S7 and WePlay, run this release's Setup.exe, and use the existing WePlay
application folder for an upgrade. Allow the launcher to prepare its matching
separate game copy. Saves/settings are preserved by the managed update flow.
Use this same RC3.6 package on all participating PCs; RC3.5 prepared copies are
not a matching multiplayer build. The game remains 32 bit; the launcher needs
Windows x64, Windows 10 build 19045 or later, and space for the separate copy.

Choose Modern (DXVK) graphics and the intended GPU where supported. Native
graphics remains available, but the new Steam runtime observations used DXVK.
Graphics quality and display choices are local preferences. Do not copy another
player's prepared game or LocalData directory to set up a second PC.

## Observed behavior and known limits

The original Steam executable reached the main menu, local login and session
browser. Two isolated Steam-derived clients on one PC joined a shared lobby,
loaded a map and completed construction commands from both host and guest.
The opposite hosting direction worked after fresh restarts. These observations
precede packaging and are separate from installer/backend fixture tests.

Changing host roles after a match without restarting still produced session
error 0 / link-reach failure. Restart both games before changing roles. A fresh
two-physical-PC Steam test, Steam/Ubisoft mixed matches, Steam Internet matches,
extended/endgame sessions and Steam Cartographer remain unqualified.

This is an experimental preview. Previously observed desyncs and intermittent
system/menu hangs are not declared fixed. The short DXVK/core-count tests do
not establish hardware-independent stability or performance. The installer and
helpers do not yet have a trusted Authenticode publisher signature. Signed
update metadata authenticates the download; it does not replace that signature.

## Updates and credits

Packaging verification covered fresh separate-copy preparation from both
identified originals, package/source fingerprints, absence of game/private
state in Setup, and the maintainer-privacy scan. The original executables and
UI archives were unchanged. Targeted installer/backend/graphics/LAN/Internet
and update regressions passed, including 19 signature/update checks and eight
read-only checks against the packaged Windows helper. The packaged DE/EN UI
self-test passed its 14 groups and the private invitation-channel roundtrip.
Some opt-in process/installer lifecycle fixtures were not repeated for this RC.
These packaging checks did not start another game or multiplayer match.

Version `2.0.0-rc3.6` uses preview update sequence **4**, following the RC3.5
privacy rebuild's sequence 3. The release includes Setup.exe, its checksum,
the matching signed weplay-update.json, and EasyTier corresponding source.
The prepared-game protocol revision remains 6; exact package/file matching is
still required. No proprietary game files, local identity or signing key ships.

Thanks, gratitude and love to [Eisenmonoxid and S7Patcher](https://github.com/Eisenmonoxid/S7Patcher)
for the foundational work that made WePlay possible.

## Deutsch

Neu sind die erkannte Steam History Edition, die verständlichere Launcherführung,
optionale DXVK-Qualitätsprofile und die Kartograph-Korrektur für das einfache
Hauptmenü. Steam muss für diese Ausgabe laufen. Beide Spieler benötigen denselben
RC3.6-Dateistand. Vor einem Wechsel der Hostrolle beide Spiele neu starten.
Steam mit zwei realen PCs, gemischte Steam-/Ubisoft-Partien und Langzeitstabilität
sind noch nicht bestätigt; bekannte Desyncs sind weiterhin offen.
