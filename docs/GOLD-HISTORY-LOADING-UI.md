# Gold world-entry UI correction

The Gold loading overlay now closes through the original UI lifecycle in both
History pairings. In the clean DXVK tests, Gold/Steam matched 1,571 consecutive
original CRCs and Gold/Ubisoft matched 1,488. Each pair delivered one payable
storehouse and one street command per player. Both new storehouses completed on
both clients. Both games exited normally with full restoration.

The world previously advanced behind the overlay. Read-only observations showed
`game.started=true` and an original loading-close request, but none of the normal
game UI updates ran. Bounded component markers located the interruption immediately
after creation of the game menu: `gameChatMessages.create` called `globalChat.hide`.
The old pinned laboratory archive used the generic removed-page adapter, which
lacked that method. Thus `startGame` stopped before `system.setUpdateFunctions`.

The current ordinary Gold menu builder already supplies `QUIET_GLOBAL_CHAT`, with
the required inactive chat lifecycle. The pair builder now upgrades its exact
older pinned input to that same contract. This does not restore the retired online
service. A separate fingerprinted loading-script correction guards the optional
candle component and removes its duplicate destruction. The candle correction
alone did not resolve the observed world-entry failure.

No loading widget is forcibly hidden, and no readiness or native start result is
invented. Original `game.lua` is unchanged in the clean candidate. Diagnostic
phase instrumentation was removed. The watchdog reads the real loading-widget
existence once per second. Explicit foreground captures of the uniquely selected
Gold window showed the complete HUD without the overlay in both pairings; a real
click on Gold's castle opened its panel in the Ubisoft diagnostic run.

Twelve freshly staged current-builder archives (three editions, construction probe
on/off, ordinary/x20-request pacing) passed Lua 5.1 compilation and unchanged-member
checks. The original
loading script also passed 15 modeled lifecycle checks, including reproduction
of the missing-candle and duplicate-destruction failures. These modeled tests
are distinct from the real native pair evidence in the
[verification receipt](verification/gold-history-loading-ui-20261114.json).

## Requested x20 laboratory pacing

`pair_interface.py --speed 20` requests the original `SetGameTurnDuration(5)`
after actual world entry. The setter queues a duration: its getter can still
return 100 during that update. The observer therefore waits for real consumption
and reports failure after three seconds without aborting the UI or retrying the
setter. Default staging remains at ordinary speed.

The first Gold/History multiplayer attempt returned 100 immediately on both
clients. That run does not establish x20 operation. A separately guarded native
original-setter candidate is under investigation; no actual accelerated-throughput
qualification is claimed here. NEO, four-player AI maps, EasyTier transport for
this mixed-edition bridge, mixed save/reload and separate-PC operation remain
unqualified. Existing History-only EasyTier support is separate evidence.
