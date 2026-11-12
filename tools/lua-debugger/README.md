# Experimental S7 Lua debugger adapter

This opt-in Frida adapter uses the **real LuaJIT inside S7**. It supports source
line breakpoints, continue, step-in, step-over, step-out, bounded call stacks and
local values. Separate guarded API profiles cover Gold 1.12.1396, Ubisoft History
and Steam History. It launches prepared isolated copies, never an installed
original. Unknown executable builds are refused.
The earlier [binary investigation](../../docs/LUA-DEBUGGER-INVESTIGATION.md)
explains which debugger internals survived in each edition.

## Actual game verification, 2026-11-12

The public launcher passed in separate **History lab-B and Gold** games. History uses the
original Interface archive and the freshly built two-core `LocalPairLab`
platform adapter. Gold uses the minimal standalone native bootstrap, without
optional account or multiplayer login modules. This was native execution in a Windows game process, not the
offline emulator or a separate Lua interpreter:

- The original `Script/profiles.lua` stopped on line 1; step-in reached line 3,
  and step-over reached line 5.
- A registered breakpoint stopped `fixture.lua` before its child call on line 8.
  Step-in entered `leaf` at line 3 with `value=11`, then step-over reached line 4
  with `doubled=22`. Step-out returned to `parent`, line 9, with `second=22`.
  The next step reached line 10 with `result=25`.
- A second execution stepped directly from parent line 8 to line 9, skipping
  the entire child. The stack depths were 4 -> 5 -> 4 for into/out and 4 -> 4
  for stepping over the call.
- Three fixture executions returned 25, with the real Lua stack restored each
  time. A stale stop ID was rejected. A deliberately unanswered third breakpoint
  resumed after five seconds, restored the original hook and detached the agent.
- Eleven real stops were recorded: three in the original script and eight in
  the fixture. There were no agent-script, hook or restoration errors in the
  successful runs. History subsequently displayed its normal menu; Gold displayed
  its profile manager. Both accepted normal window closure and returned exit code 0.
- Options/profile settings were restored. Existing saves, local platform saves,
  the shared original profiles, EXE, platform DLL and Interface remained unchanged.

The [sanitized live receipt](../../docs/receipts/lua-debugger-live-20261112.json)
pins the tested sources and records these checks. The complete local traces and
profile backups stay under ignored `work/`. Earlier failed attempts are retained
there; they are excluded from the successful-run qualification.

The tested Lua state was each game's early profile-script state. Running-world
breakpoints, longer debugging sessions, other threads/states and multiplayer
pause behavior remain unqualified. A debugger pause can stall game/network
updates. Use an isolated local session. No world was started in this test.

| Version | API map and offline checks | Actual game breakpoint/stepping |
| --- | --- | --- |
| Ubisoft History, isolated A/B profiles | Verified | Passed in B |
| Gold 1.12.1396, prepared standalone image | Verified | Passed |
| Steam History, isolated C/D profiles | Verified | Blocked before Lua by genuine SDK initialization |

Steam's real `SteamAPI_Init()` returned false in the diagnostic runs, including
the documented temporary `steam_appid.txt` developer-launch setup. The process
exited with code 0 before creating a Lua state. A control startup without Frida
also exited before debugger attachment. The visible Steam client was signed in,
and controller/client integrity levels matched. No ownership result was changed.
Do not treat this exit code as a passed debugger test. Steam's native port is
implemented and checked offline, but is **not live qualified**. Its remaining
platform dependency is recorded in the separate
[standalone follow-up](../../docs/PLATFORM-INDEPENDENCE-FOLLOWUP.md).

## Requirements and preparation

Use Windows, Python with `frida` and `pefile`, Node for the policy tests, an
existing local bootstrap service, and a separate prepared game bundle beneath
repository `work/`. The bundle needs DXVK and the project's lab-compatible
platform adapter. The source-only tool ships no game files or runtime DLLs.

The exact accepted EXE hashes and native APIs are in `profiles.PROFILES`. History
uses the existing A/B variants of the Ubisoft image and C/D variants of the
Steam analysis image. Their native profiles resolve through Windows Documents
to repository `work/A` through `work/D`. The launcher verifies that resolution and refuses an
already-used diagnostic profile. Do not replace a loaded platform adapter.
The ordinary installer/release adapter is unchanged by this work.

Gold accepts the existing prepared image fingerprint and uses its native
path-derived `Documents/WePlay/Gold/<digest>` profile/mutex isolation. It requires
an explicitly selected x86 native startup DLL beneath `work/`, with a
`GoldInitialize` export. The original guarded entry bridge initializes that DLL
on the game thread; the original entry bytes are restored before qualification.

A compatible platform adapter can be built separately with:

```powershell
rtk proxy pwsh -NoProfile -File native/build.ps1 -OutputDirectory work/debugger-native -LocalPairLab -MaximumPhysicalCores 2
```

Only deploy it into the closed, separate lab copy. The launcher itself does not
copy or patch binaries/archives on disk. Gold's entry bridge is temporary process
memory instrumentation. Before launch it backs up the actual settings,
save files and shared profiles into the fresh observation directory. After
normal game exit it restores settings and the previous local runtime config,
compares save/profile and game-file identities, and never restores an older save
over a newer one. At least 4 GiB free host RAM and commit headroom is required;
the existing memory sentinel monitors the owned client.

## Run the real-game self-test

With those dependencies on Python's module path:

```powershell
rtk proxy python tools/lua-debugger/launch.py --bundle work/debugger-game --dotnet work/dotnet/dotnet.exe --service work/service/S7Patcher.dll --output work/debugger-run-1 --self-test --pause-ms 5000
```

Supply the paths to the existing prepared bundle and bootstrap service. The
output must be a new directory under `work/`. `--affinity-mask` optionally
applies an explicit, OS-validated mask before entry; the verified local test used
`0x5` for two previously identified physical cores. That mask is machine-specific.

Gold uses its separate startup backend, without the History bootstrap service:

```powershell
rtk proxy pwsh -NoProfile -File native/build_gold.ps1 -OutputDirectory work/debugger-gold-native
rtk proxy python tools/lua-debugger/launch.py --bundle work/debugger-gold-game --gold-adapter work/debugger-gold-native/weplay_gold_startup.dll --output work/debugger-gold-run-1 --self-test --pause-ms 5000
```

Its bundle must already contain the prepared `Data/Base/_Dbg/Bin/Release` game.
For Steam, retain the genuine SDK and ownership check if diagnosing its startup;
the launcher sets app ID 965320 and defers injection until the initial input-idle
boundary. Existing Lua states are registered at their next owner-thread protected
call. The offline-verified Steam backend does not replace the unresolved
standalone/platform-detachment work and has not passed a live Lua stop here.

`--self-test` adds the small, local-only fixture to an existing game Lua state at
an owner-thread protected-call return. It runs after the original-script stops;
no archive is edited. The tool prints `live-self-test-passed` only after all
expected source/line/depth/value, stack, stale-command, timeout and hook cleanup
checks pass. Close the game normally afterward. The final restoration receipt
and actual game exit are separate from the debugger test result.

## Use breakpoints and stepping

Ordinary mode does not inject the fixture. Provide initial breakpoints and a
local JSON-lines command file, for example:

```powershell
rtk proxy python tools/lua-debugger/launch.py --bundle work/debugger-game --dotnet work/dotnet/dotnet.exe --service work/service/S7Patcher.dll --output work/debugger-run-2 --commands work/debugger-commands.jsonl --breakpoint Script/localState.lua:1
```

The private `debugger.private.jsonl` trace and console output include each stop's
ID, source, line, stack depth, frames and locals. Source matching is exact after
removing an initial `@` and normalizing slashes. Line numbers belong to the
original source passed to the game's compiler, which may be obfuscated. They do
not refer to the separately decoded analysis copies.

Append one JSON command per complete newline to the command file:

```json
{"op":"stepIn","stop":1}
{"op":"next","stop":2}
{"op":"stepOut","stop":3}
{"op":"continue","stop":4}
```

Use the current observed stop ID, not these example IDs. `next` is step-over.
Breakpoints can be replaced while running:

```json
{"op":"breakpoints","items":[{"source":"Script/localState.lua","line":1}]}
```

`{"op":"detach"}` requests owner-thread hook restoration. Keep the controller
alive until `detached` or normal game exit; dormant/foreign-owner states can
delay cleanup. Pauses are bounded to 1..30 seconds (default 10). Expiry disables
the debugger and restores its hooks rather than repeatedly pausing a game whose
controller disappeared. A stale or duplicate resume command cannot release a
different stop. Up to 32 breakpoints, eight states, 64 depth-probe frames,
16 reported frames and 24 locals in each of the top two frames are bounded.
Strings and opaque objects are redacted; numeric, boolean and nil locals are
reported. There is no arbitrary expression-evaluation command.

## Implementation boundaries

The host verifies the exact prepared EXE hash, work-copy location and live PID,
parent and path before attachment to its owned child. It pins the original
primary thread before Frida introduces other threads. Steam's startup control
defers attachment until the SDK has initialized. The agent independently
checks the named EXE module, path, PE identity and ASLR-adjusted original API
bytes before installing any hook. It refuses pre-existing debug hooks.

Lua API calls execute only on the original state owner thread. The host command
handler changes policy and signals an anonymous Windows event; it never reads
or changes a Lua stack from the debugger transport thread. Calls and local
inspection preserve the original stack boundary. State destruction restores
the original hook before the original close operation. The agent is pinned
while native hook pointers refer to it, and only unpins after restoration.

Frida's cooperative native-call scheduling allows commands to arrive during
`WaitForSingleObject`; see the [Frida API reference](https://frida.re/docs/javascript-api/#nativefunction).
The tested environment exposed two integration details: `Process.mainModule`
selected an injected OBS module, so the EXE is selected explicitly by name;
Interceptor return objects are recycled, so retained state pointers are copied.
Debug text is read with a bounded, explicit NUL search.

Run the deterministic stepping-policy checks with:

```powershell
rtk proxy node tools/lua-debugger/test_stepping.js
```

These sixteen policy checks cover source matching, recursion depth, breakpoint
priority, stepping and invalid commands. They supplement the real game test;
they do not establish native ABI or gameplay correctness by themselves.

`verify_profiles.py` additionally checks all ten APIs independently in each of
the three fingerprinted original/analysis images using unique normalized native
instruction signatures. It reuses the original library/vtable/binding audit and
eighteen bounded native hook/dispatch emulation calls. Requires `capstone` and
`unicorn` in addition to `pefile`:

```powershell
rtk proxy python tools/lua-debugger/verify_profiles.py --ubisoft <owned-original-ubisoft-exe> --steam-analysis <existing-unpacked-steam-analysis-image> --gold <owned-original-gold-exe> work/debugger-api-check.json
```
