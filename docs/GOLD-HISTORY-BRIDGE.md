# Gold / History native bridge development

Playable Gold ↔ History crossplay is **not implemented**. Private DXVK adapters
now connect Gold and Ubisoft History to the authenticated channel and deliver
foreign participant events through their original native queues. Both session
managers have accepted matching two-player rosters and host/guest roles. The
preparation frame and visible lobby still differ; no mixed gameplay test has
run. A private native codec has also delivered an actual lobby-chat message in
each direction between Gold and Ubisoft History. Normal game builds and
matchmaking admission are unchanged.

The November 11 components add original native event construction, local
clone-and-queue delivery, copied castle designs and a bounded roster-binding
protocol. They remain excluded from ordinary game adapter builds. Complete
native admission and mixed-edition gameplay tests remain incomplete.

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
`native_session_admission: false`. The separate roster-binding experiment does
not change that transport-only service contract.

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
work/crossplay-build/crossplay_admission_tests.exe
work/crossplay-build/crossplay_chat_tests.exe
work/crossplay-build/crossplay_event_codec_tests.exe
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
`work/` and runs for at most 600 seconds. Ordinary game launchers do not consume
this service; private laboratory adapters use it for the bounded roster test.

## Native participant construction and local delivery

`native/crossplay_participant.cpp` constructs original `CPlayerConnectionEvent`
values for Added (2), Confirmed (3), Leaving (4) and Removed (44). Each profile
pins the actual process executable hash/size, constructor instructions, event
vtable, destructor and clone method. Gold's custom ESI/EDX constructor ABI is
separate from History's thiscall ABI. Names are copied by the native allocator;
Confirmed can use a castle already owned by the same engine, or a bounded
canonical design copied into the target engine's own representation. Foreign
castle memory is never reinterpreted. Empty callback names and zero native principal
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

The standalone x86 executable passes 177 ABI/policy/transport/lobby/castle checks.
`tools/gold/emulate_participant_events.py` executes its compiled thunks with
all three pinned original images: 312 cases cover all four changes, local/host
flags, zero versus nonzero History connections, three name lengths, empty and
three-part castles, and both native queues. Native constructor, clone, enqueue and destructor code runs;
heap/lock imports and empty deque storage are modeled. Input immutability,
stack/nonvolatile preservation, independent string ownership and allocation
cleanup are checked. The real queue consumer and session dispatcher do not run.
See the [sanitized receipt](verification/gold-history-participants-20261111.json).

The corrected Gold DXVK runtime probe completed original Added and Confirmed
construction/destruction in a real Monhaim lobby and exited normally. The
subsequent mixed roster test also ran actual queue consumers and participant
dispatchers. Tests remain isolated from the original installation.

## Authenticated native roster binding

`native/crossplay_admission.cpp` binds one authenticated Gold/History channel
to two actual single-member native lobbies. It captures native source CIDs,
names and castles on the owner thread. Bounded Hello/Assign/Applied/Commit
records carry reciprocal session identities and common host ownership. A
foreign participant receives a receiver-local numeric CID, independently of
its source CID or Storm GUID. Backend object handles remain local; the bridge
does not invent a NetZ handle.

Original Added and Confirmed events enter the native queue. The protocol waits
for the original consumers to produce the exact two-member roster, assigned
slots and roles in the same session object. Both peers must observe those
postconditions before `AdmissionState::RosterBound`. This state establishes
member lists only: it does not qualify a shared preparation frame, visible
lobby, Ready result, content agreement or simulation.

The framing/ownership executable passes 946 checks. Once bound, it leaves later
channel records for the event dispatcher and continues checking the exact native
session, member statuses, IDs, names, slots and roles. A departed, replaced,
additional or reassigned member invalidates the interval. In the private Gold ↔
Ubisoft History DXVK run, seven authenticated records completed roster binding
on both sides. Gold remained host at slot 1; History became guest at slot 2.
The UI exposed the remaining mismatch: Gold still showed an open second slot,
and History's first preparation row changed to the Gold player while its own
second row stayed open. No world was started. Both clients exited normally;
adapters and options were restored, and saves and shared files were unchanged.
Steam History has constructor/ABI coverage but no mixed runtime result yet.

A subsequent private DXVK run bound Gold's original `CSessionConfig` slot
query to the authenticated foreign StationID. The guarded callback returned the
common slot only for the owned Gold session and original Added callsite, and
forwarded other calls. Gold then displayed the History player in its second
row with the native portrait, color and position. History still lacked its
second preparation row. Both clients again exited normally with restored
profiles and unchanged saves. This private slot callback is not part of the
public roster component or normal adapter builds.

## Native lobby-chat event path

`native/crossplay_chat.h` admits only the full `CChatEvent` wire with category 7,
subtype 20 and an asynchronous flag. Both native strings, their terminators,
lengths and the trailing fields are checked before any native factory call.
This initial gate supports printable ASCII strings up to 1,024 characters;
other characters and event classes are excluded. A separate envelope binds the
authenticated sender and receiver. Only the sender's admitted source CID is
accepted and translated to its receiver-local CID. Channel sequence and epoch
checks still apply. The framing executable passes 503 checks.

`native/crossplay_event_codec.h` uses the original binary writer, registered
factory and reader. A fingerprinted owner-thread participant binding and the
current two-member native roster are required. The codec checks stream and
visitor layouts, all 84 stream/writer/reader virtual slots and method prefixes,
the six chat methods, the transport method and queue lock. Gold's EAX stream
constructor and ESI/EDI visitor calls are distinct from History's thiscall ABI.
After decoding, complete consumption and identical reserialization are required.
Original mode-zero delivery makes a receiver-owned clone; the codec destroys
only its own factory allocation. Ordinary adapters do not include this header.

Twenty-four synthetic x86 checks cover those custom call ABIs, stack/register
preservation, memory ranges and unbound/foreign-thread exclusions. The compiled
table guard also passes 42 offline checks against all three fingerprinted
analysis images, including changed table entries, changed method prefixes and
unreadable methods. That test executes our compiled guard and models
`VirtualQuery`; no original game code, game process or network runs. It needs
the existing `pefile` and Unicorn dependencies:

```powershell
python tools/gold/verify_codec_guards.py --gold <gold-analysis.exe> --ubisoft <ubisoft-original.exe> --steam-analysis <steam-unpacked-analysis.exe> --thunk-executable work/crossplay-build/crossplay_event_codec_tests.exe work/codec-guard-receipt.json
```

In the actual private Gold ↔ Ubisoft History DXVK High run, both original native
factories first completed same-engine Confirmed-event write/read round trips.
Then seven authenticated admission records bound the rosters. An actual UI chat
message from each game passed through the channel, the opposite engine's native
factory and its original queue consumer. Both game windows displayed each
message once, with no duplicate local echo. No world was started. Both clients
exited normally; profiles/adapters were restored and saves, cloud and all 16
shared files checked by each runner were unchanged. One broker rejection
occurred during normal peer shutdown, after the nine valid records.

The runtime result belongs to the private predecessor of the public codec.
The added public roster/table guards have build, ABI and offline coverage;
their runtime requalification and the next History preparation-slot candidate
were blocked before launch by the existing 4-GiB host-memory preflight. Steam
has no mixed chat runtime result. This does not qualify native Ready, setup,
gameplay events, content compatibility or either mixed-edition match. See the
[sanitized event receipt](verification/gold-history-events-20261111.json).

## Remaining native integration

The remaining bridge must complete native slot/preparation assignment, join
acknowledgement and departure handling. Gold's native Added handler obtains a
remote slot from `CSessionConfig` using StationID; a preassigned field in a
remote event does not bypass that lookup. The real preparation state must
agree on both engines before world event transport is enabled.

The inspected participant events are insufficient by themselves: their native
constructors and dispatchers also consume local-only flags and additional
participant data which their payload writers do not serialize. Relaying that
payload cannot reproduce the original callback lifecycle. Numeric network slot
IDs also exist in History; its account/Storm GUIDs cannot replace those fields.
The full native mapping and its runtime qualification remain incomplete.

After that mapping, the adapters still need bounded native event-factory and
queue integration for setup and world events, a common content admission check,
and actual Gold ↔ Ubisoft History and Gold ↔ Steam History matches under DXVK.
Native Ready results, acknowledgements, checksums and turn progression must
remain consequences of those real operations. A channel test or matching
serialization fixture cannot qualify those outcomes.
