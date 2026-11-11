# Gold startup and quiet menu

Verified in the separate Gold laboratory copy on 2026-11-11. This ports the
History startup/menu cleanup to the independently fingerprinted Gold build.
It is a source/development milestone, not a replacement public installer.

Gold now answers only its startup-options reset question with No. The guarded
call is `40DEF8 -> 41C400`; the original boolean consumer and `RET 4` epilogue
are checked independently of History. The in-memory replacement returns false
in AL and discards one stack argument. Existing executable identity checks,
transactional installation and rollback remain. Options.ini, Startup.tag and
other message boxes retain their original handling.

`tools/gold/menu_interface.py` builds a new Interface.bba from the exact
original Gold archive. It also includes the existing local-profile correction.
The menu changes are:

- Skip both startup movies and the timed camera flight/click blocker.
- Default to the black menu without loading the background castle map.
- Remove the separate left-side Quickstart and legacy Store, code-entry,
  Uplay, My Realm, Facebook and My City links.
- Remove the Shop entry under Customize, the Expansions/store/redeem group,
  and the coin balance/shop button in the player card.
- Replace the news ticker with an empty, correctly owned frame. It keeps the
  original create/getFrame/update/destroy lifecycle without fetching feeds or
  registering promotional URL callbacks.
- Disable the expansion-advertising popup. Existing profile creation, owned
  content and progression are not rewritten by these menu edits.

The script pins the original archive plus all five original menu members,
preserves their Lua constant-table headers and checks every resulting archive
payload. Together with the two existing profile corrections, seven of the 284
file payloads change; the rest match the original bytes.

```console
python -B tools/gold/menu_interface.py "path/to/original/Interface.bba" --output work/gold-quiet-menu
powershell -NoProfile -File native/build_gold.ps1 -OutputDirectory work/gold-quiet-native -LocalSimulation
```

The first command only stages an archive. The second builds the separate Gold
adapter and runs its fixtures. Neither command installs or launches a game.
Use a backed-up test copy and the existing explicit Gold initializer; the
History loader is not compatible. The ordinary Gold preparation command and
published packages have not been changed to install this candidate.

The original Castle Forge requires the menu map. `-mapInMainMenu` explicitly
retains that mode; `-noMapInMainMenu` takes precedence. This change does not
qualify Castle Forge operation without its required map.

Validation separates authored fixtures from actual game observations:

- x86 build with `/W4 /WX`; installed replacement bytes execute with the false
  result, exact stack cleanup and an intact sentinel. Rollback and all 19
  single-byte guard rejections pass. The 75 network guard checks and Gold
  network import checks also pass outside the socket-restricted sandbox.
- All seven modified scripts compile with Lua 5.1. Full archive readback and
  member comparison pass. Authored quiet-news lifecycle and rejection tests
  pass alongside the existing Gold profile tests.
- The final candidate runs with pinned DXVK, High settings and 1920x1080.
  Startup.tag already exists; the reset question is absent and the installed
  native No instruction bytes are verified in the owned process. After the
  existing local-profile confirmation, the black main menu and Customize panel
  show the cleanup. Settings open and cancel back normally.
- The final game exits normally with code 0. Options.ini is byte-identical;
  all 16 backed-up shared save/options/profile files retain their hashes and
  timestamps. No world or match is started by this verification.

One intermediate player-card candidate failed to enter the menu because its
layout still measured a removed cash local. The final script retains an empty
local for that width calculation; the corrected candidate passes the live UI
check. It performs no Shop cash query.

The separate navigation check also found that opening and cancelling Gold's
original Settings dialog serializes DepthOfField/System defaults. The owned
diagnostic options were restored from backup before the final startup-only
test, which leaves Options.ini byte-identical. This patch does not change the
original Settings dialog.

Sanitized receipt: [Gold menu verification](verification/gold-menu-20261111.json).
The concurrent crossplay laboratories use their own pinned builds and archives;
this menu test did not deploy over or stop those clients. New crossplay builds
must refresh their source checkpoints deliberately to incorporate this native
startup change. Multiplayer qualification is tracked separately.
