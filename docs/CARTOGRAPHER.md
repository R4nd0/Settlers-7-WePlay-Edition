# Kartograph and the simple main menu

The RC3.5 Kartograph fails to finish creating its editor when S7 starts with
`-noMapInMainMenu`. Reproduced on Waldreich / Standard with DXVK on PC1:
the minimap and sector borders appear, but the edit panel stays empty and the
map-selection dialog remains underneath. The same map and renderer with the
menu world loaded show the editing categories and objects correctly.

The original menu dispatcher immediately registers MapForge, although its
entity/UI initialization needs the menu world. The guarded archive delta in
`tools/offline-ui/cartographer-context.json` defers that existing action and
requests the game's existing background-map loading screen on demand. Once
`Game.InMapMode()` is true, the regular dispatcher opens the editor with the
original action data. Repeated clicks do not request another world load;
another menu action clears a pending editor action. Destroying the main-menu
state also destroys the pending action with it.

The ordinary simple main menu still starts without a world. After using the
Kartograph, the loaded menu world remains for that game process; the next
simple-menu start is unloaded again. No command-line preference, renderer,
developer mode, simulation, matchmaking or network pump is changed by this
delta. No new world-unloading API is invented.

The installer applies this stage after the existing public-LAN and achievement
notification stages. The recipe, archive and changed Lua member have pinned
input/output hashes. Every unrelated archive member is compared after rewriting.
The package capability and recipe participate in the installed game's contract;
old packages remain supported and do not silently receive this archive change.

`tests/test_cartographer_context.py` tests malformed archives/recipes with invented
data. Its opt-in Lua 5.1 integration uses a locally supplied RC3.5 archive, compiles
all Lua members, and executes the real constant decoder and action dispatcher in
invented engine objects. It includes the original failing dispatcher as a negative
control, delayed world readiness, repeated requests and cancellation. These tests
do not replace a real editor loading/editing check.

The separate real-game candidate has now passed that check with simple menu
enabled: black menu first, background-world loading only after selecting
Kartograph, complete Waldreich / Standard editor, a victory-condition edit
(19 to 18 selected, then restored), saving a new local QA template, leaving,
reopening and loading that template. The game was then closed normally.
The original installation and installed RC3.5 archive were not modified.
This is editor coverage, not a multiplayer or long-session qualification.

The candidate's independent memory sentinel recorded 80 samples and no measured
pressure (peak private memory 1330 MiB), but also recorded `monitor_failed:true`.
The bounded monitor currently records no error reason, so this run does not
establish uninterrupted memory monitoring or identify that monitor failure.
No monitor limit was relaxed for the editor test.

The separate [memory-start correction](MEMORY-START.md) removes the unrelated
4 GiB unused-physical-RAM startup quota; it does not provide the editor context.
