# Gold / History native bridge development

Actual Gold ↔ History crossplay is **not implemented**. The new x86 connection
module is a working, independently tested transport component. It has not been
attached to either game's native session or event callbacks, and no mixed
gameplay test has run. The normal game builds and matchmaking admission are
unchanged. DXVK remains the intended renderer for subsequent game tests.

The November 11 participant component adds original native event construction
and local clone-and-queue delivery for all three executable profiles. This is
still a separate component, excluded from ordinary game adapter builds. Native
session admission and mixed-edition game tests remain incomplete.

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

## Native participant construction and local delivery

`native/crossplay_participant.cpp` constructs original `CPlayerConnectionEvent`
values for Added (2), Confirmed (3), Leaving (4) and Removed (44). Each profile
pins the actual process executable hash/size, constructor instructions, event
vtable, destructor and clone method. Gold's custom ESI/EDX constructor ABI is
separate from History's thiscall ABI. Names are copied by the native allocator;
Confirmed requires a castle already owned by the same engine. Foreign castle
memory is never reinterpreted. Empty callback names and zero native principal
IDs are retained. History connection ID zero is valid; Gold's connection ID
constraints remain distinct.

`ParticipantEvent::EnqueueLocal()` resolves the current native transport from
the pinned main object and checks its class, virtual method, instructions and
queue lock. It invokes the original send method with mode zero. That branch
clones the event and transfers the clone into the native synchronous or
asynchronous queue, without a NetZ/Storm send or immediate session dispatch.
The caller retains its own event until Reset. Duplicate enqueue of the same
constructed event and foreign-thread operations are excluded. This method
does not establish a session or validate foreign participant admission.

The standalone x86 executable passes 131 ABI/policy/transport-guard checks.
`tools/gold/emulate_participant_events.py` executes its compiled thunks with
all three pinned original images: 288 cases cover all four changes, local/host
flags, zero versus nonzero History connections, three name lengths and both
native queues. Native constructor, clone, enqueue and destructor code runs;
heap/lock imports and empty deque storage are modeled. Input immutability,
stack/nonvolatile preservation, independent string ownership and allocation
cleanup are checked. The real queue consumer and session dispatcher do not run.
See the [sanitized receipt](verification/gold-history-participants-20261111.json).

The Gold DXVK runtime probe reached the online menu. Earlier constructor
observers stopped at an input guard; the corrected observer's lobby check is
pending because window activation failed. No corrected runtime constructor or
mixed-session success is claimed. The test remains isolated from the original
installation. Native source integration and game testing must follow the
admission work below.

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
