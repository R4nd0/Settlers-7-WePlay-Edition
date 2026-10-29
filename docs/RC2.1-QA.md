# RC2.1 — public LAN match creation

Recorded 2026-10-29. This release changes the prepared multiplayer menus only;
the native r3 adapter and service binaries remain unchanged. The known desync
is still unresolved.

## Result

In WePlay multiplayer creation, Private session starts unchecked and disabled.
The selection is cleared and its widget disabled again during updates. Creation
passes false to the existing SetIsPrivate / SessionCreate boundary, and the
host-lobby handoff cannot restore a stale true option. The original installation,
campaign/skirmish scripts and server privacy checks are not altered.

WePlay uses its local service even with the engine's legacy LAN boolean false.
The rule therefore covers this actual multiplayer path as well as true/nil
legacy values; it is not conditional on a flag that misses the shipped flow.
The supported WePlay multiplayer service is local/LAN; this does not change an
original Ubisoft online installation or expose a private session afterward.

## Verification

- Complete Python suite: 287 cases, 286 passed and one unrelated opt-in
  original-game integration skipped. The new real-menu integration was enabled.
- Seven new archive/Lua checks include the actual pinned menu archive,
  unchanged-member verification, Lua 5.1 compilation of both changed scripts,
  full creation-script execution for false/true/nil engine flags, forced stale
  checkbox state, submitted privacy=false, and stale host-option rejection.
  The old-menu negative control reproduces its checked/enabled checkbox.
- The packaged embeddable Python independently applies the new transformation
  and reproduces the exact declared archive hash. Lupa 2.8 / Lua 5.1 is a local
  test dependency only and is not shipped in Setup.
- A separate unused owned game copy was updated through backed-up, hash-guarded
  menu and graphics transactions. Normal LAN startup reached the creation
  screen. A supported computer-use capture visibly showed Private Sitzung
  unchecked and disabled; the user independently confirmed it with a screenshot.
  The test game then exited normally with code0. No private session was silently
  made public in the service and no multi-PC join was claimed.
- Read-only memory monitoring recorded 103 samples, pressure0, peak private
  allocation500MiB and no monitor failure during that menu test.
- The actual candidate Setup update exits0. Its real preparation path creates a
  new full game copy, applies the old join delta followed by the new creation
  delta, installs the graphics profile and completes full manifest validation.
  The final result is installed=true / needs_repair=false / update_available=false.
  Saved local identity, name and graphics preferences were retained byte for byte;
  the previous copy was retained and the original game was unchanged.
- The compiled WinForms version is RC2.1. Its bundled-runtime component render
  and bilingual/display checks are also run before delivery.

Only sessionCreation.lua and hostLobby.lua change inside the archive.
Previous archive SHA-256:
`2b1ad77ab1afda98e9d9aab8bbc91279607e1bbbac506d2b52de675550de08ad`.
New archive SHA-256:
`6966c878749f668de12b13126d971958bf1ba8e5952e5d7f2b224819dcd877eb`.

## Upgrade and remaining limits

Install RC2.1 on every PC and complete Update / repair in the launcher before
playing together. A new completed copy becomes active atomically and retains
personal data. The changed archive fingerprint deliberately prevents mixing
RC2 and RC2.1 sessions. GPU models and display settings may still differ.

This focused checkbox verification is not a new two-PC/three-PC match,
endgame or long-session qualification. It does not fix the known desync.
The installer remains unsigned and includes no original game data.

[Player guide](LAN-PLAYERS.md) · [Deutsch](LAN-SPIELER.md) ·
[Earlier installer verification](RC2-QA.md)
