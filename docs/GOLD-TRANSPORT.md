# Gold transport development checkpoint

Gold's opt-in account adapter now completes the original CNetwork account login
and profile startup under DXVK: native account state 6 and enabled RendezVous.
The original UI reports Online and opens its Session Browser and map selection.
The separate loopback service uses a fresh launch secret and its own tickets.
Gold/History crossplay is not implemented: the lobby bridge and shared
simulation transport remain incomplete. Gold is excluded from normal
multiplayer admission. The History service has no fallback to this protocol.

## Independent local peers, 2026-11-11

`tools/gold/peer_server.py` serves two to eight separately configured local
launches. Each has its own launch secret, principal, ticket, profile, UDP auth
and backend sockets, and HTTP configuration origin. Only the bounded Gold
registration registry is shared. Credentials and tickets from another launch
are rejected. Registration authority remains the exact authenticated connection;
another fully logged-in peer cannot update, delete or claim its host record.
Only explicitly eligible public title records and their registered endpoint are
readable through the opt-in peer laboratory's discovery routes.
Disconnect and expiry remove only that connection's records. Endpoints bind only
to IPv4 loopback; this service does not yet admit peer joins or History clients.

Run `python tools/gold/peer_server.py --ready-file <private-ready-path>
--report-file <private-report-path> --duration 600` with a fresh, environment-only
`WEPLAY_GOLD_LOCAL_PEERS` JSON array of `{ "name": ..., "secret": ... }` objects.
Names and 256-bit hexadecimal launch secrets must be distinct. The environment
entry is consumed at startup; do not store or commit it. The ready file contains
only indexed endpoint triples. Set each child's existing `WEPLAY_GOLD_LOCAL_NAME`,
`WEPLAY_GOLD_LOCAL_SECRET`, `WEPLAY_GOLD_LOCAL_PORT` and `WEPLAY_GOLD_CONFIG_PORT`
to that launch's own values. Never give another client the host's secret.

Ten synthetic tests use actual loopback sockets to check independent logins,
ticket/credential rejection, native configuration routing, connection ownership,
disconnect/expiry isolation and input bounds. The existing 40 login, account,
registration and codec tests also pass. Separately, the actual DXVK Gold client
completed its native account login, opened the Session Browser and created a
named host lobby on this service. It exited normally; all 16 backed-up shared
profile/save files were unchanged. The second configured identity remained
unused in that initial game test. See the [peer-service receipt](verification/gold-peers-20261111.json).

The subsequent native account change accepts one bounded printable ASCII launch
name per process instead of requiring the original fixed label. The facade's
original string constructors, SDK login and authenticated native player creation
now use the same independent name. Re-login and vendor authentication remain
outside this local adapter. Native fixtures cover one-, thirteen- and 64-character
labels, malformed names, stack/nonvolatile registers and the SDK-only regression.

Two actual Gold clients subsequently completed independent account logins at the
same time, with separate executable-derived profiles, secrets and endpoints.
Both used DXVK at 1920x1080 with High settings. One created the original named
Monheim host lobby. The other's native browser accepted the `121/1` dynamic
session catalogue and requested `21/16` host URLs. The service now returns that
exact native StationURL list shape; `121/5` returns the genuinely empty recent
session history. Publication requires a live authenticated owner, claimed host,
open public slots, version 16 and populated title hashes/map. Leaving, expiry,
private/full/started sessions and malformed queries exclude publication.

In actual pair test 76, catalogue and endpoint requests succeeded, but the native
browser still showed no selectable session. Endpoint reachability and the later
peer join are **not qualified**. No participant or match was created on the
searching client. Both processes exited normally and all 16 shared files checked
by each runner remained unchanged. See the
[two-client receipt](verification/gold-discovery-20261111.json).

The registered address also carries the server-issued `RVCID`, independently of
`CID` and `PID`. Gold's original NAT reader requires that exact key before
starting its echo attempt. Another launch's connection ID and a station URL's
supplied `RVCID` cannot establish this value; duplicate server connection IDs
reject service startup. In actual pair 79, both native logins and named host
creation still succeeded. The search entered the original asynchronous ping
phase, waited approximately two seconds and removed the unreachable candidate.
This corrects the missing address field, while endpoint reachability remains
unqualified. Both clients exited normally with all 16 shared files unchanged.

The original multiplayer Start button also correctly refused a sole human with
an AI: it requires another human participant. That is a legitimate prerequisite,
not evidence of another turn-zero stall. No participant or readiness gate was
fabricated or removed. Genuine peer discovery, join and simulation transport
are still required before Gold/History matches can be tested.

The next native endpoint correction distinguishes the secure login-server URL
(`prudps`) from a registered player's station (`prudp`). Gold's initial NAT
traversal batching at `C97F40` supports the latter scheme. The opt-in laboratory
also answers the subsequent `11/3` query: zero selector and authenticated host
principal, returning a bool followed by the native StationURL list. Resolution
requires a currently published, live host and a live authenticated requester;
unknown, released and expired hosts and malformed queries are rejected.

In actual pair 83, both original clients logged in under DXVK High at 1920x1080.
The searching client displayed an actual selectable Monheim Valley row with
1/4 players and a valid native ping. Its host-name field remained empty. Joining
then failed with the original "Session no longer exists" dialog: the later
foreign-record read remains excluded. This establishes discovery and endpoint
resolution, not peer admission or a network match. Thirteen synthetic peer tests
and the existing 40 Gold tests passed. A metadata-only diagnostic native build
was used for this game observation; no ping/readiness/participant was fabricated.
Both clients exited normally with all 16 shared files checked by each runner
unchanged. See the [endpoint receipt](verification/gold-peer-endpoints-20261111.json).

The subsequent selection adapter answers native `121/2` with the currently
published records requested by a bounded, unique u32 ID list and version 16.
The original `21/21` read also permits a live public record to be inspected by
another authenticated client. These read permissions grant no update/delete/
host authority and do not publish private, full, started or expired sessions.
Fifteen peer tests and the existing 40 Gold tests pass.

Actual pair 84 used the regular native account adapter. The second original
Gold client joined the first through native NetZ: both windows displayed the
same two named human players, and read-only native inspection confirmed host/
guest roles, distinct local slots and connected participants. The guest's Ready
change propagated to the host. Start opened the original two-player loading
screen, then both processes ended with adapter guard code `0xe05370a1`.
All 16 shared files checked by each runner were unchanged. This establishes a
native Gold lobby, not a running simulation. The single-player simulation bridge
still excludes network calls. Backend peer participation also remains rejected;
the native NetZ join succeeded independently and does not qualify backend
admission, capacity enforcement or cross-edition identity/content checks.

### Independent loopback addresses

Several Gold processes sharing `127.0.0.1` can collide on the original fixed
UDP ports 9100/9103. A live diagnostic reproduced a failed 9103 bind and a
native "Cannot create new session" result. Earlier receive diagnostics also
found a default socket key absent from the native socket table; original NAT
request parsing still ran, but the original send queue discarded packets whose
local socket key could not be found. These observations do not justify choosing
an arbitrary remaining socket or manufacturing a ping result.

The account startup now optionally accepts `WEPLAY_GOLD_LOOPBACK_SLOT=2` through
`9`, binding wildcard and explicit `127.0.0.1` IPv4 sockets, plus previously
unbound senders, to `127.0.0.<slot>`. Native port numbers, packet bytes and native
socket ownership remain unchanged. Configuration is immutable after guard
installation begins. Existing launches without the variable retain their
previous binding behavior; external network access remains denied.

Configure the matching optional integer `loopback_slot` in each disposable
`PeerLoginService` peer entry. Nondefault slots must be distinct. The HTTP
configuration endpoint and login adapter accept only that peer's configured
source address. The registration response uses the authenticated connection's
observed source address and port. It never copies an advertised endpoint from
the request. Synthetic real-socket tests cover independent authenticated
registrations, exact source ownership, invalid/duplicate slots, HTTP ownership,
native port reuse across addresses and the source of an initially unbound UDP
sender. The complete Gold Python suite passes 64 tests.

Live DXVK High test pair 94 confirms both original fixed-port binds, native
probe request/reply callbacks, a selectable 1 ms browser entry, two-human
joining, readiness propagation and the two-human loading screen. Both clients
then stopped at the retained simulation guard. This qualifies the observed
login/lobby transition, not gameplay or cross-edition compatibility. See the
[sanitized receipt](verification/gold-loopback-slots-20261111.json).

### Native probe initiation

The opt-in peer service now relays Gold's native NAT traversal request `3/1`
(a bounded StationURL list) to the selected host as `3/2` (one StationURL).
These methods are probe initiation, not a second account login. Each target
must be another live, authenticated, currently published host. StationURL
attribute order may change during the native read/write cycle; the complete
field values must still match the service-issued registration. Duplicate,
unknown or altered stations are rejected. The forwarded source is taken from
the authenticated registration, never supplied by the request.

The relay bounds outstanding jobs, time, native sequence numbers and pending
packets. Leaving, disconnection or expiry cancels its own pending notification.
Only the exact target connection can answer its issued RPC correlation. Real
transport ACKs remain required; duplicate packets do not repeat the operation.
Six additional loopback tests cover the native request/response shape,
correlation, replay, bounds, station normalization, backpressure and revocation;
all 61 Gold tests pass.

In actual DXVK pair 88, both native account logins succeeded. The host accepted
and answered the relayed `3/2` request, then sent its own 24-byte UDP probes to
the searching client. The browser still showed no selectable session: a valid
return ping was not observed. Both clients exited normally with all 16 shared
files checked by each runner unchanged. This qualifies native probe dispatch,
not reliable discovery, a running match or Gold/History crossplay. See the
[probe receipt](verification/gold-probe-relay-20261111.json).

## Native local player identity, 2026-11-11

The opt-in account adapter now supplies its independent launch name when Gold
originally creates the authenticated local NetZ player. A later edit of the
preparation record is overwritten by incoming native player metadata. The two
online creation calls are guarded separately; offline callers are unchanged.
The adapter requires the owner thread, authenticated account, owned native
session, empty bounded name and the actual principal from the service-owned
RendezVous ClientContext. Account management's protocol client is unrelated to
that context. Existing names and excluded contexts retain the original input.
The original constructor copies the name and returns its own object identifier;
no participant, connected flag or readiness result is fabricated.

In the actual DXVK client, the original Host Match screen now showed the named
human player with colour, team and position controls. The name remained present
after selecting Bishop Matheusz through the original AI dialog. Read-only native
inspection confirmed a human player, connected status, assigned network slot and
nonempty name. Leaving the lobby worked, but creating another session in that
process returned a server error; registration reuse remains unresolved. Normal
exit and all 16 unchanged shared profile/save files were verified.

Native fixtures cover both creation callsites, all seven arguments, native
failure and success returns, stack/nonvolatile registers, LastError, excluded
identity contexts and an unexpected-caller process rejection. All 119 altered
instruction bytes were rejected. The SDK-only regression and 75 network guard
checks also passed. This qualifies the local host's identity only: no network
match was started and neither History edition was connected. See the
[sanitized participant receipt](verification/gold-participant-20261111.json).

The subsequent registration-lifecycle correction permits a fresh registration
after the same authenticated owner explicitly leaves through `21/12`. Gold
does not necessarily delete the previous record before registering another map.
The service retires only released records of that exact connection and principal,
after fully validating the replacement, and allocates a new monotonic identifier.
Live registrations, foreign ownership, malformed replacements and exhausted
identifiers remain rejected without discarding the old record. A released record
cannot regain participation. Transport retries retain their original response.

The actual DXVK client completed two create/host/leave cycles in one process,
with the named human visible in both lobbies. Both registrations were created,
participated, assigned the authenticated host, read, updated and left; normal
disconnect left no records. The client exited normally with all 16 shared files
unchanged. Eleven registration tests cover replacement, ownership, capacity,
exhaustion and old transport replay. This corrects the preceding server error,
without admitting a peer or network match. See the
[reuse receipt](verification/gold-registration-reuse-20261111.json).

## Owned Gold registrations, 2026-11-11

`--account-mode --session-mode` adds an isolated registration laboratory. The
native `21/1` request creates an actual in-memory record, returns its allocated
u32 identifier and binds authority to the authenticated connection object.
Only that connection may remove it or participate as its own host. Replayed
transport packets return the retained reply without repeating the mutation;
another connection, even with the same local principal, cannot claim ownership.
Disconnect and expiry remove owned registrations. Identifiers never wrap or
recycle during the service lifetime; records and one-registration-per-owner
are bounded. This is not peer joining or network game admission.

The codec covers `DynamicGathering`, `S7Session` and `S7SessionMM`. It preserves
the original scalar order, nullable versus empty QStrings, UTF-8 byte bounds,
canonical serialized booleans and raw dynamic attribute tail. Holder length and
the nested OutputStream length must both match exactly. Native `C646D0` fills
that inner length; treating stream finalization as a no-op gives an incorrect
wire shape. The comparison harness now executes the original finalization and
checks 156 authored writer cases, including null/empty/Unicode strings and both
session variants. Borrowed QString lifetime, stream construction/destruction,
arena allocation, reserve, memcpy and Windows text conversion are stubbed;
this is not a full native session or reader test.

The separate DXVK client accepted registration `21/1`, own-host participation
`21/11` and the owner-control RPC `21/40` used by its original host workflow.
Earlier rejection tests cancelled participation and removed the registration.
The final client read the actual stored record with `21/21` and opened its
original Host Match screen. Its own player row remained an open slot; no match
was started. Back cancelled participation, and normal disconnect removed the
remaining record. The client exited with code 0 and all 16 backed-up shared
profile/save files unchanged. This does not establish a populated lobby.
The service has no predefined `S7Multiplayer` groups and returns an empty,
bounded catalogue for that exact group query. It does not publish unqualified
session metadata or fabricate a group to make the query nonempty. See the
[sanitized registration receipt](verification/gold-registration-20261111.json).

Gold's unresolved title fields remain positional; decoded fields do not grant
authority, validate map content, or translate to History identities. The shared
History registry, peer join/endpoint exchange and common simulation transport
are still outstanding. Neither History client has been tested with Gold at this
checkpoint.

The follow-up DXVK test assigns the authenticated local principal to the native
host field (`Gathering+0xc`) only after owned participation and `21/40`.
Gold's original host predicate at `A89360` compares that field with its backend
principal. A client-provided host is cleared at creation; leaving clears the
assigned host. The owned `21/4` update accepts mapped title attributes while
requiring the current base record, connection and participating host role.
The actual client accepted three updates. Read-only native observation found a
session handle, host role and local network slot 1. The player row was still
empty; no network match or History crossplay was observed. Normal exit and all
16 unchanged shared files were verified again.

The original Gold attribute setter independently maps the session's four
strings to `ApplHash`, `DataHash`, `LobbyState`, `Map`; six words to `MaxSlots`,
`OpenSlots`, `AISlots`, `PrivateSlots`, `Version`, `TemplateId`; and four flags
to `Teams`, `FixedPositions`, `TemplateLoaded`, `Coop`. This maps field names,
not content equivalence, identity conversion or wire compatibility with History.

## Original account integration, 2026-11-10

The account mode restores the original `IsLoggedIn` Lua binding's boolean
conversion. The offline unlock inverted the real account backend predicate;
restoring its instruction preserves the native virtual call and its false result
without a backend. All 256 possible native byte results are checked in the
executable ABI fixture, alongside 92 instruction-byte rejection cases.

Gold's title option is an opaque u8, not a canonical boolean. Its native writer
copies the user's byte directly. Preserving all 256 values fixes intermittent
profile startup rejection and timeout. Truncated and trailing requests remain
invalid and cannot mutate the stored option.

Opening Multiplayer also sets the global network-selection predicate while the
menu retains its local OSF message handler. In account mode only, that local
scene may continue after the original account reaches state 6 with its owned
backend and service registration. The launcher/core types, parent ownership,
absent network delegate and original callsite/arguments remain guarded. Actual
network Ready calls remain rejected; this does not admit network simulation.
Native fixtures preserve both original readiness results and reject seven
invalid account contexts plus the network Ready argument.

`native/build_gold.ps1 -AccountLogin` enables `gold_account_login.inc` and the
shared SDK helpers. Unlike the separate SDK probe below, it redirects the
original account manager's automatic login using its own CallContext and backend
output slot. The native account state machine, profile methods, callbacks and
service activation execute unchanged. There are no writes to their success,
account-state or connected flags, no synthetic CallContext completion and no
extra timer/manual account Update pump.

The guarded configuration adapter points OnlineConfigClient at an ephemeral
IPv4 loopback HTTP origin. Gold's native HTTP client fetches and parses the real
JSON response. The prepared EXE has a changed method-selection branch that
passes invalid CURL option zero. The adapter selects the original HTTPGET option
only inside the verified own configuration request, and replaces that request's
legacy Host header with its own loopback authority. Native CURL results and
request/header ownership remain unchanged. The original extension-client
initializer also receives the same independent local transport key, retaining
its native initialization without overwriting the owned stream's checksum seed.

Start `tools/gold/login_server.py` with `--account-mode`, the fresh secret/name
environment described below and private `--ready-file`/`--report-file` paths.
Its readiness record adds `config_port`; pass that as `WEPLAY_GOLD_CONFIG_PORT`
to the native child. This mode currently requires the existing independent
Gold identity `WePlayGoldLocal`. It is one automatic login per process;
reconnect and another identity are not implemented.

`account_config.py` serves only the exact owned configuration route/query,
loopback authority and empty request body. Its single HTTP worker has a one-second
connection timeout and an eight-connection listen backlog. `account_profile.py`
implements the observed native profile read/update, opaque castle/progression
buffer, title option, empty title/option maps and fresh gamer-card schemas.
Profiles live only in memory for this diagnostic launch; progression evaluation,
earned titles, importing existing progress and persistent accounts are not
implemented. This is an explicit **fresh local profile laboratory**, not a
profile migration or a complete replacement online service.

Reliable transport now assembles bounded ordered fragments after packet-local
decryption/decompression, retains a bounded duplicate window, and fragments
large replies. Native ACKs must match both sequence and fragment before they
cancel retries. Assembly has a ten-second deadline; datagrams, logical messages,
fragment counts, profile strings/lists/buffers and pending replies are bounded.
Profile RPCs require registration on the actual authenticated backend connection.
Another profile identity, malformed/trailing data and unsupported RPCs are rejected.

```console
python tests/gold_login_tests.py
python tests/gold_account_tests.py
powershell -File native/build_gold.ps1 -OutputDirectory work/gold-account-build -LocalSimulation -AccountLogin
```

The synthetic account suite covers bidirectional profile fragmentation, an old
fragment retry, altered duplicate data, wrong fragment ACKs, lost ACK recovery,
missing/expired assembly, atomic identity/length rejection, local profile storage
and strict HTTP route/host/body handling. Native fixtures reject 92 altered
instruction bytes and check HTTP calling convention, unchanged failure results
and three failing caller/argument contexts. These checks complement live DXVK
observations; an accessible menu is not multiplayer proof.

The original Session Browser and Create Session map-selection UI have now been
observed under DXVK. Their searches still receive explicit unsupported-RPC
rejections; an empty browser does not establish a working catalogue. Additional
presence, lobby and session services remain unsupported. The normal launcher
does not yet provision these developer endpoints, and no Gold/History match
has been observed.

The connection test reached Online, Session Browser and map selection, then
requested native session creation on a base map. The unimplemented create RPC
was rejected and Gold displayed its original server error. The client closed
normally. A deliberately mismatched launch secret with the same DLL left
account state 0, failed context 3/result `0x80010001`, no backend or enabled
RendezVous, and no server registration/profile access. It also closed normally;
both tests preserved all 16 shared profile/save files. See the
[connection receipt](verification/gold-connection-20261110.json).

The private report includes bounded numeric schema metadata (protocol, method,
argument length and count) for unsupported or malformed profile requests on the
authenticated backend. Each table has at most 32 entries. Fixed parser-stage
counters distinguish packet, payload and RPC rejection. Arguments, correlation
IDs, peer addresses and account identities are never retained in these tables.

The final regular-source DXVK positive test reached native account state 6,
context state 2/result `0x00010001`, a non-null original account backend and
enabled RendezVous. Native OnlineConfig completed HTTP 200. Two profile reads
and updates included 18 accepted incoming fragments and one fragmented reply.
With a deliberately different launch secret, the same native build reached
account state 0/context state 3/result `0x80010001`, with no backend, registration
or enabled RendezVous. Both clients exited normally and all 16 shared
profile/save files stayed unchanged. See the [sanitized account receipt](verification/gold-account-login-20261110.json).

## Native SDK login adapter, 2026-11-10

`native/gold_local_login.inc`, enabled explicitly by the native build's
`-LocalLogin` switch, finds the initialized Gold RendezVous service on its owner thread,
allocates a native CallContext and calls the original eight-argument async
login function. Native QString values contain a UTF-16 pointer; constructors,
destructors, the native job queue and backend output assignment remain in the
SDK. Executable identity, callsite bytes, function entry bytes, object types,
pointer ranges and owner thread are guarded. The local access-key setter is
redirected before native stream initialization; its verified stream-3 checksum
seed is 3. The renderer preflight still requires DXVK.

`tools/gold/login_server.py` is an independent bounded developer service on two
ephemeral IPv4 loopback UDP ports. It implements SYN/CONNECT, reliable DATA ACKs,
bounded response retries, keepalive/disconnect ACKs, packet-local RC4/ZLib
compatibility framing, numeric RMC correlation, TicketGranting LoginEx and
RequestTicket, the owned-ticket backend challenge and SecureConnection Register.
It parses complete bounded argument shapes and rejects wrong credentials,
ticket MACs, principals, sessions, signatures, ordering, lengths and unsupported
RPCs. It neither imports account caches nor impersonates a vendor service.
Compatibility RC4/checksums are protocol requirements, not a new security layer.

The launch environment supplies `WEPLAY_GOLD_LOCAL_PORT`,
`WEPLAY_GOLD_LOCAL_NAME` and a fresh 64-character lowercase hexadecimal
`WEPLAY_GOLD_LOCAL_SECRET`. The server takes the same name/secret; its ready
file supplies the auth port. The native adapter removes the secret from its
environment, dispatches once, then clears its temporary secret buffer. An
optional `WEPLAY_GOLD_LOCAL_STATUS` file contains numeric state/result and
backend presence only. The server reports counters only, never raw packets,
credentials, tickets, keys, challenges or URLs. These are developer launch
inputs; the normal launcher does not yet configure or enable this mode.

```console
python tests/gold_login_tests.py
powershell -File native/build_gold.ps1 -OutputDirectory work/gold-login-build -LocalSimulation -LocalLogin
```

Eleven synthetic tests include 10,000 malformed packets, independently known
cipher/checksum vectors, strict credential shapes, authenticated ticket
challenges, cross-instance replay rejection, duplicate request idempotence,
ordering/session/signature rejection, expiry, disconnect and bounded retry/ACK
behavior. They do not substitute for live game observations.

The private live DXVK test of the repository adapter completed auth handshake,
LoginEx, RequestTicket, backend handshake/challenge and registration. Gold's
own CallContext reached state 2, result `0x00010001`, with its backend output
non-null. It closed normally; all 16 backed-up shared profile/save files were
unchanged. A separate deliberately mismatched launch-secret test checks the
native failure path: state 3, no backend, no registration, normal exit and the
same 16 shared files unchanged. The final response-retry version passed a second
fresh-secret positive test. See the [sanitized login receipt](verification/gold-sdk-login-20261110.json).

The separate `-LocalLogin` SDK probe owns one login interval per process. Its context and backend output
slot stay valid through normal process shutdown; runtime unload, retries/new
accounts, cancellation and reconnect are not supported. It does not attach that
backend to CNetwork's account manager, expose a lobby or translate game commands.
Steam History and Ubisoft History crossplay cannot be qualified from SDK login.

## Independently mapped differences

The fingerprinted Gold 1.12.1396 executable uses the older Quazal transport.
Its packet writer at RVA `0x91d1f0` writes source/destination virtual ports,
type/flags, session, a four-byte packet signature and a two-byte sequence in
the first ten bytes. SYN/CONNECT add a four-byte connection signature; DATA
adds a one-byte fragment index. Flag `0x40` adds a two-byte payload length.
The final transport checksum is **one byte**, unlike the four-byte checksum
in the independently mapped History transport. These checksums do not
authenticate players or replace simulation state checksums.

`GoldPrudpChecksum` reproduces Gold's function at RVA `0x8456e0`: add
little-endian 32-bit words with 32-bit wrapping, fold the resulting four
bytes, then add remaining bytes and the explicit stream seed modulo 256.
A simple sum of every input byte gives different results after word overflow.
The passive C# decoder still requires a caller-supplied seed and assumes no
default. The separate explicit local login profile above verifies seed 3 for
its own access key; that does not establish a vendor server's key or seed.

`GoldPrudpPacketDecoder` accepts only a bounded single packet and returns
metadata and a payload slice. It requires the checksum and exact declared
length where present. Unsupported streams, packet types and the unmapped
`0x80` extension are rejected. Crypto, retransmission, fragmentation assembly,
coalesced packets, RMC, tickets, identity and connection state are not supplied.
There is no fallback from a failed History decode to this Gold codec.

`GoldRmcRequestDecoder` separately reads the older numeric request envelope:
four-byte body length, one protocol byte with its request bit, four-byte call
ID and four-byte method ID, followed by opaque arguments. It rejects responses,
unmapped protocol encodings, truncation, trailing/coalesced messages and inputs
above its local 64-KiB bound. Protocol/method IDs are metadata; decoding one
does not authorize a response. The separate loopback login adapter above owns
its own admission and argument parser. History's named-RPC codec remains separate.

## Verification

The source-only oracle executes the exact original checksum routine in an
emulated x86 CPU with synthetic inputs. It never calls native game code in
the host process. The executable hash is mandatory; execution time,
instruction count and writes are bounded. Stack cleanup, nonvolatile
registers and unchanged inputs are checked. A new output path under `work/`
is required. Install developer dependencies `pefile` and Unicorn 2.1.4 in a
private environment, then run:

```console
python tools/gold/emulate_checksum.py <owned-original-Gold-exe> work/checksum.json
```

All 504 comparisons passed, including empty input, every remainder, word
overflow and a 65,507-byte input. The C# tests contain 126 short synthetic
vectors from that native oracle, plus handcrafted header and rejection cases.
The original packet writer and checksum were additionally emulated together
for 180 empty packets: all five accepted types, six flag combinations, three
seeds and both automatic-length settings. Only the SDK thread-index getter
and CRT memcpy were stubbed. Those output bytes are separate committed
fixtures, reproduced with `tools/gold/emulate_packet_headers.py` and the same
two CLI arguments as the checksum oracle. Nonempty payloads, compression and
crypto are not covered by this writer oracle.

The combined C# tests passed 2,142 checks and 10,000 malformed-input samples. The unchanged
History decoder regression passed 2,871 checks and 10,000 malformed samples;
the main Debug service build passed without warnings. These are emulation,
codec and build results, not live packet or multiplayer observations.

For numeric RMC framing, 80 synthetic request cases passed the original Gold
stream constructor and scalar readers in bounded x86 emulation (RVAs
`0x8648a0`, `0x861990`, `0x82a900`, `0x82a9a0`). Buffer add-reference and CRT
memcpy were stubbed; the full native dispatcher was not executed. Native
dispatcher inspection independently identifies the request bit and field
order. The separate C# framing suite passed 977 checks and 10,000 malformed
samples, including the local length bound and opaque maximum-width IDs.
Neither this check nor the passive request decoder supplies Gold's connection,
login arguments or ticket lifecycle.

## Cross-edition findings and next gate

Read-only archive comparison found 720 of 732 shared resource payloads and
797 of 800 map payloads byte-identical. Two maps contain differing payloads;
those differences must be resolved before sharing a content identity.
Matching native visitor traces for start/turn/checksum events are encouraging,
but do not establish equal command wire bytes or deterministic simulation.
The History storm connectivity facade is absent from Gold, and several
native session/transport vtables have different slot counts. Reusing its
addresses or admitting Gold under the History profile cannot provide crossplay.

The next gate is an independently guarded Gold loopback bootstrap with genuine
local connection callbacks, followed by Gold-to-Gold gameplay. Only then can
Gold/Ubisoft History/Steam History be tested in both host directions for
commands, sustained native checksums, castles, AI, content and save/resume.
See the [sanitized receipt](verification/gold-transport-20261110.json).
