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
Gold still captures its actual original local Confirmed event and selected map
before sending the versioned bootstrap context. On History only,
`G.UI.WriteToConsole('weplay-crossplay-v1:bootstrap-map')` returns that observed
map after authenticated receipt, and false while it is unavailable. The guest
waits for this value, then invokes its original local session preparation once
with that map and an empty local save token. It does not invent a Gold session
in Storm's browser or open the host's file locally. Its original LocalConfirmed
then permits native admission and role projection; only then is its normal guest
lobby presented. Roster/setup/AI/save/start agreement follows separately.

Bootstrap payload magic is eight bytes `S7XBOOT\x01`, followed by sender and
recipient channel IDs (16 bytes each), a little-endian `uint32` saved-kind (`0/1`),
a little-endian map-byte count, then the bounded printable ASCII relative map.
It is carried inside the existing authenticated channel. It contains no save
token, AI choice, native Ready or world-start permission. Its map came from the
owned host's original configuration, not from the launcher or a test constant.

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
alone does not enable it. RC4 remains held until the final packaged Gold/Steam
and Gold/Ubisoft pairs demonstrate common world start, active NEO logistics,
host save, shared reload, continued original checksum agreement and clean exit
under DXVK. The private Native162 results are implementation evidence, not that
final-package qualification.
