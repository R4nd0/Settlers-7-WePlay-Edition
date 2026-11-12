# Gold startup and quiet interface

Verified in a separate Gold laboratory copy on 2026-11-11/12. This ports History's
startup/menu cleanup to the independently fingerprinted Gold build and removes
additional vendor UI found in an archive-wide audit. The dedicated
[Gold single-player installer](GOLD-INSTALLER.md) now includes this interface
and the full campaign selection by default. Existing History release assets
have not been replaced.

Gold answers only its startup-options reset question with No. The guarded call
is `40DEF8 -> 41C400`; its original boolean consumer and `RET 4` epilogue are
checked independently of History. The replacement returns false in AL and
discards one stack argument. Executable guards, transactional installation and
rollback remain. Other message boxes retain their original handling.
Options.ini and Startup.tag are not rewritten by this patch.

`tools/gold/menu_interface.py` stages an archive from the exact original Gold
Interface.bba and includes the existing local-profile correction. It:

- Skips startup movies, camera flight and click blocker; defaults to a black
  main menu without loading the background castle map.
- Removes Quickstart/promotional links, Store, Uplay, Facebook, My City, My
  Realm, Expansions and redeem routes, including scenario navigation and
  unavailable-map dialogs.
- Reduces the player card to portrait/name: no coins, store, mail, chat, mentor,
  friends, ladder or achievement icons/polling, or developer connection footer.
- Replaces news and the green online status with empty lifecycle-compatible
  frames. Status still reports actual network transitions to its caller; it
  cannot log in, log out or invent connection state.
- Retires eleven social/shop/ladder/unlock/template-exchange pages and redirects
  their actions before dispatch. Removes vendor invitations, achievement/Uplay
  popups, Facebook authorization/credential polling, template votes and the
  Map Forge publish control.
- Removes store/code controls below map lists, both DLC padlock badge variants,
  Mentor/AboutMe profile controls, and the shop-currency presentation and
  purchase-content nag at game end.
- Removes external shop/browser/feed callback references and menu tracking
  references across the archive, including callbacks passed as values.
- Makes all installed base-campaign and Chronicles mini-campaign maps directly
  selectable. Base campaign uses native GetUnlockedCampaignMaps(true), whose
  independently inspected Gold branch enumerates the complete campaign vector;
  Chronicles marks the full native RotR enumeration active. Saved
  completion, rewards and map existence/availability are not fabricated.

Real map availability, local rewards, campaign/technology progression, native
profile serialization and actual multiplayer state remain authoritative. Normal
Play, Campaign, Skirmish, Chronicles, Multiplayer, Load, local Map Forge, Castle
Forge, Profile and Settings remain. Publisher credits/copyright remain intact.
Dormant helper definitions and compatibility field names may still contain
vendor words; they do not establish visible or actionable vendor UI.

The original archive is SHA-256 pinned. Primary script fingerprints, unique
source anchors and full archive readback guard edits. Lua constant-table headers
are retained. Forty of 284 payloads change; the other 244 retain their
original bytes. Map/template lists now resolve Gold's existing
`local.ui.listControl` module instead of a missing legacy global factory,
including the campaign difficulty dropdown.
Native Locked/isUnlocked values are unchanged.

```console
python -B tools/gold/menu_interface.py "path/to/original/Interface.bba" --output work/gold-quiet-menu
powershell -NoProfile -File native/build_gold.ps1 -OutputDirectory work/gold-quiet-native -LocalSimulation
```

These commands stage/build and verify; they do not install or launch a game.
Use a backed-up test copy and the explicit Gold initializer. The History loader
is incompatible. Never replace an archive or adapter loaded by a running game.
Castle Forge requires the background map: `-mapInMainMenu` explicitly retains
it; `-noMapInMainMenu` takes precedence. Castle Forge operation without that map
is not qualified by this cleanup.

Validation separates fixtures from actual observations:

- Native x86 `/W4 /WX`: actual replacement bytes return false with exact stack
  cleanup and an intact sentinel. Rollback, 19 byte-guard rejections, 75 network
  checks and Gold import guards pass. Loopback checks need unrestricted sockets.
- All 40 changed scripts compile with Lua 5.1; all 284 payloads verify. Twelve
  authored menu tests and nine existing profile tests pass. The audit finds
  zero references to the enumerated vendor browser/tracking callbacks, eleven
  inert legacy pages and no vendor controls in the card.
- Actual Gold uses pinned DXVK at High/1920x1080. With Startup.tag present, the
  reset question is absent and native No bytes are verified in the owned process.
  Local-profile confirmation remains. The black menu, compact card and absence
  of the green online box are observed. Skirmish lists fully populate; scrollbar
  movement, Amber Coast selection/description and Back work without lock badges,
  store or redeem. Map Forge selection/Cancel work without store controls.
  Profile and Customize show the cleanup. No world or match is started.
- All twelve base-campaign markers appear. The first and final mission
  (The Final Frontier) can be selected directly; the Easy/Normal/Hard dropdown
  and Back work. Chronicles exposes all five mini-campaign missions; its final
  Rebel Realm selection updates the description and retains Start/Back.
- Owned test games exit normally with code 0. All sixteen backed-up shared
  save/options/profile files retain hashes and timestamps. Startup-only
  verification leaves the owned Options.ini byte-identical.

Opening and cancelling the original Gold Settings dialog separately serialized
DepthOfField/System defaults in the owned diagnostic profile. Those options
were restored from backup before subsequent startup-only checks; shared
profiles were unchanged. This patch does not change that original dialog.

Scenario navigation is checked live. Game-end promotion removal, template voting
and Map Forge's post-selection publish control are checked in source/compilation
and authored service fixtures; no completed world or forge publication workflow
is claimed. Accessible menus do not qualify multiplayer.

Sanitized receipt: [Gold interface verification](verification/gold-menu-20261111.json).
Concurrent crossplay laboratories retain their own pinned builds/archives;
this test did not deploy over or stop those clients.
