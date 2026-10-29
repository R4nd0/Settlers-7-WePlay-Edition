# WePlay development

The public repository contains implementation sources, tests, build inputs and
technical documentation. Personal agent instructions, editor workspaces, local
handoff journals, copied games, credentials, signing keys and generated packages
are intentionally excluded. Keep intermediate builds under ignored `work/` and
deliverables under ignored `outputs/`. Public product screenshots belong in
`docs/screenshots/`; `Images/` is an ignored legacy/private collection.

## Components and builds

- `Source/`: .NET 10 local matchmaking service and original patcher.
- `native/`: guarded x86 local platform/network adapter. Read
  [native boundaries and ABI guards](NATIVE-LOCAL-START.md) before editing.
- `tools/installer/`: verified separate-copy game preparation and launcher backend.
- `tools/desktop/`: bilingual Windows desktop launcher and Inno Setup packaging.
- `tools/easytier/`: pinned modified helper build and corresponding-source tooling.
- `tools/discord-worker/`: external Discord interactions/game directory service.
- `tests/`: synthetic fixtures and opt-in Windows integration checks.

The packaging entry point is `tools/desktop/build_rc2.py` (historical filename),
currently building RC3.4. Use its `--help` and the
[build guide](RC2-BUILD.md) for toolchain inputs. It requires a verified base
package, local .NET SDK/runtime, graphics probe, Inno Setup compiler and the
matching EasyTier runtime/source. Game data is never included in Setup.
The public source tree deliberately does not contain those generated runtimes
or proprietary game archives. [EasyTier build instructions](../tools/easytier/README.md)
describe its reproducible inputs and corresponding-source requirement.

Do not change the original installation or overwrite a loaded adapter. Prepare
and verify a separate game copy. Retain executable hashes, exact callsite bytes,
ownership, lifetime and ABI guards. An accessible menu, working tunnel or packet
acknowledgement does not establish multiplayer synchronization.

## RC3.5 candidate and RC3.4 baseline

The RC3.5 candidate adds [bounded Windows API helpers](WINDOWS-HELPERS.md),
update sequence 2 and copied-game revision 6. Its native adapter, service and
prepared UI archive below are unchanged. Normal player operations ship no
PowerShell scripts and do not change execution/security policy. RC3.5 is locally
built and tested; the published RC3.4 release remains intact.

RC3.5 verification passes 11 native update handoff tests (including a real isolated
Inno installer), eight Windows API/environment checks, 19 updater-core checks,
24 installer checks, 26 desktop-backend checks, nine updater-backend checks,
eight elevation checks, nine network-runtime checks, 30 LAN checks and 11
Internet-start/backend checks. A real scoped firewall/Wintun lifecycle and
packaged DE/EN UI/private-pipe checks also pass. Fresh preparation in a path with
spaces, umlauts and sharp-s, real LAN permission, and cleanup/uninstall work with
PowerShell subprocesses expressly forbidden by the verification harness. The
original executable/archive and policy values remain unchanged; a fixture save
survives cleanup. No game or new multiplayer match was started.

Local RC3.5 deliverable:
`outputs/S7-WePlay-RC3.5-Windows-Helpers-Preview/Setup.exe`, 105,618,249 bytes,
SHA-256 `2fffd85d93933b7e31f9735d480aa14bfb6abec8e0e2953267016f4c64c99627`.
Its matching signed `weplay-update.json` uses sequence 2 and tag v2.0.0-rc3.5.
The shipped native helper validates that actual signed installer without an
installed .NET runtime and without executing Setup. Neither RC3.5 asset has
been published by this implementation task.

RC3.4 changes the prepared UI archive and copied-game network revision to 5.
All players need matching prepared content. Its native adapter and service
payloads are unchanged from the preceding preview:

- Adapter SHA-256: `14f9bc05638480a9f3cf32c6e668ce35781e47dc368a526d0c75223b5412bf16`.
- Service SHA-256: `b228b88e6200bce03ea4dcd6befdb989e9b218b37e29a259b321662cbda61206`.
- Prepared UI archive SHA-256: `38edae6dd347e9bc4afb9f3474285e8458f3d712dd058492f56bacc066ce15e4`.

Targeted checks passed: seven achievement/Lua checks, 18 updater-core checks,
nine updater-backend checks, five real Windows handoff fixtures, 26 desktop
backend checks and 23 installer checks. The packaged application passes DE/EN
UI rendering and the private-pipe roundtrip. Separate-copy preparation verifies
the expected archive and leaves the source executable/archive unchanged.
These checks did not start a game. See
[achievement evidence](ACHIEVEMENT-NOTIFICATIONS.md) and
[update trust, recovery and publishing](UPDATES.md).

The owner reported two three-hour play sessions without an observed desync on
2026-10-29. Build, map, transport and host direction were not specified. Earlier
reverse-host failures remain part of the evidence; see
[two-PC results](TWO-PC-DELIVERY.md) and [network reliability](NETWORK-RELIABILITY.md).
Avoid treating synthetic checks or this report as universal stability claims.

## Releases

WePlay v2 releases publish the locally verified full `Setup.exe`, its signed
`weplay-update.json` and EasyTier corresponding source. The legacy v1 tag workflow
does not create or overwrite WePlay v2 releases. Keep private signing material
outside Git and release assets. Source cleanup removes files from the current
tree without rewriting historical commits.
