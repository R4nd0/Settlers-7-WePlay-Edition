# Original Gold fresh-start capture

The opt-in `crossplay_gold_start_hold.h` holds the original Gold fresh-start
event at its original send call. It invokes a borrowed-event callback and
returns through the original caller cleanup without enqueuing that event.
`Release` restores only its exact owned hook; it never replays or delivers the
captured event. There is no History delivery or world-start permission in this
primitive. Ordinary adapters do not yet include it.

This supplies the missing capture point for a later coordinated start. The
[local-event observer](GOLD-LOCAL-EVENT-OBSERVER.md) always forwards events and
cannot by itself prevent Gold from loading a world before History agrees.
The hold instead replaces only the six-byte original fresh-start callsite and
guards its entire 254-byte caller before installation. The original general
sender and other network events remain intact.

`Install` requires an owned one-human Gold host lobby. `InstallBound` requires
the separately authenticated two-human native Gold host admission and current
session/connection roots. Both retain the existing full native fresh-start
method guards. Each invocation checks the owner thread,
main/transport/session roots, expected original sender, zero local-only
arguments, exact event type, source host, async flag and empty fresh-save path.
Only one capture is attempted. Invalid, repeated or reentrant invocations stay
held and mark the state failed. A callback must validate and copy the event
before returning; the original caller destroys its temporary afterward. The
callback cannot retain that borrowed pointer. Caught C++ exceptions fail the
hold; access violations are not caught as recoverable exceptions. The hook is
tied to its first session. Owned release enables safe teardown but does not
authorize reuse with a new session or replay of an old event.

The bridge preserves incoming general registers, flags, direction flag and
x87/SSE state around the C++ callback, clears DF before entering C++, replays
the original caller store and removes the original five stack arguments once.
Release rejects foreign patches, the wrong owner and calls inside a callback.

## Updated component qualification, 2026-11-16

The compiled offline fixture passed 17 ABI scenarios, six owned-release cases
and five full-caller original-byte guard cases. The callback deliberately
destroys floating-point state; the continuation must still observe the original
state, volatile registers and flags. Tests include DF set on entry, incorrect
original sender and release attempted inside the callback. Native roots/events,
the continuation and Windows imports are modeled. C++ exception handling and a
positive installation transaction are not emulated. The relocatable Windows
fixture is explicitly excluded when its modeled low addresses are occupied;
that exclusion is not a successful ABI run.

This is component qualification, not a new paired world-start result. The
historical live capture below does not qualify the updated admitted-host entry
or common Gold/History loading.

## Actual game result, 2026-11-12

In paired case 59, the original Gold Start button produced a genuine fresh-start
event. Private runtime 1 verified the native roster and original event methods,
then used the original writer to capture its 21-byte wire representation. The
hold prevented native enqueue, and both games remained in their original lobby
views. This was a capture test, not a common world start. The public hold has the
same implementation with renamed symbols and explanatory comments; source
equivalence to the live private implementation was checked.

Before the click, both original Ready caches, identities, map and slot states
agreed; the original rules/player signatures also agreed. The click then exposed
the next required transition: Gold's original host logic converted open slots
3 and 4 into closed neutral entries with native state 2, Ready 1 and their slot
numbers. History still retained state 1 open entries. Gold also emitted neutral
Team changes that the current two-human options bridge rejected. The start was
nevertheless captured and held independently of that failed lobby channel.
The earlier setup agreement must not authorize this changed setup.

The run used DXVK High and Steam History data with the isolated Ubisoft History
executable, without a loaded Steam SDK. It does not qualify the original Steam
executable or both History data sets for this hook. Both games exited normally
with code 0; settings, adapters and interface were restored, and saves, cloud
files and shared files remained unchanged. Available physical RAM fell during
the run; no sustained memory or gameplay stability is claimed.

## Offline checks

The public fixture compiled with MSVC `/W4 /WX`. Its verifier passed 14 compiled
ABI scenarios and five compiled full-caller guard cases. Synthetic native roots
and events, the caller continuation and Windows imports are modeled. The original
sender address is deliberately unmapped in ABI execution, so accidentally
forwarding a held event fails the fixture. Checks cover stack cleanup, preserved
nonvolatile registers, replay of the overwritten caller store, LastError, one
capture, invalid source/type/async/mode/arguments, failed ownership/session,
callback rejection, duplicate calls and reentrancy. C++ exception handling and a
positive installation transaction are not emulated.

Build `native/build_crossplay.ps1` under a fresh `work/` output. Its fixed analysis
fixture must not be launched on Windows. With `pefile` and `unicorn` available:

```powershell
python tools/gold/verify_gold_start_hold.py `
  --gold <owned-original-Gold-executable> `
  --thunk-executable work/<build>/crossplay_gold_start_hold_tests.exe `
  work/<build>/start-hold-result.json
```

See the [sanitized receipt](verification/gold-fresh-start-hold-20261112.json).
Native neutral-slot/Team delivery, updated setup agreement, a full actual content
manifest, coordinated release and world loading remain necessary before turn,
Gold NEO and mixed save/reload tests can begin.
