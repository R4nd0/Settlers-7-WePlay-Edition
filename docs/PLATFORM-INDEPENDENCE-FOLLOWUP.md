# Separate follow-up: platform-independent copies

The owner's target, clarified on 2026-11-12, is a standalone copy regardless of
the source of the game data: no requirement to run Ubisoft Connect or Steam,
and no communication from that copy to either platform. Privacy and reduced
overhead are the motivations. Platform detachment is a separate workstream
from the Lua debugger; debugger functionality does not establish isolation.

## Current distinction

| Path | Established behavior | Remaining boundary |
| --- | --- | --- |
| Gold with native standalone bootstrap | The isolated debugger run uses the minimal native startup and its loopback guards, without optional account/multiplayer login modules. | The native import guard is bounded; it is not an exhaustive network capture or general proof about every module. |
| Ubisoft History standalone runtime | The local adapter supports an explicitly unavailable legacy platform API and local bootstrap, retaining image/callsite/ABI guards. | Existing gameplay, save/reload and network qualification remains limited to its recorded cases. |
| Steam-sourced History data with the isolated Ubisoft History runtime | This source-data/runtime combination has been tested without the Steam SDK in the crossplay laboratory. | It does not qualify the original Steam EXE as platform independent. |
| Native Steam laboratory C/D runtime with local facade | D passed actual debugger stepping, started a local Monhaim simulation, added a separate save, exited and reloaded it in a new process. A further start/reload passed with Steam/SteamService/Ubisoft clients absent. All games exited normally with 0. Only the local API boundary and local platform adapter were observed as platform modules, with no child process. | Exhaustive communication coverage and the unchanged production Steam EXE remain unqualified. |
| Original Steam History runtime | Its debugger has a separately mapped native API profile; a diagnostic test retaining the genuine SDK/ownership path is a debugger test only. | Complete independence from the Steam client/SDK and absence of platform communication remains open. |

Do not describe Steam data plus a different executable as proof that the native
Steam executable has been detached. Do not describe a normal SDK connection as
standalone qualification. No performance improvement from platform removal is
claimed without measurement.

The debugger investigation found a concrete native Steam startup blocker:
`SteamAPI_Init()` returned false despite a visibly signed-in client and matching
controller/client integrity levels. A documented temporary App-ID file did not
resolve it. A separate control startup without debugger injection also ended
with exit code 0 before attachment. No SDK result or ownership check was
overwritten. This is evidence of a remaining platform dependency, not evidence
that its exact failure cause has been established.

## Implemented laboratory candidate, 2026-11-12

The separate source-only [Steam local-facade tool](../tools/platform-standalone/README.md)
builds a private snapshot of the existing native adapter and an explicitly
unavailable Steam API boundary. Three complete function guards and the existing
executable/ABI guards protect a local startup branch, removal of SDK overlay
dispatch and a local language source. The genuine SDK is backed up, replaced
only in a closed isolated lab copy, and restored after exit. The boundary never
reports an authenticated account or successful SDK initialization.

Its native checks and actual Steam D menu/world/save run passed. Two existing
save files retained their original contents; the separately named diagnostic
save was added. A first full-exit reload was stopped before deployment by the
unchanged 4-GiB RAM guard (3,884 MiB free). After recovery, a fresh process loaded
the save into the visible world and closed normally. A subsequent fresh start
and world reload also passed with all Steam/SteamService/Ubisoft client process
counts zero before startup, at the menu and in the world, verified using normal
host rights. Restricted process snapshots that hid background clients were not
used as absence evidence. Native network guards remain loopback-only and
bounded; module checks
and kernel-only shim imports are not an exhaustive egress capture. The Steam
client was closed through its regular shutdown command, without force-killing
any process. Both original DLLs and settings were restored after every real run.

## Concrete scope for the separate implementation

Trace all remaining platform dependencies in the isolated original Steam
runtime, including startup, periodic updates, ownership/session gates, profile
and save handling, dynamic imports and child-process/platform IPC. Preserve the
installed originals, local saves and all executable, object, callsite and ABI
guards. Do not read account caches or credentials. Identify the native meaning
of unavailable platform services before implementing replacement behavior.

Any candidate must have a reproducible source checkpoint and fresh isolated
test copy. Acceptance should demonstrate cold start with neither platform
client required, usable menus, local game start, save, complete exit and reload,
plus checks covering the copy's relevant modules and child processes for
unwanted platform communication. Merely hiding a login dialog, reaching an
empty list, or suppressing one HTTP call is insufficient. Keep experiments
loopback-only until an explicitly scoped LAN/Internet transport test is intended.

Measure startup time and steady CPU/RAM/frame behavior before and after with
the same game data, graphics configuration and scene. Keep functional and
performance results separate. The laboratory candidate implements the first
bounded native Steam detachment path; complete platform-independent production
support remains open under the acceptance conditions above.
