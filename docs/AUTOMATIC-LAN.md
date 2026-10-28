# Automatic LAN directory

`Start-S7-LAN.cmd` checks the prepared copy and opens a LAN picker. Players
enter their display name, select a discovered lobby or create one, then choose
their graphics device. No IP-address entry is required. The ordinary game's
multiplayer screen still creates and joins the actual match.

The new directory is experimental. The earlier two-PC build joined and started
in both host directions, but a reverse-host sustained match desynchronized.
That is not resolved by discovery, content fingerprints or the new tests.
Three or more real game clients have not yet been qualified.

## Names and developer mode

The name is stored only in `LocalData/player-settings.json`, outside the content
fingerprint. It changes neither the stable local identity nor another player's
settings. Names have a strict 24-byte UTF-8 bound and exclude control characters.

Both the old explicit-address LAN path and the new group path set `lan_game`.
The native patch validator checks the original developer-mode getter, but never
replaces it for LAN, even if `developer_mode` is requested. Its original false
initial state and original Lua getter remain. The fixed status label is
`local-lan-developer-mode-disabled`. The launcher offers no LAN developer switch.

Content unlocks are separate: the existing privilege and campaign patches stay
enabled. The new reward predicate follows the existing offer/identifier lookup,
then returns its existing unlocked result. It does not add missing assets,
fabricate SDK store responses, complete campaign progress or online achievements.

## Directory and admission

`LobbyDirectory.list_rooms/resolve` is the provider boundary. `LanDiscovery` uses
bounded UDP query/reply discovery on port9112, fresh nonces, short cached TTLs,
fixed endpoint ports and selected-interface source checks. The reply's actual
source address is authoritative. Discovery is neither admission nor a match.

The LAN coordinator binds its HTTP control/configuration service to the selected
private address on TCP9110, and the existing game-service transport to UDP9111.
Discovery selects its outbound interface only on its own sockets. It changes no
global routes or adapter settings. Multiple IPv4 addresses on the same interface
are deliberately rejected until exact source selection is supported.

The control format is `s7-lobby-control-v1` under `/public/lobby/v1/`:

| Request | Purpose |
| --- | --- |
| GET status | Bounded active/member counts and content fingerprint |
| POST admit | Admit or renew the actual source address, with matching content ID |
| GET peers | Exact current addresses, revision and lease duration |
| POST leave | Remove only the actual source's lease |

The coordinator is reserved among at most eight addresses. Its own game must
also obtain a lease. One game participant per IPv4 address is supported; the
map's slot limit still governs an actual match. Each client renews every three
seconds and atomically publishes its exact peer list under `LocalData` with a
15-second local `GetTickCount64` deadline. Failed renewals never extend native
routes. No subnet-wide or public Internet game permission is granted.

The native file reader runs on an explicitly started/stopped helper thread,
not inside `UPLAY_Update`. It publishes a short locked snapshot and calls no
game or SDK function. Stop immediately revokes dynamic peers, requests I/O
cancellation and uses a bounded join. The already hooked module is pinned so
a delayed file operation cannot outlive its state; a timed-out worker prevents
a second worker until it has been joined. Kernel receive/send buffers and
events retain their original game ownership. Bounded metadata distinguishes
pending, completed and consumed overlapped operations before exposing results.

The coordinator is independent of the player who hosts a particular match.
Leaving its game preserves other participants' sessions. With no active game
leases, its service ends after a minute; maximum lifetime is24hours. Its own
service child belongs to a kill-on-close Windows job, so abrupt hub termination
does not leave that child running. The coordinator PC must remain reachable;
coordinator migration is not implemented.

## Private firewall helper

`LAN-Netzwerk-freigeben.cmd` is a separate, deliberate user action. Windows asks
for elevation. It creates only inbound Private/LocalSubnet rules for the exact
prepared copy's game, Python and .NET executables. It enables no Public rule and
changes no network category or global firewall setting. Running its PowerShell
helper with `-Remove` removes only those uniquely named rules.

## Future community provider

A community directory can implement the same typed list/resolve boundary. Only
the explicit `lan-direct` endpoint kind is currently implemented. Public service
authentication, Internet transport, NAT traversal, moderation, persistence and
public hosting have not been built or deployed; LAN announcements cannot select
an arbitrary remote URL or silently turn this into an Internet client.

## Verification boundaries

Tests cover nonce/schema/rate bounds, multi-member admission, exact peer expiry,
shutdown races, native asynchronous receive lifetimes, and confirmed/pending
slot accounting. Separate socket smoke checks exercise real private-interface
discovery plus source-bound HTTP admit/renew/leave. These do not execute the
simulation or establish multi-PC endgame stability. The native FP diagnostics
only observe bounded control-state changes; they never normalize floating-point
state, remove simulation checks or conceal a desync.

Revision 2 verification:184 Python tests (one skipped symlink-privilege case),517
native LAN,78 legacy guard,92 platform,1,049 bootstrap,18 connection and24 CPU
checks pass. The service passes116 focused lobby/capacity,8,796 full service,
38 login-network and477 login-RPC checks. Error mutants restore the slot-count
and lease-generation defects and fail their new regressions; native mutants
also exercise the developer guard, FP diagnostics and reward predicate.

The package installs successfully from the original HE installation into a
fresh separate copy. Its helper/form starts and its scripts parse; supported
computer-use tooling does not expose the PowerShell-hosted form for visual
automation, so no visual picker usability result is claimed.

The installed-copy check caught an isolated-Python import-path error which
regular development Python did not reproduce. The hub entrypoint now adds its
own verified launcher directory, with an isolated-interpreter regression.
Static send/receive review also caught an important native distinction: send
completion does not use `WSAGetOverlappedResult`. The revised guard permits
reuse of that same submitted send request only after its existing kernel
completion status is final; receive completion still requires its source gate.
Eighty real repeated-send cycles exercise the game's no-GetResult pattern.
The first internal package was superseded before a game was launched with it.

The final r2 installed-copy test uses its own embeddable Python, coordinator,
Windows job, discovery and source-bound admission. Its UTF-8 group record is
accepted by the actual DLL and its foreign test EXE is rejected. Four final
normal coordinator restart cycles pass. Earlier cold-start attempts aborted
after service readiness without a classified cause; an independently bounded
direct diagnostic and the final normal retries passed, so startup under heavy
load is not qualified as reliable. No game was started for these checks.

Revision 3 preserves these changes and fixes reproduced RPC loss under delayed
ACKs/backpressure and nested native connect-thread entry. Updated checks,
candidate artifacts and unqualified gameplay boundaries are documented in
[NETWORK-RELIABILITY.md](NETWORK-RELIABILITY.md). Graphics and CPU-hang live
investigation is coordinated exclusively in the separate graphics chat.
