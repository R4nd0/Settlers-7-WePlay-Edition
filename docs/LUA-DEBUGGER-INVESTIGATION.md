# S7 Lua debugger investigation, 2026-11-12

S7 retains both a stripped game-debugger interface and a substantially intact
LuaJIT debug library. The examined retail builds do **not** contain a working
F9 debugger frontend that can simply be enabled. They do retain native hooks,
stack inspection, locals/upvalues access and traceback code that could support
a new debugger adapter. Live qualification is recorded separately below.

Update: the opt-in [multi-version debugger adapter](../tools/lua-debugger/README.md)
now passed real breakpoints, locals, step-in/over/out, timeout and restoration
checks inside isolated Gold and Ubisoft History games. Qualification is limited to an original early
profile script and a small fixture in that same actual Lua state; no world or
multiplayer pause behavior was tested. The Steam native API port is independently
verified offline, but its live test exits before Lua because genuine SDK
initialization fails, including a control startup without debugger injection.
The original binary audit below remains
separate from that subsequent live result.

## What the S6 project tells us

The supplied [Settlers6LuaDebugger project](https://github.com/Eisenmonoxid/Settlers6LuaDebugger/tree/df33d2c5cfd0f509fd55064afe84c7832f9570aa)
is a launcher for a recreated debugger. Its original
[SettlersLuaDebugger implementation](https://github.com/SettlersDev/SettlersLuaDebugger/tree/eb4b26e6330afaa616379e6a21620f491c02f107)
explains the distinction: Blue Byte did not ship the debugger itself, but S5/S6
retained the DLL interface. S6 uses development mode, a debug search path and
`BBLuaDebugger.dll`. Its exported callbacks include state addition/removal,
loaded-file notification and break control. The implementation also imports
the game's Lua C API and hooks protected calls to catch errors.

S7 needs a separate investigation; neither the missing Lua-level `debug` table
nor S6's development-mode switch establishes whether that interface survived.

## Confirmed S7 findings

The decoded original Interface scripts contain all of these links:

- `local/menu/panel/devUtil.lua` advertises **Lua Debugger [F9]**.
- `local/ui/profile.lua` connects `OnActionDebugBreak` to `Game.ShowDebugger`,
  inside an `IsDevM()` condition.
- `local/core.lua` exposes `system.showDebugger` and `system.stop`, the latter
  calling `Game.DebugBreak`.
- `localState.lua` supplies a fallback `debug.traceback` returning `*RETAIL*`.
  That fallback says nothing about native LuaJIT code availability.

The EXEs contain RTTI for `EGame::CLuaDebugger` and `CScr::ILuaDebugger`.
Their concrete debugger vtables have a destructor and three empty callbacks.
The native bindings for `ShowDebugger`, `DebugBreak` and
`DebugShowExecuteLineDialog` also survive, but their handlers do no debugger
work. Gold returns zero immediately. Both History handlers check a context's
debugger pointer, optionally fetch the context again, then return zero; they
never invoke a debugger method. These bodies remain ineffective even with a
non-null debugger pointer. A development-mode flag alone cannot restore code
that was compiled out.

| Preferred VA | Gold 1.12.1396 | History Ubisoft | History Steam analysis |
| --- | --- | --- | --- |
| Concrete debugger vtable | `0xF32C54` | `0x113414C` | `0x1132654` |
| Empty debugger callbacks | `0x5A75B0`, `0x5A75C0`, `0x5A75D0` | `0x53B6A0`, `0x53B6B0`, `0x4C5990` | `0x53AC90`, `0x53ACA0`, `0x4C5AC0` |
| ShowDebugger handler | `0x5B0420` | `0x544C40` | `0x544230` |
| Native luaopen_debug | `0xBF6220` | `0x46C3F0` | `0x46C400` |
| Native lua_sethook | `0xBCD6D0` | `0x445EA0` | `0x445EB0` |
| Lua debug.sethook handler | `0xBEEB80` | `0x464F00` | `0x464F10` |
| Lua debug.traceback handler | `0xBEEDB0` | `0x465130` | `0x465140` |

All three contain **LuaJIT 2.0.0-beta4**, with the full fourteen-function debug
registration list: `getregistry`, `getmetatable`, `setmetatable`, `getfenv`,
`setfenv`, `getinfo`, `getlocal`, `setlocal`, `getupvalue`, `setupvalue`,
`sethook`, `gethook`, `debug` and `traceback`. These are backed by native function
pointers and a real initializer, rather than being isolated strings. The names
use length-prefixed LuaJIT registration metadata, so searching only for
NUL-terminated API names misses them.

The exact matching upstream release is
[LuaJIT beta4, commit 23189fa4](https://github.com/LuaJIT/LuaJIT/tree/23189fa40d8cda4fa039a44b1211771b3d34fc7b).
Its [debug library](https://github.com/LuaJIT/LuaJIT/blob/23189fa40d8cda4fa039a44b1211771b3d34fc7b/src/lib_debug.c),
[registration format](https://github.com/LuaJIT/LuaJIT/blob/23189fa40d8cda4fa039a44b1211771b3d34fc7b/src/lj_lib.h)
and [hook implementation](https://github.com/LuaJIT/LuaJIT/blob/23189fa40d8cda4fa039a44b1211771b3d34fc7b/src/lj_dispatch.c)
match the decoded table and the observed hook pointer/mask/counter updates.
The native `lua_debug>` routine is LuaJIT's stdin/stderr console, not evidence
of a hidden graphical debugger. Invoking it in a GUI game could block on input.

The inspected Ubisoft state-initialization helper at `0x5B9710` opens five
selected library initializers and does not call `luaopen_debug`. This explains
how debug code can be linked into the EXE while remaining unavailable to ordinary
scripts. It does not establish every state's runtime contents.

No named Lua exports or imported Lua DLL were found in the three analysis
images. No S6 loader/callback markers were found in those EXEs, nor in a separate
ASCII/UTF-16 scan of all sixteen EXE/DLL/vPlugin files in the original Ubisoft
`Base/Bin` directory. This is bounded negative evidence, not proof that every
possible custom debugger protocol or unexamined build is absent. Dropping S6's
DLL beside S7 has no demonstrated activation path and would also require a new
Lua API bridge for the embedded runtime.

## Verification and reproduction

The [read-only audit](../tools/research/audit_lua_debugger.py) checks complete
image fingerprints, actual binding targets, vtable callbacks, the fourteen
length-prefixed names and their native pointers, and the initializer. It follows
`debug.sethook` to the native hook API, then executes that original API and its
original dispatch update in bounded x86 Unicorn emulation. No imported function
is forwarded or substituted. Writes are restricted to modeled global-state
storage and the emulated call stack; the callback itself is not executed.

Six cases per image passed: line hooks, count hooks, call/return masks,
unsupported mask-bit filtering, null-hook normalization and disabling hooks.
Hook pointer, preserved high mask bits, both counters, return value, stack
cleanup and nonvolatile registers were checked. All **18 original native calls
passed**. The [sanitized receipt](receipts/lua-debugger-audit-20261112.json)
contains only image identities, addresses, check results and scope limits.

Requires Python with `pefile`, `capstone` and `unicorn` available:

```powershell
rtk proxy python tools/research/audit_lua_debugger.py --ubisoft <owned-original-history-ubisoft-exe> --steam-analysis <existing-unpacked-steam-analysis-exe> --gold <owned-original-gold-exe> work/lua-debugger-audit.json
```

The output must be a fresh file beneath repository `work/`. The Steam input is
the existing private unpacked analysis image with SHA256 `b45794ae...0812b4`,
not the original packed Steam EXE (`8349ae65...ab410`). Original Gold and Ubisoft
images are accepted directly. The script refuses patched/unknown images.
No executable, archive, save, profile or registry setting was changed; no game
was launched. This verifies surviving hook machinery against synthetic state,
not Lua bytecode execution, a real line breakpoint, stepping or multiplayer.

## Practical implication

The promising route is an opt-in native S7 debugger adapter using the embedded
LuaJIT, rather than an S6 registry/DLL recipe. A first experiment should expose
the original debug library at a verified owner-thread state-creation boundary,
before global protection and the retail traceback fallback. It should start
with a bounded traceback/locals probe and a synthetic line-hook test in an
isolated menu state. Hook lifetime must follow actual state creation/destruction;
existing hooks must be preserved, and the owner thread and ABI must be guarded.

Only after that succeeds should a frontend implement breakpoints, stepping,
file/source tracking and error capture. Pausing the simulation would need
separate work because it can stall the network pump and desynchronize a match.
Neither debugger activation nor such an adapter was implemented in this audit.
