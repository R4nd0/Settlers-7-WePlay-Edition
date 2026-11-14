# Opt-in Gold Lua UI marker capture

The source debugger observes the original Gold 1.12.1396 `UI.WriteToLog` and
`UI.WriteToConsole` callbacks. Capture is disabled by default and separate from
breakpoint/error-trace modes. It installs no Lua line hooks, executes no Lua
from an interceptor, and accepts no execution commands. It does not change the
original callback arguments, result or Lua stack.

| Binding | Preferred VA | Original body | ABI |
| --- | --- | ---: | --- |
| `UI.WriteToLog` | `0x984210` | 65 bytes | `int __cdecl(lua_State*)` |
| `UI.WriteToConsole` | `0x9842A0` | 65 bytes | `int __cdecl(lua_State*)` |

The original callbacks return zero. The reviewed original EXE fingerprint is
`ec27c7fab2c83a1444f043eb4730f0065393ca2e91607f71e5fd78ee5a5c7c94`;
live attachment accepts the prepared work-copy fingerprint
`76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab`.
ASLR uses `module.base + (preferredVA - 0x400000)`.

The controller verifies owned PID, parent, path, full EXE hash and PE identity.
Additional guards cover both complete callback bodies, complete 16-byte binding
rows (`0x10ABDD8`, `0x10ABDE8`), both names and the original check-string helper.
Runtime comparisons apply the EXE relocations. Unknown or already modified
selected targets are rejected before hooks are installed.

Capture checks the registered Lua state's owner thread, reciprocal main-state
identity, bounded readable stack, existing string tag, length and terminator.
It never coerces values. Unrelated text is rejected before copying its payload.
Only 1..16 explicitly supplied, distinct ASCII markers with prefix
`WEPLAY_GOLD_CAPTURE_V1 ` are eligible. Each is at most 128 bytes; line endings
are not implicit. Output contains marker-list indices and fixed channel names,
not message strings. At most 128 records are retained. Original callback result,
Lua stack/value and nonvolatile-register checks accompany each record; Windows
LastError is preserved around the observation callbacks.

For a closed, separately prepared Gold copy under repository `work/`, with the
existing debugger dependencies available:

```powershell
rtk proxy python tools/lua-debugger/launch.py --bundle work/portable-gold --output work/gold-marker-run --gold-marker 'WEPLAY_GOLD_CAPTURE_V1 startup'
```

Emit that exact string through the original game script's UI environment, for
example `e.UI.WriteToConsole(...)` in the original core. An independently loaded
chunk's `_G.UI` is not assumed equivalent. Capture does not enable the legacy
patcher's general logging/debug patches.

Use `--gold-channel log` or `--gold-channel console` to select one callback.
The unselected function is neither hooked nor included as a hook target in the
additional guards. This allows log-only observation beside the crossplay lab's
existing Console64 wrapper, provided the other required API guards still match.
That combined deployment remains separate from the standalone log-only live
check; never install a second hook over an unknown one.

## Real marker self-test

```powershell
rtk proxy python tools/lua-debugger/launch.py --bundle work/portable-gold --output work/gold-marker-selftest --self-test-gold-markers
```

This explicit test temporarily appends a fixed diagnostic fragment to the
fingerprinted original `Script/local/core.lua` in the prepared RC3.27 or RC3.28 Gold
interface archive. All other archive payloads are checked unchanged. The normal
game loader executes it using the original core's UI environment. It calls both
APIs, exercises unapproved/oversized/nonstring inputs, then emits a completion
marker. The test checks three exact marker records and unregisters its hooks.
Close the game normally to finish restoration.

The archive is backed up before replacement, restored after game closure and
compared by hash. Unexpected newer bytes are retained for inspection instead of
being overwritten. Shared and private profiles are backed up; saves are never
replaced with older diagnostic data. Original installations, production DLLs,
the 4-GiB test preflight and per-copy platform isolation are retained. Other
archive versions fail closed instead of being automatically rewritten.

Read-only native guard verification is available with `pefile` installed:

```powershell
rtk proxy python tools/lua-debugger/verify_gold_capture.py --image work/owned-gold/Settlers7R.exe --output work/gold-capture-guards.json
```

The [sanitized verification receipt](receipts/gold-lua-marker-capture-20261114.json)
separates real game observations from policy/layout models and mutation checks.
Earlier direct protected-call probes did not capture markers and are not counted
as passes. Final qualification covers startup/UI capture; it does not establish
multiplayer pause safety, performance or world-state diagnostic coverage. The
main RC3.27 package remains unchanged.
