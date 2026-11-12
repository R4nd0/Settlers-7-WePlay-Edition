# Gold local queue observation

The opt-in `crossplay_gold_local_observer.h` observes original Gold events sent
with LOCALONLY mode. The previous outgoing probes observe targeted/broadcast
calls and miss this path, including the original fresh-start event. Ordinary
adapters do not include the new header. Observation does not deliver a start to
History, defer a start, or establish working mixed gameplay.

The observer guards all 225 bytes of the original sender and the ten-byte patch
site before installation. It installs during the owned one-human host lobby,
before other outgoing probes change sender bytes. It checks the owner thread,
main object, transport, session identity, readable event and reentrancy before
calling a borrowed read-only callback. The callback cannot retain the event.
Only mode zero invokes it; native forwarding remains unconditional. The bridge
preserves registers, flags and incoming LastError before executing the exact
original lock call and returning to the original clone/queue continuation.
Caught C++ exceptions fail the observer without suppressing the native event.
Installation failures attempt rollback and never report a successful install.
Access violations are not converted into recoverable callback exceptions.

The hook covers the common local branch rather than crossing the separate
mode-zero entry: broadcast enters that same branch at its second instruction.
Both original queue choices and native event ownership remain with Gold.
This observer is tied to its first session and fails closed after that session
changes. It is a laboratory component, not a session-reuse implementation.

## Verification on 2026-11-12

The public x86 fixture built with MSVC `/W4 /WX`. Its offline verifier executed
11 compiled ABI scenarios and five compiled sender guard cases. The original
lock/queue continuation and Windows imports are modeled. Checks include native
stack cleanup, nonvolatile and volatile registers, flags at lock entry, original
return values, LastError, once-only forwarding, reentrancy and ownership/session
exclusions. This emulator does not execute a C++ exception. An earlier private
Windows fixture passed ten scenarios including a caught C++ exception; the
expanded public fixture is qualified here only through offline execution.

Private runtime 1 contains the same observer implementation with a different
namespace. Its source equivalence was checked before recording the live result.
In actual DXVK High Gold/History lobby runs, it installed and observed original
mode-zero events. No Frida injection was used. Case 58 used Steam History data
with the isolated Ubisoft History executable and no Steam SDK; it does not
qualify the original Steam executable or a second History data run of this hook.

In case 58, the original guest Ready click reached both native caches. Player
identities, Monhaim map and slot states matched in two read-only inspections.
The original 2152-byte rules and 41-byte player signatures matched. Gold remained
connected behind the History window beyond the previous failure interval; real
service requests continued through approximately 290 seconds. Both clients then
closed normally and the runners restored their settings, adapters and interface;
saves, cloud files and shared files remained unchanged. A broker rejection and
poll failure followed normal peer closure. They are not a successful reconnect.
Physical RAM fell below the initial 4 GiB launch threshold during the run, with
no reported memory-pressure recovery requirement; no memory-stability claim is
made.

Case 57 is retained as a failure: after Gold was minimized during guest operation,
the original game returned to the main menu with error 28. It had already reached
matching setup signatures. The comparison supports avoiding Gold minimization
in this laboratory workflow; it does not prove or fix the native timeout cause.
Neither run clicked Start or captured a real fresh-start event. Neutral slot
closure, complete content agreement, common world loading, commands/turns, Gold
NEO hooks and actual mixed save/reload remain unimplemented or unqualified.

## Reproduce the offline checks

Build `native/build_crossplay.ps1` into a fresh directory under `work/`. Its
`crossplay_gold_local_observer_tests.exe` is a fixed analysis fixture; do not
launch that fixed image on Windows. With `pefile` and `unicorn` available:

```powershell
python tools/gold/verify_gold_local_observer.py `
  --gold <owned-original-Gold-executable> `
  --thunk-executable work/<build>/crossplay_gold_local_observer_tests.exe `
  work/<build>/local-observer-result.json
```

The verifier accepts only the existing fingerprinted original Gold analysis
image and a bounded fixed x86 fixture. It writes metadata to a fresh `work/`
output and saves no game bytes, credentials or packet payloads. See the
[sanitized receipt](verification/gold-local-event-observer-20261112.json).
