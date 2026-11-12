# Experimental native Steam local facade

This separate, source-only candidate removes the Steam SDK dependency from the
fingerprinted **Steam History laboratory C/D runtime**. It retains the existing
local platform/bootstrap and loopback guards. It does not prepare an original
installation, update release packages or qualify the unchanged production EXE.
Gold and Ubisoft standalone support use their existing native backends.

The genuine-SDK debugger diagnostic stopped at `SteamAPI_Init=false`. This
candidate provides an explicitly unavailable local API boundary: initialization
still returns false, handles are zero and interfaces are absent. Unexpected
SDK-object paths terminate the owned game. No account, ownership response,
ticket, client loading, platform IPC or network implementation is supplied.
The compiled boundary imports only Kernel32.

The game-side facade has three separately guarded changes:

- After the existing local bootstrap succeeds, an owner-thread bridge accepts
  the exact native facade with its offline flag and absent Steam callback object.
  It skips SDK initialization/callback construction and uses the original native
  startup epilogue. It does not overwrite an SDK result.
- Overlay requests retain their original local bookkeeping/cleanup, while the
  additional Steam SDK dispatch is skipped.
- The original language conversion/assignment receives an explicit local
  language instead of querying Steam. The default is `english`; the test used
  `german`. This does not supply Steam account language or modify game archives.

Full function guards cover 368, 483 and 275 bytes, including ASLR relocation
normalization. Existing full EXE, application, callsite, object and ABI guards
remain. A changed function is rejected before facade writes. Partial-write
failure restores the affected sites; fatal rollback failure terminates the owned
test. The bridges, guard normalization/corruption, object/thread rejection and
unavailable API contracts have compiled x86 Windows tests.

## Build and run

Requires the repository's native x86 Visual Studio toolchain and the existing
debugger dependencies (`frida`, `pefile`) on Python's module path. Use a fresh
build directory under ignored `work/`:

```powershell
rtk proxy powershell -NoProfile -File tools/platform-standalone/build.ps1 -OutputDirectory work/steam-local-native
```

The builder snapshots native sources, adds the facade only to that private
snapshot, builds the isolated two-core adapter, and runs twelve new native
scenarios. It does not edit `native/` or deploy game files. Binary hashes and
source checkpoints bind the candidate to its native test receipt. No DLL is
published in this source-only tool.

With an existing, closed Steam lab C/D bundle and local bootstrap service:

```powershell
rtk proxy python tools/platform-standalone/launch.py --bundle work/steam-port/Pair-D --native work/steam-local-native/native --output work/steam-local-run-1 --language german --dotnet work/dotnet/dotnet.exe --service work/service/S7Patcher.dll --self-test --pause-ms 5000
```

The wrapper requires the original SDK before deployment. It checks the exact
EXE, candidate binary/source receipts and all three function guards; backs up
both DLLs; deploys only while that copy is closed; delegates profile/settings
backup, owned-process checks and memory monitoring to the existing debugger
launcher; and restores both original DLLs after normal exit. Saves are retained,
never replaced with older diagnostic backups. Outputs and raw observations stay
under `work/`. A failed 4-GiB memory preflight starts no game and deploys no DLL.
Use the debugger's command-file options instead of `--self-test` for ordinary
breakpoints. The self-test detaches before any subsequent manual gameplay.

## Observed scope, 2026-11-12

Steam D passed eleven real Lua stops: in/over/out, numeric locals, three native
VM results of 25, stale-stop rejection, balanced stacks, five-second timeout and
restored hooks. Its first script stops were in the installed local UI's
`Script/local/core.lua`, rather than unmodified retail Lua. After detachment,
the original game menus started a Monhaim skirmish. The running world was
visible; a separately named save appeared in the native load list and produced
one new save file. Both older saves retained their contents. The game exited
normally with code 0 and both original DLLs/settings/shared profiles restored.

The first candidate run exposed an optional debugger observer that could not
intercept the tiny unavailable API function. It is retained as a failed debugger
initialization case, despite reaching the menu. That client also closed normally
with 0; no forced termination was performed. The debugger now recognizes the
local boundary, keeps optional diagnostics nonfatal, and pins its script only
while raw Lua hooks need ownership. Eight controller handshake regressions pass.

A first full-exit reload attempt was blocked before deployment by 3,884 MiB free
RAM, below the unchanged 4-GiB experiment threshold. Once memory recovered, a
fresh process found the new save and loaded it into the visible, running world.
It closed normally with 0; saves, settings, shared profiles and both original
DLLs passed their restoration checks.

Steam was then stopped with its regular client shutdown command. A further
fresh startup passed another eleven actual Lua stops, this time in
`Script/localState.lua` at 1 -> 3 -> 5 and the same native VM fixture with depths
14 -> 15 -> 14. It again loaded the save into the running world and closed
normally with 0. Process snapshots **with normal host rights** verified no
Steam client, helpers, SteamService or Ubisoft client before startup, at the
menu and in the loaded world. Earlier restricted snapshots had hidden live
background clients and were discarded as absence evidence. The tool did not
force-terminate Steam or any game.

During the successful world run, the existing sentinel reported pressure level
1, no required recovery and no monitor failure; this is not a memory-stability
or performance qualification.
The game's observed platform modules were the two local DLLs; no client module
or child process was observed. This is not an exhaustive IPC/egress capture or a
general qualification of later-loaded modules. Longer play, multiplayer and production integration
remain open. See the [separate acceptance criteria](../../docs/PLATFORM-INDEPENDENCE-FOLLOWUP.md)
and [sanitized receipt](../../docs/receipts/steam-local-facade-20261112.json).
