# Gold / History production integration contract, version 1

This contract separates the launcher handoff from native game authority. It is
the implementation target for the production port; its presence does not mean
the ordinary adapters or a released installer already provide mixed play.

## Activation and compatibility

The existing `WePlayInitializeStandalone` entry ABI is retained. The normal
Gold build uses `LocalSimulation + PlatformIndependent`, the original prepared
executable and its private portable profile. Neither `AccountLogin` nor an
isolated laboratory executable is a production build input.

An absent `WEPLAY_CROSSPLAY_V1` selects existing solo / History-only behavior.
An explicitly supplied value must equal `1`; malformed or incomplete mixed-mode
configuration must fail initialization rather than silently select another
mode. The launcher supplies the following process-local environment values only
for a mixed session. They are not installation settings or account data.

| Variable | Value |
| --- | --- |
| `WEPLAY_CROSSPLAY_V1` | `1` |
| `WEPLAY_CROSSPLAY_V1_ROLE` | `host` or `guest` |
| `WEPLAY_CROSSPLAY_V1_PEER` | Channel edition number: Gold `1`, Ubisoft History `2`, Steam History `3` |
| `WEPLAY_CROSSPLAY_V1_PORT` | Decimal TCP port, `1..65535`; the native endpoint is always `127.0.0.1` |
| `WEPLAY_CROSSPLAY_V1_SESSION` | 16 nonzero session bytes, 32 lowercase hexadecimal characters |
| `WEPLAY_CROSSPLAY_V1_KEY` | 32 nonzero per-client authentication bytes, 64 lowercase hexadecimal characters |
| `WEPLAY_CROSSPLAY_V1_IDENTITY` | 16 independent local identity bytes, 32 lowercase hexadecimal characters; Gold uses a nonzero little-endian `uint32` at most `0x7fffffff` followed by 12 zero bytes; History uses any nonzero 16-byte random value |
| `WEPLAY_CROSSPLAY_V1_CONTENT` | 32 nonzero bytes identifying the mutually checked content contract, 64 lowercase hexadecimal characters |
| `WEPLAY_CROSSPLAY_V1_NAME` | Local display name, 1..64 printable ASCII bytes |
| `WEPLAY_CROSSPLAY_V1_PROFILE` | Absolute, resolved portable profile directory; must match `StandaloneProfile::ProfileDirectory()` |
| `WEPLAY_CROSSPLAY_V1_GOLD_AUTH_PORT` | Gold host only: decimal local UDP authentication service port, `1024..65535` |
| `WEPLAY_CROSSPLAY_V1_GOLD_CONFIG_PORT` | Gold host only: decimal local HTTP configuration service port, `1024..65535` |

Both Gold port fields are mandatory for Gold and absent for History. The
launcher owns one independent local Gold service with an authentication UDP
socket, a backend UDP socket and a configuration HTTP endpoint. All bind to
`127.0.0.1` and accept only Gold's own `127.0.0.2` source. The backend endpoint
is supplied by the original service ticket, not an extra environment variable.
Its implementation reuses `GoldLoginAdapter` with `account_mode=True` and
`session_mode=True`, a `LocalGatherings` owner and the existing `ConfigService`;
there is exactly one Gold identity and no phantom second player. The independent
service is not a Ubisoft login and reads no vendor credentials or account cache.

The native/UI display name is `NAME`, and the service's explicit local principal
is the same validated Gold `IDENTITY` little-endian scalar. Its launch password
is lowercase hexadecimal
`HMAC-SHA256(KEY_bytes, b'S7X-GOLD-LOCAL-AUTH-1' + SESSION_bytes)`.
This separates the service password from the channel authenticator. Endpoints,
secrets and tickets remain process/session-local and are not persisted in
installation descriptors, catalogs or reports. Closing the owned game closes
its service. Existing solo and History-only starts create no Gold service.

The native channel slot is derived from role: host `1`, guest `2`. The native
adapter obtains the executable identity from its existing full-image guard,
not from a launcher-supplied replacement hash. The existing versioned channel
authentication and frame formats remain in use. Broker/network work must not
add remote native sockets: LAN/EasyTier transport terminates at the local
broker. That transport still requires its own qualification.

Version 1 admits a Gold host and exactly one original Ubisoft/Steam History
guest. Its bounded roster is two humans and two native AI slots on a four-player
map. A History host with a Gold guest, four humans, additional peers and other
roster layouts are rejected until separately implemented and tested. Existing
History-only sessions use their existing path.

### Required reverse role extension

The required production scope also includes **original Steam History as the
actual host and Gold as its guest**. The paragraph above describes the current
parser/component boundary, not the final feature requirement. This reverse path
is not implemented or admitted by the current normal entry, parser or catalog.
Do not advertise it by changing a label or relaxing the existing role checks.

| Actual host | Actual guest | Required final qualification |
| --- | --- | --- |
| Gold | Original Ubisoft History | Retain and qualify the existing forward path |
| Gold | Original Steam History | Retain and qualify the existing forward path |
| Original Steam History | Gold | Implement and qualify the reverse path |

The common host always owns channel/native slot 1 and the guest slot 2,
independently of edition. The actual host's original UI owns map, template,
positions/teams/coop, AI, castle setup, NEO selection, start and selected save.
Host bootstrap must therefore capture either Gold's original host state or
Steam History's original host state. Gold guest preparation needs its own
original native row claim, backend role projection, consumed castle/configuration
checks and applied-choice queries. The History guest claim is not reusable by
casting a Gold object or rewriting its role flag.

Each edition's initial local scaffold is distinct from common host authority:
the independent Gold login/session service may still prepare Gold's own local
NetZ session, but it does not make a common Gold guest authoritative. Its local
SDK source binding and the role-derived control-channel source binding must
remain independently guarded. The Gold local service is required for Gold in
either common role; extending its current host-only activation/secret guards
must accompany the real guest implementation, not precede it as a bypass.

Original Steam History start/setup/root events must be captured as host events,
delivered through Gold's verified original reader and queue consumer, and
acknowledged against Gold's actual loaded world. Preserve original human Ready
actions and host start authority. Mixed NEO still uses shared mask 19 and the
same explicit host checkbox/peer agreement in either direction; History NEO5
must not run in parallel or have its Lua handshake fabricated.

The reverse save test must originate in the Steam History host's original save
UI and actual owned profile. A fresh owner/session/keys and namespace projections
must then resume that host-selected archive through the original mixed file
stream, restore the admitted NEO state and continue original turn checksums and
logistics. Reusing a Gold-host archive or fixed old identities cannot qualify
the reverse direction. Final packaged validation is required for all three
rows above, including custom castles, common start, active NEO, host save,
coordinated reload, continued logistics/checksum agreement and normal exit.
Ubisoft History hosting Gold remains outside this required reverse-path scope
until separately implemented and qualified.

### History local startup service

The normal History adapter also requires its existing owned loopback startup
service and a fresh `LocalData/s7-local-runtime.ini` before `UPLAY_Start`.
`platform_standalone.launch_environment` alone does not prepare that service.
The original five-minute configuration freshness guard remains in force. Bind
service/configuration/profile/save ownership to the same game lifetime, close
the service on owned exit, and preserve the persistent solo profile identifier.
The normal local platform save directory is `LocalData/saves`, in addition to
the game's isolated History profile and its own save/options paths.

The History `IDENTITY` bytes are an opaque WePlay channel identity. A local
runtime UUID text, the Storm descriptor's optional `ProfileID`, the native
Storm peer GUID and a preparation row's scalar `principal` are distinct fields.
No Windows-GUID or UUID network-byte-order equivalence between them is currently
qualified. Do not derive a UUID with an assumed byte order or substitute that
UUID for a captured native principal. A separate fresh owner-scoped runtime
profile UUID can be associated with the same session without modifying the
persistent solo identifier. Any claimed native GUID equivalence needs its own
original-parser/capture evidence.

## Shared content catalog and broker wire

`CONTENT` is the SHA-256 of a single qualified catalog containing all three
production profiles, not an edition's `compatibility.network_id`. Its exact
canonical UTF-8 JSON representation uses `ensure_ascii=True`, `sort_keys=True`,
`separators=(',', ':')`, no BOM and no trailing newline. The only top-level keys
are `format` (`weplay-crossplay-content-v1`), `native_protocol` (`1`),
`neo_features` (`19`) and `editions`. `editions` is an array ordered `1,2,3`;
each object has only `edition` and `files`. Each nonempty `files` array is sorted
by its unique ASCII `id`; records contain only `id` and a lowercase 64-character
nonzero `sha256`. IDs are paths relative to that edition's installed game root
(Gold `Data/...`, History `Base/...`, and the adapter's actual installed path),
not paths relative to the installer payload. IDs use `/` with no empty, `.` or `..`
components, leading slash, colon, control characters or backslashes. Runtime
preferences, save files, account data and absolute installation paths are absent.

The release catalog must contain the independently qualified native adapter,
original executable and the reviewed gameplay/script/map inputs for each profile.
The launcher verifies its installed profile's corresponding inputs against that
approved catalog before using its common digest. A self-generated arbitrary file
list is not a compatibility qualification. No final catalog is frozen yet: it
depends on the final qualified production adapters and payloads. There is no
fallback to an arbitrary nonzero digest while mixed play is unavailable.

Use the existing `tools/gold/crossplay_server.py` broker and native
`crossplay_channel.cpp` wire implementation. Its header is little-endian Python
`struct.Struct('<4sHH16sQBBHI')`, followed by payload then a 32-byte HMAC-SHA256
over header plus payload. Magic is `WPX1`, version `1`, sequence starts at `1`
independently in each direction, reserved is zero and payload is at most 65536
bytes. The session has two distinct client keys. Client-to-broker authentication
uses the source slot's key; broker-to-client authentication uses the destination
slot's key. Gold host is launch/slot `1`, History guest launch/slot `2`; native
source addresses are respectively `127.0.0.2` and `127.0.0.3`. The broker listens
on `127.0.0.1`. Existing Hello, namespace projection and payload frames retain
their implementation's full identity, executable, session and content checks.

## Native choices and save provenance

The version-1 read-only UI query is
`G.UI.WriteToConsole('weplay-crossplay-v1:mode')`: it returns `host`, `guest`, or
`solo`. `...:phase` returns `pending`, `local-lobby`, `connecting`, `admitting`,
`roster-bound`, `closed`, or `failed`. An older adapter can return nil; that
must retain ordinary solo/History-only UI. Unknown owned commands return false.
These queries never expose keys, identities, endpoints or profile paths.

The launcher requests automatic entry into the original host/guest lobby for
explicit mixed mode. The interface uses the original login/session-creation
actions and their actual completion callbacks. It must not wait for the removed
solo-only main-menu Multiplayer button, manufacture native completion, toggle
Ready automatically, or treat `roster-bound` as a started world. UI query support
alone does not enable mixed-play capability in the launcher. The production
entry remains closed until the complete runtime is attached and qualified.

The authenticated control channel starts before either original local lobby.
Gold captures its actual original local Confirmed event, selected map and host
choices before sending bootstrap payload version 2. On History only, these
read-only queries become available after authenticated receipt:

| `weplay-crossplay-v1:` command | Successful Lua return |
| --- | --- |
| `bootstrap-map` | Observed nonempty map string |
| `bootstrap-flags` | Decimal **string** `"0"` through `"7"`; bits `1` positions, `2` teams, `4` coop |
| `bootstrap-template` | Observed template string; `""` explicitly means no template |

Each returns boolean false while unavailable, after failure/closure, on Gold or
outside mixed mode. An empty template is not an unavailable result. The owner
thread, exact native session and actual local Confirmed host identity bind
capture. Original configuration snapshots and template reads must remain stable
across the observation interval. A busy configuration lock retries; an invalid
object or method guard rejects capture. No map-only/default-choice fallback is
accepted.

The guest waits for all three values. After the original
`G.map.loadSetupMapConfig(map, templateOrNil)`, it invokes the original
yield-capable History function once:

```lua
Network.SessionCreate(map, false, false, 2,
    positions, teams, templateOrNil, coop)
```

The two false arguments initialize the local backend. The literal `2` is the
original UI argument, not evidence of the maximum human-player count. Decode
the three boolean choices from the received flags; use nil only for the
observed empty template. Original jobs must remain able to yield: do not wrap
this call in `pcall` or `xpcall`. These values are not Ready or world authority.
The guest does not invent a Gold session in Storm's browser or open the host's
save file locally. Its original LocalConfirmed
then permits native admission and role projection. `roster-bound` alone must not
open the guest lobby: the host configuration must be consumed and the admitted
local preparation row and castle must be observed before presentation. Actual
host template/team/position/coop settings must come from that applied setup;
bootstrap defaults must never be displayed as authoritative host choices.
The separate setup-bound presentation query is still pending. AI/save/start
agreement follows separately.

`HistoryGuestClaim` ports the original History preparation-row claim into the
production source. After authenticated roster binding, it verifies the original
edition-specific methods and actual four-player preparation owner, invokes the
original prepare/claim/identity/name methods for local slot 2, and queues the
original local Confirmed event carrying the captured castle. Backend host-role
projection follows that claim. Its read-only postcondition additionally checks
the unchanged session/preparation interval, original row identities/names and
the actual consumed castle. Queueing is not consumption, Ready or world-start
permission. History routing CID zero is valid for either participant. Only
`UINT32_MAX` is unavailable; owner matching, distinct participants, original
slot/status/name/principal checks still apply. The earlier nonzero check had
incorrectly carried a Gold identity restriction into History routing.

A bounded diagnostic pair using the original Gold and Ubisoft History
executables under DXVK now reaches native roster binding, returns successfully
from the original History preparation claim and queues its captured castle,
then completes backend host-role projection. The ordinary production entry
remains closed; this run explicitly attaches the component in private,
instrumented normal adapters. It does not qualify original configuration
consumption, a common world, Steam History, NEO, reload or a final installed
package. See the [claim correction receipt](receipts/native-history-zero-cid-20261116.json).

The production source now sends the host's original `SessionConfig` snapshot
after roster binding and projects only its three routing fields for History.
The guest arms an exact configuration receipt before invoking the original
clone/queue operation. A separately guarded observer runs after the original
History configuration consumer, requires the same native cache/session/owner
and serialized event bytes, then compares the original map and the serialized
fields of all eight cache slots with that received configuration. A pre-existing matching map, successful
enqueue or local Confirmed event cannot substitute for consumption. Failure,
changed ownership or a ten-second deadline invalidates the transaction. This
fresh-configuration path rejects saved-kind; saved configuration uses the
separate source-bound resume protocol. The [component receipt](receipts/native-production-configuration-consumer-20261116.json)
does not establish actual mixed-game delivery, applied template/flags, complete
setup/root agreement or a `setup-bound` UI result. Those remain separate gates.

Later bounded Gold/Ubisoft diagnostic pairs now observe the exact original
configuration consumer. The original History cache retains zero station
placeholders; the read-only comparison therefore uses the same typed routing
binding as the Gold source snapshot. It still checks the unchanged raw cache,
map, all eight slots and owner interval and never writes that cache.

Those pairs also expose a separate initial host-Ready difference: the original
Gold host snapshot has Ready set, while History's configuration consumer leaves
its prior host-Ready value unchanged. The source now permits the existing
original History Confirmed events and preparation setters to project only that
actually observed host value, after exact configuration consumption and only
when every other snapshot byte already agrees. This cannot change the guest's
own Ready. The original preparation/cache postcondition and complete snapshot
comparison must then succeed. This is one initial projection per fresh
transaction, not general Ready handling or a start permission.

The expanded 1,077-check receipt fixture rejects every non-host-Ready byte
change and retains exact-consumption, routing, owner and deadline checks. The
first pair stopped before deployment because the measured startup RAM buffer
was unavailable. After resources were freed, bounded pairs against both original
History editions now consume the original configuration, apply the observed
host-Ready through the original events/setters and pass the complete native
configuration-cache comparison, with 70 Ubisoft and 69 Steam applied observations.
Both pairs exit normally with code zero and strictly restore their guarded
files, profiles, History runtime and platform saves; their owned services close.

The guest must first enter its original session browser and wait for its actual
discovery completion, idle browser state and original discovery state `2`.
Login alone is insufficient: an earlier fresh run returned false from the
original create call before that readiness. The diagnostic correction uses
the original callback and lifecycle, with no sleep, create retry, fabricated
Ready or NAT override. The [updated bounded receipt](receipts/native-configuration-readiness-20261116.json)
does not qualify complete setup/root agreement, applied template/flags,
a common world, NEO/reload, reversed host roles or a final installed package.

The source also provides read-only `applied-flags` and `applied-template`
queries. They return a decimal string `0..7` and the actual template string
(including an observed empty string), respectively; unavailable values return
Lua `false`. They require the owned History guest, exact configuration
application, its admitted preparation/castle, a stable native cache/session and
actual values equal to the authenticated host choices. Flags are read under the
original cache mutex. The template is copied from the original owned backend
descriptor with its original inline/heap string layout and checked ownership
roots. Bootstrap values are never returned as substituted applied values.

With fresh public discovery/Coop helpers, bounded original Ubisoft and Steam
pairs independently observe flags `0` and an empty template matching the host,
74 and 75 times respectively. Both end normally with strict restoration. The
first nonzero-flags attempts stopped before deployment at the unchanged RAM
guard. After resources became available, actual Ubisoft and Steam pairs also
observed flags `7` and an empty template through the committed public queries,
51 and 22 times respectively, with normal exits and strict restoration.
Nonempty templates remain unqualified. The reader passes 813 synthetic
checks and compiled guard checks against both original image layouts; those
guard checks do not execute game methods. The source now derives the UI-only
`setup-bound` result from `roster-bound` plus `GuestPresentationBound()`, which
requires those actual owned choices and the admitted preparation/castle. This
permits presentation of the original guest page only. The underlying runtime
phase remains `RosterBound`; complete setup/root, content, Ready and start
agreements remain separate. Actual Gold-host pairs now exercise the public
derived phase and presenter with both original History editions: the original
guest page is created and visibly shows both assigned humans and its Ready
button, with flags `7` and an empty template. The Ubisoft probe's Gold watchdog
closed normally after 45 seconds; its subsequent channel EOF is not a desync.
After correcting that second watchdog, the Steam probe retained matching
metadata through 119 seconds before both clients closed normally. Both probes
strictly restored files/profiles and closed their services. They still use a
private frame driver and instrumented normal adapters. No Ready action, world
start, active NEO or shared reload was requested in these presentation probes.
See the [bounded applied-choice receipt](receipts/native-history-applied-choices-20261116.json).

The production runtime now observes original targeted/broadcast transport calls
and routes genuine History Ready requests through the authenticated channel.
Gold enqueues its original state event and publishes a snapshot only after
observing actual consumption. History projects that snapshot through original
Confirmed clones and preparation setters, then observes the native result.
Queues and consumer deadlines are bounded. The observer preserves incoming
registers, flags, LastError, x87/SSE and direction flag, forwards unconditionally
exactly once, and rejects foreign owners, changed call sites and recursive
callbacks. Its source passes 377 synthetic checks and original-image guard
emulation for all three editions. Actual Gold-host DXVK pairs with both original
History editions install the observers and retain the original configuration and
guest page through 119 seconds, then exit normally with strict restoration.
These pairs request no Ready action: the existing NEO UI gate remains intact.
Actual Ready delivery, the production NEO lifecycle, complete setup/start and
shared reload therefore remain unqualified. See the [Ready transport receipt](receipts/native-production-ready-transport-20261116.json).

The shared logistics source components have also been migrated from the bounded
world prototype: typed world/task bindings for all three editions, the FIFO and
explicit-intent ledger, both Gold comparator paths, the History comparator,
individual-priority entry bridges, replay clock and NEO2 snapshot/restoration.
They represent feature mask `19` (FIFO, intent and priority commands), not the
complete History NEO5 variant. Private passive diagnostics were removed; native
method/layout guards remain. Synthetic verification covers 210 layout/ledger
checks, 94 intent/active-resume checks and the comparator/priority-entry ABIs.
Another 211 compiled guard checks use offline original-image emulation and never
execute original game methods. Run `native/build_crossplay_logistics.ps1` with a
fresh output directory under `work/`. These components are not yet called by the
production session controller, provide no independent world/Ready authority and
must not be stacked with History's NEO5 hooks. Session selection, shared start,
live logistics and source-bound reload still require runtime integration and
actual package tests. See the [shared logistics component receipt](receipts/native-shared-logistics-components-20261116.json).

Bootstrap payload magic is eight bytes `S7XBOOT\x02`. Its 56-byte header contains
sender and recipient channel IDs (16 bytes each), then little-endian `uint32`
saved-kind (`0/1`), map-byte count, host flags (`0..7`) and template-byte count.
Map bytes precede template bytes; both are printable ASCII, at most 255 bytes,
without a terminator. The nonempty map is a relative engine resource name.
The template may be empty, relative, or Gold's original virtual resource
`\Download\Template` (optionally with a relative child). Absolute OS paths,
parent traversal, malformed counts and old map-only version-1 frames are
rejected. The existing authenticated channel binds the payload to its peer and
session. It contains no save token, AI choice, native Ready or world-start
permission. Choices come from the owned host's original configuration and
original template getter, not from launcher values or test constants.

The [host-choice receipt](receipts/native-production-host-choices-20261116.json)
records the bounded actual Gold observation: original session creation and
local host registration under DXVK, flags `0` and `7`, and an observed empty
template. The flags-7 control uses the original discovery notification with no
completion relay. An enum typo in the laboratory driver had blocked earlier
observations; it was not evidence of a missing production callback. Nonempty
templates and normal mixed guest preparation remain unqualified. These probes
do not qualify a common world or final production package.

The native host UI owns map, AI, castle, start and NEO selection. The launcher
does not write a preparation buffer, synthesize Ready, toggle a native start
flag or provide fixed map/player/save values. The normal native observer must
capture the original host action and the resulting setup on its owner thread.
Map identity and the full closed-slot/player projection must agree before start.

Mixed NEO advertises exactly mask `19`: FIFO (`1`), saved individual intent (`2`)
and individual-priority commands (`16`). Its enabled value comes from the host's
checkbox and is acknowledged by the peer. History-only NEO5 remains a separate
capability; mixed mask `19` never advertises distance (`4`) or pickup (`8`). The
bounded real-world qualification currently covers Monhaim Valley with two humans
and two AI. It does not establish arbitrary-map or full NEO5 compatibility.

For resume, the original host UI selects the save. The adapter captures that
actual token and map from the owned original configuration and resolves the
archive within that same portable profile's `Savegames` directory. There is no
`s1009`, retained test case, embedded NEO pickle, or launcher-supplied checkpoint
constant in the production path. The file must be locked against replacement
and writers while its identity and metadata are read.

The native checkpoint binds the selected archive identity, its actual saved
metadata/setup provenance, the separately verified original `SaveGame.s7`
stream, logic/replay clocks and saved NEO state. The native port must derive
these from the selected source and observed native consumption. Launcher/broker
code relays the versioned checkpoint and original event/file frames; it must not
invent missing checkpoint fields. The guest consumes the original native file
stream and restores the admitted NEO payload before its first scheduler step.
Transport credit, file receipt and adapter Prepared/Queued receipts are not
native Ready or proof of a loaded world.

`CrossplayResumeSource::CaptureSelected` now derives this source checkpoint
without caller-supplied clocks or file/NEO hashes. Its saved setup provenance is
`SHA256(b'S7XSAVE\x01' + whole_archive_sha256_bytes + observed_map_ascii_bytes)`;
it is distinct from the native setup/root signature and does not replace that
later comparison. The returned lease denies writers/replacement until the owned
source is released. Its enabled/disabled NEO metadata is decoded without Lua
execution. Production admission must still bind the observed native map/token,
portable profile, original stream digest and ownership interval.

`CrossplayGoldResume::Install` is the standalone source port of the bounded
request-postload exception. It captures deserialized original fields, accepts
only saved request states `0/1/6` at both exact Gold callers and validates the
entire original registry before the first scheduler. Unlike the older diagnostic
fallback, it rejects changed state/waits instead of writing them back. Release
must be called on the same owner after native teardown; foreign hooks are never
overwritten. These components are not yet connected to ordinary game startup.

## Lifetime and qualification

All native session operations remain on the verified simulation/UI owner thread.
The initialized process context, portable profile, session, peer projections and
ownership interval bind every admission. Rejoin invalidates old pending events,
save fragments, NEO agreement and setup/root ownership. A channel failure stops
new delivery and returns through the native disconnect path; the launcher must
not claim a successful match or kill a healthy client to hide failure. Normal
game exit closes the channel and releases owned queues.

The launcher must keep the mixed-play capability unavailable until the ordinary
production adapters implement and pass this contract. Updating installer pins
alone does not enable it. RC4 remains held until final packaged
Gold-host/Steam-guest, Gold-host/Ubisoft-guest and required Steam-host/Gold-guest
pairs demonstrate common world start, active NEO logistics,
host save, shared reload, continued original checksum agreement and clean exit
under DXVK. The private Native162 results are implementation evidence, not that
final-package qualification.
