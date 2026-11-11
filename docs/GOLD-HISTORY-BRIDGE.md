# Gold / History native bridge development

Actual Gold ↔ History crossplay is **not implemented**. The new x86 connection
module is a working, independently tested transport component. It has not been
attached to either game's native session or event callbacks, and no mixed
gameplay test has run. The normal game builds and matchmaking admission are
unchanged. DXVK remains the intended renderer for subsequent game tests.

## Implemented connection component

`native/crossplay_channel.cpp` implements a nonblocking, owner-thread TCP channel
to `tools/gold/crossplay_server.py`. Only IPv4 loopback is supported. Two
independent launch keys authenticate one Gold profile and one Ubisoft History
or Steam History profile. Session epochs, ordered sequence numbers, source and
destination ownership, profile metadata and HMAC-SHA256 protect each record.
Connections and session epochs cannot be reused after an authenticated owner
disconnects.

The broker issues explicit session-scoped WePlay identities and reciprocal
namespace projections. A foreign player receives an independent numeric alias
in Gold's view; the reverse view receives a freshly issued WePlay GUID. These
are adapter identities, not vendor accounts or a GUID-to-integer cast. Native
game slot indices are a separate layer and still need native binding.

`State::Connected` means that both channel launches authenticated. It does not
mean a native participant exists, a session joined, a player is Ready, or a
world may advance. The ready file explicitly reports
`native_session_admission: false`.

Payloads are bounded to 64 KiB, each direction to 8 MiB, and receive queues to
4,096 messages. Parsing and socket operations are bounded per owner tick.
Partial/coalesced TCP records are supported. At EOF, complete authenticated
records are drained across subsequent ticks; truncated records are rejected.
No native departure event is manufactured. Callers can inspect pending writes
before choosing to close their own connection.

The executable and content digests in the handshake are expected launch
metadata. This component does not inspect a running game or hash its assets.
Actual image checks and native object/ABI guards must belong to the game
bootstrap before it supplies those values. The socket tests use synthetic
identities and content metadata; they do not prove game ownership, content
compatibility or executable binding.

## Verification

Build with the existing Visual Studio x86 toolchain:

```powershell
powershell -NoProfile -File native/build_crossplay.ps1 -OutputDirectory work/crossplay-build
python tests/crossplay_channel_tests.py work/crossplay-build/crossplay_channel_tests.exe
python tests/crossplay_broker_tests.py
```

The native build uses `/W4 /WX`. Twenty actual C++ ↔ Python TCP scenarios pass:
ordered bidirectional payloads through the 64-KiB boundary, bursts of 1,024 tiny
messages immediately before EOF, and authenticated rejection cases for both
History launch profiles. Twelve broker tests cover ownership, replay, malformed
and fragmented input, truncation, content disagreement and session retirement.
See the [sanitized receipt](verification/gold-history-channel-20261111.json).

The optional broker CLI consumes `WEPLAY_CROSSPLAY_LAUNCHES`, an environment-only
JSON array containing two records with `edition`, `key`, `native_identity` and
`content` (hexadecimal bytes for the last three). Keys must be independent;
the expected content digest must match. It writes a fresh ready file under
`work/` and runs for at most 600 seconds. No game launcher currently consumes
this service, and its test executable is not a game adapter.

## Remaining native integration

The missing bridge must translate an authenticated common session into each
engine's genuine participant creation, assignment, join acknowledgement and
departure callbacks. Gold's NetZ and History's Storm ownership intervals must
be tied to the same common session before native event transport is enabled.

The inspected participant events are insufficient by themselves: their native
constructors and dispatchers also consume local-only flags and additional
participant data which their payload writers do not serialize. Relaying that
payload cannot reproduce the original callback lifecycle. Numeric network slot
IDs also exist in History; its account/Storm GUIDs cannot replace those fields.
The full native mapping and its runtime qualification remain incomplete.

After that mapping, the adapters still need the original native event factory,
receiver ownership and local event queue, a common content admission check,
and actual Gold ↔ Ubisoft History and Gold ↔ Steam History matches under DXVK.
Native Ready results, acknowledgements, checksums and turn progression must
remain consequences of those real operations. A channel test or matching
serialization fixture cannot qualify those outcomes.
