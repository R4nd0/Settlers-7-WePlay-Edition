# Gold / History native bridge development

Playable Gold ↔ History crossplay is **not implemented**. Private DXVK adapters
now connect Gold and Ubisoft History to the authenticated channel and deliver
foreign participant events through their original native queues. Both session
managers have accepted matching two-player rosters and host/guest roles. The
preparation defaults now show both participants in both windows. The latest
private configuration path also selects History's original guest page and
delivers Gold's native map value. Complete guest initialization, Ready exchange
and mixed gameplay remain incomplete. The guarded public native codec has also delivered repeated
lobby-chat messages in each direction between Gold and Ubisoft History. Normal game builds and
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

## Automated development tests

The opt-in `tools/gold/native_lobby_driver.lua` is an **unqualified development
experiment**. It waits for the actual login,
enters the original session browser, waits for native network startup, selects
one unambiguous Monhaim map and calls the edition's original `SessionCreate`
API. Gold keeps its seven arguments; History keeps its eight arguments.
Only a real boolean success selects the original host-lobby action. A latch
prevents a native job's callbacks from creating a second session.

The driver runs after the original module manager updates, waits for the
browser's real `isCreated()` result and resolves Monhaim through the engine's
map metadata. Its map query retains the original single release argument.
Lua job errors stop creation while retaining the normal shutdown deadline.
The driver requests normal engine shutdown after 90 seconds of observation
following creation, or after its initial waiting deadline if creation never
occurs. An engine stall can prevent the Lua update and shutdown from running.
It never supplies Ready, start, acknowledgement, turn or checksum results.
It is excluded from ordinary interfaces and launchers.

Real-engine follow-ups reached the browser, but direct map loading/session
creation inside the main-state update stalled at `creating` in both clients.
The original GUI creation sequence subsequently worked. Do not use this driver
as an unattended or headless test runner; its inline native-job lifecycle still
needs correction. Renderer-free protocol, ABI and memory-layout checks run separately.
The private pair launcher verifies executable, adapter, extension source and
archive hashes, backs up the actual profiles and restores the test archives.
Memory preflight precedes archive staging and retains the laboratory's 4-GiB
threshold. Successful configuration tests below used the original GUI sequence.

`tools/gold/history_guest_lobby.lua` also adds an experimental guest-page
transition. It requires the actual guest role and local slot 2 plus a strict
boolean from the guarded native query. `native/crossplay_history_preparation.h`
checks confirmed ownership, matching native preparation IDs, slots, names and
human states, inactive-buffer roots and foreign backend host ownership for
each independently pinned History layout. This presentation gate does not
establish a Storm join, content agreement or permission to start a world.
The transition has now been observed with both standalone History data sources.
It is a presentation transition after native queueing, not a completed join.
The private History build
must select the existing `GameDirectory` console bridge for that query;
the earlier laboratory build returned no Lua value because it omitted it.

Thirteen Lua 5.1 driver tests, five guest-page tests and 598 synthetic read-only
preparation checks pass. These exercise reentry, native failure, edition
argument lists, readiness gates, ownership changes and unreadable memory;
they do not execute a mixed game. See the
[automation receipt](verification/gold-lobby-automation-20261111.json).

## Native session configuration candidate

The experimental native path now supports a bounded `CSessionConfigEvent`,
in addition to participants and chat. It remains excluded from ordinary
adapters. The private Gold host can capture its actual configuration under
the original configuration lock and invoke the original Gold constructor.
The constructor owns independent map strings and slot storage; its original
destructor releases them after serialization. This provides an initial
configuration source without claiming that a vendor backend joined a peer.

The authenticated frame carries the original map, empty save path and eight
native network-slot records. The current policy admits two human slots,
optional original AI slots in positions 3–4, and native unused/open/closed
states. It preserves actual Ready, privacy, intermediate state and slot values.
Resume configurations and unsupported shapes are rejected before the reader.

Both original `GetSlotByStation` implementations compare Station with a numeric
routing CID. Station stays separate from principals and Storm GUIDs. For the
two admitted humans, the source snapshot acquires the authenticated roster's
receiver-local CIDs. Reception translates those two Station fields and the
base sender CID into the target namespace. No Ready value, native object
handle, join result or acknowledgement is supplied by that translation.

The receiver checks the current guest roster, event class, complete payload,
map path and scoped identities before using its original factory reader. It
requires exact reserialization, then asks the original mode-zero transport
to clone the event into its local queue and destroys only the factory-owned
original. The private runtime handles configuration and chat through one
receive loop. The experimental guest-page query additionally waits for a
configuration to have been enqueued; this is not a consumer acknowledgement.

Offline verification passes 7,141 framing/projection checks, 80 native ABI and
source-bound checks, and 87 compiled integrity-guard checks across all three
profiles. Original per-event writer/reader visitors agree in emulation.
Two Gold constructor/clone/destructor cases also execute original code through
the compiled ABI thunk, retain their borrowed inputs and release their modeled
allocations. These tests do not execute a real queue consumer or establish a
mixed lobby. See the [configuration receipt](verification/gold-session-config-20261111.json).

Two manual real-engine pairs now pass initial snapshot capture, authenticated
delivery, original factory/clone enqueue and the original History guest-page
transition. One uses Steam History data, the other Ubisoft History data; both
use the separately guarded Ubisoft runtime without a loaded Steam SDK. Both
clients load DXVK at High 1920×1080 and close normally, with profiles, options,
adapters and archives restored and saves/cloud/shared files unchanged.

The second pair deliberately created History on Tower Island while Gold used
Monhaim. Read-only inspection subsequently found Monhaim in History's native
configuration cache: a real consumer effect, beyond queue insertion. Its map
preview still showed Tower Island, and the native host Ready cache differed.
The original Ready button emitted `CToggleReadyStateEvent` (29), which the
current cross-edition path does not relay. Complete guest map initialization,
Ready exchange, session intake, content agreement, world start and synchronized
turns remain unqualified. No mixed world has started.

## Verification

Build with the existing Visual Studio x86 toolchain:

```powershell
powershell -NoProfile -File native/build_crossplay.ps1 -OutputDirectory work/crossplay-build
python tests/crossplay_channel_tests.py work/crossplay-build/crossplay_channel_tests.exe
python tests/crossplay_broker_tests.py
work/crossplay-build/crossplay_participant_runtime_tests.exe
work/crossplay-build/crossplay_admission_tests.exe
work/crossplay-build/crossplay_chat_tests.exe
work/crossplay-build/crossplay_event_codec_runtime_tests.exe
work/crossplay-build/crossplay_preparation_tests.exe
work/crossplay-build/crossplay_session_config_tests.exe
python tests/gold_guest_lobby_tests.py
python tests/gold_lobby_driver_tests.py
```

The two Lua tests use Lupa's Lua 5.1 runtime. An existing private installation
under `work/lua-test-runtime` is supported; otherwise install Lupa in the
test Python environment. No game archives are required for these unit tests.
The fixed-base participant and codec executables remain separate emulator
fixtures; the `_runtime_tests.exe` variants use ASLR for Windows execution.

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

Admission protocol version 2 retains each source's actual numeric principal as
well as its source CID. A zero principal stays zero, allowing the original
preparation setter's assigned-slot fallback. The two effective preparation
player IDs must be distinct and non-sentinel before any foreign native event
is constructed. These native metadata values do not authenticate vendor
accounts. Version 1 is rejected explicitly; it substituted the common slot for
the foreign principal and could produce different frontend IDs across engines.

The framing/ownership executable passes 1,050 checks. Once bound, it leaves later
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

Twenty-eight synthetic x86 checks cover those custom call ABIs, stack/register
preservation, memory ranges and unbound/foreign-thread exclusions. The compiled
table guard also passes 42 offline checks against all three fingerprinted
analysis images, including changed table entries, changed method prefixes and
unreadable methods. That test executes our compiled guard and models
`VirtualQuery`; no original game code, game process or network runs. It needs
the existing `pefile` and Unicorn dependencies:

```powershell
python tools/gold/verify_codec_guards.py --gold <gold-analysis.exe> --ubisoft <ubisoft-original.exe> --steam-analysis <steam-unpacked-analysis.exe> --thunk-executable work/crossplay-build/crossplay_event_codec_tests.exe work/codec-guard-receipt.json
```

The fixed-base executable is an offline analysis fixture. Run the separately
built `crossplay_event_codec_runtime_tests.exe` for Windows checks; it allows
relocation when a system DLL occupies the analysis fixture's preferred base.

In the actual private Gold ↔ Ubisoft History DXVK High run, both original native
factories first completed same-engine Confirmed-event write/read round trips.
Then seven authenticated admission records bound the rosters. An actual UI chat
message from each game passed through the channel, the opposite engine's native
factory and its original queue consumer. Both game windows displayed each
message once, with no duplicate local echo. No world was started. Both clients
exited normally; profiles/adapters were restored and saves, cloud and all 16
shared files checked by each runner were unchanged. One broker rejection
occurred during normal peer shutdown, after the nine valid records.

After available host memory recovered, the current public codec and its
roster/table guards passed a further actual DXVK run. Two UI messages from each
engine appeared once in both windows: eleven authenticated records, no
pre-shutdown rejection, and one retirement rejection during normal shutdown.
Both clients exited with code zero and the same profile/save/shared-file checks
passed. The private History preparation candidate also claimed its own second
slot through the original open-to-connecting and connecting-to-human methods,
copied its name through the original setter, and queued its own Confirmed event.
Both participants then appeared with portraits, colors, teams and positions in
both windows. This establishes default row population only; the host page on
History, general setup synchronization and frontend player-ID agreement remain
unqualified. Steam has no mixed chat runtime result. This does not qualify native Ready, setup,
gameplay events, content compatibility or either mixed-edition match. See the
[sanitized event receipt](verification/gold-history-events-20261111.json).

A subsequent protocol-v2 DXVK run also completed the seven-record admission
transaction. Read-only inspection of both owned processes verified identical
frontend player IDs, names' lengths, assigned slots and human slot states in
the two preparation rows. The same inspection confirmed a separate lifecycle
mismatch: History's legacy backend still considers itself host while its native
member list is guest. Both clients exited normally with profiles and adapters
restored and saves/cloud/shared files unchanged. This verifies principal
retention, not general setup or a completed guest join.

## Remaining native integration

The remaining bridge must complete native slot/preparation assignment, join
acknowledgement and departure handling. Gold's native Added handler obtains a
remote slot from `CSessionConfig` using StationID; a preassigned field in a
remote event does not bypass that lookup. The real preparation state must
agree on both engines before world event transport is enabled.

`native/crossplay_history_role.h` adds an excluded, one-shot History guest
backend projection for an already authenticated, exact two-member roster.
Ubisoft and native Steam images have independent address and method guards.
It checks the fingerprinted process, owner thread, native guest/host members,
backend session class and local CID before calling the original host-CID setter
with the admitted foreign CID. The local CID and backend principals stay
unchanged. The original role getter must then report guest. All eight methods
in the getter/setter call path are guarded; the target field must be writable.
No Storm connection handle, Connected flag or acknowledgement is constructed.

The private DXVK probe executed this original assignment successfully. Read-only
inspection confirmed the backend changed from host to guest while both
preparation player IDs and slots still matched. An actual chat message in each
direction remained visible once in both windows. Nine records completed without
a pre-shutdown rejection; both clients closed normally with restored profiles
and unchanged saves/cloud/shared files. The expanded inner-method guards have
52 compiled offline mutation/range checks (26 per History image), alongside
the 28 x86 ABI/range checks above. The Ubisoft path also passed the later
standalone-data runtime below. Native Steam coverage remains offline only.
This is host projection, not the complete native guest join lifecycle.
UI transition, session replacement/departure and native Steam runtime coverage
still need integration before this helper can enter ordinary builds.

```powershell
python tools/gold/verify_history_role_guards.py --ubisoft <ubisoft-original.exe> --steam-analysis <steam-unpacked-analysis.exe> --thunk-executable work/crossplay-build/crossplay_event_codec_tests.exe work/history-role-guard-receipt.json
```

## Client-free Steam-data standalone runtime

The standalone target does not require the Steam client. Its existing private
Steam-data conversion uses the owned Ubisoft startup image after checking the
shared original assets; see [standalone integration](PRIVATE-STANDALONE.md).
Data source and executable profile are separate: this path uses the Ubisoft
native ABI, and cannot qualify the original Steam executable's ABI at runtime.

A fresh separate copy of that client-free Steam-data standalone passed the
Gold pair test under DXVK High at 1920x1080. Both original native rosters bound;
read-only inspection verified matching preparation IDs, slots and human states,
and History's original backend recognized the foreign Gold host. One actual
UI chat message in each direction appeared once in both windows. Neither game
loaded `steam_api.dll` or `steam_api64.dll`; both loaded their test-copy D3D9
module. The Steam client was not closed or modified for this test, so it is not
a new client-absent environment test.

Nine authenticated records completed with no pre-shutdown rejection and one
normal retirement rejection. Both clients exited normally; profiles, adapters
and options were restored, and saves, cloud and shared files remained unchanged.
No world was started. The original Steam-executable attempt had exited before
its menu and established no mixed runtime result. Neither result establishes
native Ready, a complete guest UI/join lifecycle, compatible gameplay content
or playable crossplay. See the
[standalone-data receipt](verification/gold-history-standalone-20261111.json).

## Remaining gameplay bridge

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
