# Gold transport development checkpoint

Gold's opt-in native SDK connection/login adapter now completes a genuine
local login and backend registration under DXVK. The separate loopback service
uses a fresh launch secret and tickets owned by that service. Gold/History
crossplay is not implemented: the CNetwork account/lobby bridge and shared
simulation transport remain incomplete. Gold is excluded from normal
multiplayer admission. The History service has no fallback to this protocol.

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

This adapter owns one login interval per process. Its context and backend output
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
