# Disabled achievement notifications

The owner reported recurring achievement popups during two roughly three-hour
playtest sessions. An unavailable Ubisoft bookkeeping service was suggested as
a possible cause. That cause is not established by the UI evidence, and this
change does not invent an online achievement acknowledgement.

The prepared WePlay copy now suppresses the shared achievement popup factory.
Its returned object keeps the existing `destroy`, `update` and `isCreated`
interface, with no UI allocation and an immediately closed state. This prevents
both in-game and menu/castle achievement dialogs without changing simulation,
native code, unlocks, rewards, saved progress or the achievement catalogue.
Underlying engine achievement calculations/bookkeeping are not rewritten.

## Verified consumer behavior

Read-only inspection of the fingerprinted scripts found three consumers of
`local.menu.panel.shared.achievement`:

- `local/menu/game.lua` calls `create`, places the result in `achievementList`,
  calls `update`, and removes a result whose `isCreated` is false.
- `local/menu/panel/menu/control/mainState.lua` calls `create` for its achievement
  action, destroys an older result before replacing it, updates it, and clears
  a closed result. It does not retry a closed factory result.
- `local/menu/panel/menu/community/achievementBrowser.lua` only calls
  `getAchievementData`. This catalogue function is untouched.

The factory retains its inert object rather than returning nil, so the existing
callers never dereference a missing result. Ordinary reward-list feedback and
the separate UPlay-action path are not redirected through this factory.

## Integration contract

`tools/installer/achievement_notifications.py` exposes the pure function
`build_archive(original_bytes, recipe_dict) -> bytes`. Apply it after the existing
public-LAN-creation archive transform, using
`tools/offline-ui/achievement-notifications.json`.

| Item | SHA-256 |
| --- | --- |
| Required input archive | `6966c878749f668de12b13126d971958bf1ba8e5952e5d7f2b224819dcd877eb` |
| Patched archive | `38edae6dd347e9bc4afb9f3474285e8458f3d712dd058492f56bacc066ce15e4` |
| Required factory member | `d8fe509028de1a9a2abcef807d1cc8d4de83989857fda104d18d9b3ec23fd373` |
| Patched factory member | `e390b6603c21bee57a3d7d57c1117c68af880cc67f7a7a2b172217fe98279044` |

The sole target is
`Script/local/menu/panel/shared/achievement.lua`. The recipe locks its original
bytes, offset and final hash. The helper pins the complete input/output archive,
rejects extra/duplicate targets, and verifies every unchanged archive member.
Its output is 1,323,584 bytes. It performs no filesystem writes itself and must
only be used for independently prepared game copies. Existing payload and
content fingerprints must be updated by the installer/patcher integration.

## Validation and limits

Seven tests passed, including the optional real-archive/Lua 5.1 checks:

- Changed inputs, unexpected targets, duplicate targets, wrong delta bytes and
  changed expected hashes fail closed.
- Only the allowed factory member changes; all resulting Lua scripts compile.
- The unmodified factory is a negative control: creating a valid achievement
  attempts to create a modal in the invented UI fixture. The patched factory
  creates none and preserves catalogue lookup and idempotent lifecycle calls.
- The actual unmodified `game.lua` caller and its update loop consume 100
  repeated events and a 32-card burst without retaining notifications or
  starving the normal lifecycle tick. No synthetic achievement is published.

To run the read-only integration checks, set `S7_ACHIEVEMENT_ARCHIVE` to the
matching separate copy's input archive and put a Lua 5.1-capable `lupa` runtime
on `PYTHONPATH`, then run
`python -B -m unittest discover -s tests -p test_achievement_notifications.py -v`.
Without that explicit archive, only the synthetic guard checks run.

The generated archive and extracted game scripts remain under ignored `work/`.
No original installation or existing prepared copy was changed during these
tests, and no native game process was started. These checks establish the
factory and caller behavior, not an additional live three-hour playtest.
