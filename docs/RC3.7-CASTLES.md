# WePlay RC3.7 — Castle Restoration

**The community fixes what Ubisoft left behind. Your castle is back in multiplayer.**

You built it. You customized it. Your friends should see it. The History Edition
left Castle Forge designs missing from multiplayer matches, turning a much-loved
personal touch into a longstanding frustration. RC3.7 brings those designs back
where they belong: on the map, visible to the other players. A small castle,
a very satisfying community victory.

## The headline fix

- Your selected Castle Forge design now appears inside multiplayer matches.
- Other players receive your own design, and you receive theirs. This restores
  individual castles rather than applying the host's appearance to everybody.
- Both identified Ubisoft Connect and Steam History Edition builds are supported.
- Castle restoration is enabled in the normal RC3.7 adapter; no developer switch
  or extra setting is needed. Select your design in the game's Castle Forge.
- Cosmetic data uses the existing game transport, with bounded records and
  validation. No additional port or external castle service is required.

Two isolated clients on one Windows PC joined and loaded Tal von Monhaim with
different 106-part and 25-part designs. Both players saw both distinct castles,
with Ubisoft hosting and again with Steam hosting after fresh starts. Native
setup reads independently confirmed the transferred records. These are actual
match observations, separate from the packaging and synthetic regression checks.

## Also included

Failed, unconfirmed joins now release their validated matchmaking reservation
so a retry does not retain a stale slot. Delayed failure reports cannot remove
an already confirmed player. Fixed diagnostic categories improve join reports.
This does not establish a general fix for changing host roles without restarting.

All RC3.6 features remain: Steam support, automatic LAN discovery, private
EasyTier Internet rooms, Discord invitation tools, DXVK graphics choices,
compiled Windows helpers and signed update checks. Two physical CPU cores
without SMT siblings remain the shipped limit. Developer cheats remain disabled
for LAN play.

## Install and play together

Close S7 and WePlay, run **Setup.exe**, and upgrade your existing WePlay folder.
Let the launcher prepare its matching separate game copy. The managed update
flow preserves supported settings and saves; original game installations remain
untouched. Steam must be running when using the Steam edition.

**Every player must use RC3.7. Do not mix it with RC3.6 or older clients.**
Castle replication extends the native message format. The package/file checks
remain mandatory, and the release network revision is now **7**. Restart both
games before changing host roles after a match.

## Preview limits

The castle feature has not yet been qualified with three or more players, two
physical PCs, Internet rooms, save/reload or extended/endgame matches. Transfer
is limited to 512 parts and 2,048 encoded bytes; larger or unusually diverse
designs may exceed the budget and appear empty to peers while remaining visible
locally. Invalid received records are rejected. Known desyncs and intermittent
system/menu hangs are still open; this release does not bypass synchronization
checks or claim to solve them. See [technical castle verification](MULTIPLAYER-CASTLES.md).

Version **2.0.0-rc3.7**, preview update sequence **5**. Assets: Setup.exe, its
SHA-256 checksum, matching signed weplay-update.json and EasyTier corresponding
source. Update metadata is cryptographically signed; Setup and helpers still
lack a trusted Windows Authenticode publisher signature. No proprietary game
data, local identity or private signing key is distributed.

## Deutsch: Eure Burgen sind zurück!

**Die Community repariert, was Ubisoft liegen gelassen hat.** Endlich sieht man
die eigene, individuell gebaute Burg wieder in Mehrspielerpartien — und auch
die Burgen der Mitspieler. Ein langjähriges Ärgernis der History Edition bekommt
damit die Aufmerksamkeit, die es verdient. Danke an alle, die WePlay beim Testen
und Weiterentwickeln unterstützen!

Der Fix ist in RC3.7 regulär aktiv, für die erkannten Steam- und Ubisoft-Ausgaben.
Alle Mitspieler müssen denselben RC3.7 installieren. Zwei verschiedene Burgen
wurden in laufenden Partien auf beiden Clients und mit beiden Hostrichtungen
bestätigt. Tests mit zwei realen PCs, Internet und langen Partien stehen für
diesen Castle-Fix noch aus; bekannte Desyncs sind weiterhin offen. Sehr große
Burgen können das oben genannte Übertragungslimit überschreiten.

Grüße, Dank und Liebe an [Eisenmonoxid und S7Patcher](https://github.com/Eisenmonoxid/S7Patcher)
für die großartige Vorarbeit. WePlay baut auf diesem Fundament weiter.
