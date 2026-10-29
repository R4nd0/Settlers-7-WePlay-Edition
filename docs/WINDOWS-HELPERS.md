# RC3.5 Windows helpers

RC3.5 replaces PowerShell execution in normal desktop installation, LAN/Internet
configuration, discovery, update handoff and cleanup. It does not change
PowerShell execution policy, firewall policy, SmartScreen, App Control or UAC
settings. A declined permission prompt or a centrally enforced block is reported
as a failure. Do not ask players to disable a Windows security feature.

## Programs and rights

`WePlay.WindowsHelper.exe` uses the already bundled .NET 10 runtime, with app-local
runtime resolution. It reads interfaces through Windows/.NET network APIs and
process paths through Windows process APIs; process command lines are not read
or returned. Its fixed commands have no shell, arbitrary program or arbitrary
firewall-rule input. The launcher/backend passes structured arguments.
Helper launches remove external CLR startup-hook/profiler environment options
and use app-local runtime resolution; this affects only child-process state.

LAN configuration requests Windows UAC for the same bounded helper only. It
creates six inbound program rules restricted to Private profiles and LocalSubnet.
Internet configuration runs under the existing short-lived elevated network
worker. Six game/runtime rules are limited to the verified room interface,
local address and /24 subnet; two outer EasyTier rules permit its fixed TCP
listener and UDP transport. Existing unrelated firewall rules are not modified.
Locally disabled rule merging is respected rather than silently weakening policy.

After the owned EasyTier process exits, adapter cleanup checks the exact random
WePlay name, expected address when still assigned, matching interface GUID and
Wintun device service. SetupAPI removes only that device, including a disconnected
device left after process termination. It does not uninstall Wintun or remove
physical/other adapters. Private room configuration is reclaimed even if adapter
cleanup reports a failure. No driver installation policy is changed.

`WePlay.UpdateHelper.exe` is a native AOT executable with an embedded public update
verification key. It needs no installed/shared Python or .NET runtime and is copied
outside replaced app files. It validates the signed manifest, bounded metadata,
highest-seen payload fingerprint and installer hash/size again, retains exact old
process handles and the per-app mutex, acknowledges readiness, then waits for
launcher/backend exit. A final non-writable installer file handle spans its hash
check and execution. Only fixed Inno Setup arguments are used. Success restarts
the launcher; failure keeps a fixed local status. Recovery limits remain in
[UPDATES.md](UPDATES.md).

New packages require all helper binaries and contain no `.ps1` files. Legacy
source scripts remain for old preview/developer workflows, and the update client
retains a compatibility branch for RC3.4 packages. A validated RC3.5 package always
selects the compiled helper and fails closed if it is absent or modified.
Maintainer signing/build scripts are not installed on players' PCs.

## Building

Build with the Windows .NET 10 SDK and Visual Studio C++ build tools for Native AOT:

```powershell
python -B tools/windows/build_helpers.py --dotnet work/dotnet/dotnet.exe --output work/windows-helpers
```

The build restores official .NET packages from NuGet. Outputs stay under `work/`.
Pass that directory as `--windows-helpers` to the desktop/package builder.
Only the four framework-dependent helper files and the native update EXE ship;
debug symbols, SDK, test runners and fixture keys are excluded.

## Verification and boundaries

Tests exercise real interface/process APIs, fixed input/rule validation, package
completeness and native update fixtures under inherited
`PSExecutionPolicyPreference=Restricted`. They compare machine/user execution-policy
registry values before and after; no persistent policy is set. Disposable test keys
and harmless fixture processes isolate signature/installer failure, process identity,
mutex, readiness, restart and tampering checks from the production signing key.

An opt-in compiled live fixture also creates and removes six real LAN rules and
eight Internet rules against its own Wintun adapter. It binds its transport only
to loopback, uses no public nodes and starts no game. Device/rule removal and
unchanged policy values are verified. The test runner is not included in Setup.

These are local Windows checks, not qualification across all Windows versions,
enterprise policies or antivirus products. RC3.5 binaries currently have no trusted
Authenticode publisher signature. SmartScreen or application-control policy can
still warn/block them; removing PowerShell does not bypass those controls.
Windows x64, the existing supported OS minimum and normal network UAC requirements
remain. A separate trusted code-signing identity is a further distribution improvement.

If an RC3.4 install cannot execute its old PowerShell update handoff under its
current policy, run the RC3.5 Setup directly once. Do not change the policy to make
the old updater run. Subsequent RC3.5 update handoffs use the native executable.
