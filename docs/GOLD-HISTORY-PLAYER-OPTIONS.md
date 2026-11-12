# Gold to History: native lobby Color and Team

The opt-in `crossplay_player_option.h` and `crossplay_player_option_codec.h`
implement host-authored Color and Team notifications for the admitted two-human
Monhaim preparation lobby. Ordinary adapters do not include these modules.
They do not start a world or establish synchronized gameplay.

The actual DXVK High test on 2026-11-12 used Gold and the standalone History
runtime with Ubisoft History data. The private second candidate delivered a
real Gold menu change from Red to Blue and Team I to III and back to I. Read-only
native inspection confirmed the resulting Color and Team on both clients.
The guest's original Ready button also reached both native configuration caches.
Both processes exited normally; settings, adapters and Interface were restored,
and saves, cloud files and shared profiles remained unchanged.

The later third candidate used the public encoder and receiver with their
narrower bounds and preparation ownership checks. Fresh DXVK High pairs passed
the same actual Color/Team changes with both Steam History and Ubisoft History
data, followed by native [castle-position swaps](GOLD-HISTORY-POSITIONS.md) and
guest Ready. Both data sets used the same isolated standalone Ubisoft executable,
without a Steam SDK. The original Steam executable remains unqualified by these
live tests. The Steam-data run recorded memory pressure; this is a bounded lobby
functional result, not sustained gameplay or resource-stability qualification.

## Native behavior

Color uses `CPlayerColorChangedEvent`, class `0x0be2024e`, type `0x00320007`;
Team uses `CPlayerTeamChangedEvent`, class `0xc773eae9`, type `0x001e0007`.
The wire carries the original class, type, source CID, asynchronous flag,
target player slot and value. Color has a four-byte value and 21-byte wire;
Team has a one-byte value and 18-byte wire. These are notifications emitted by
the genuine host, not requests granting a guest authority over another player.

History's original Team consumer updates team affiliation. Its Color consumer
only invalidates the lobby presentation: enqueuing that event alone left the
preparation color unchanged in the first private test. The corrected path also
calls the original Color leaf setter on the owned preparation row. The original
factory, serialization round-trip, asynchronous clone and temporary destruction
remain in the delivery path. No direct write substitutes for the native setter.

The original Color setters are Ubisoft `0x546430` and Steam `0x545a80`. Their
complete 13-byte bodies are checked before calling; the only native field they
write is the four-byte color at player row `+0x2c`. Team is at row `+0x68`.
Original constructor inspection separately established the event subtypes;
default factory constructors alone do not establish a real producer's subtype.

## Bounds and integration

The public payload parser accepts human target slots 1 and 2, Color IDs 2–7,
and Teams I–IV. AI/neutral slots, guest intents, start positions, arbitrary maps,
saved-world state and active-world mutation are excluded. A recognized
authenticated notification outside this scope must retire the diagnostic path.

Use the existing authenticated channel and admitted reciprocal peer namespace.
`CrossplayPlayerOption::Encode/Decode` bind the reciprocal 16-byte peer IDs,
strictly consecutive nonzero revision, exact length, original host CID and event
shape. Decode translates only that authenticated host CID into the receiver's
namespace. Failed parses clear their outputs. The caller owns the revision and
must bound its queue; an envelope is not independently authenticated.

`EncodeGoldPlayerOption` validates the real Gold event, methods and owned lobby.
For each accepted wire, create one `HistoryPlayerOptionReceiver`, call `Deliver`
once and retain it until `Observe` returns `Applied` or `Invalid`. Failure consumes
its enqueue attempt too; never retry/replay that event with a replacement object.
Observation is limited to ten seconds and the original owner thread, session
and preparation buffer. The two native human records must match the admission
identity, assigned slots, state and kind. Color additionally checks the existing
History preparation/backend ownership gate before its original setter call.

`Applied` means the guarded native preparation value matches the notification.
It does not acknowledge a world transition or prove that every unrelated native
queue entry has drained. Changes must invalidate any earlier setup agreement.
The rule/template signature described in
[the signature investigation](GOLD-HISTORY-SETUP-SIGNATURE.md) omits Color and the
complete admitted roster, and cannot replace a fresh complete setup comparison.

## Verification

`native/build_crossplay.ps1 -OutputDirectory <fresh-work-folder>` builds fixed
offline-analysis and relocatable Windows test executables. Run
`crossplay_player_option_runtime_tests.exe`: 682 checks cover payload bounds,
malformed headers, reciprocal IDs, revision/replay, length, source translation,
failure clearing and unbound receiver exclusions. Both builds pass MSVC x86
`/W4 /WX` and contain an embedded `asInvoker` manifest.

`tools/gold/verify_player_option_guards.py` accepts the three owned, pinned
analysis images and the fixed fixture. It executed 148 compiled guard checks,
including changed vtables/methods and both Color setter guards. `VirtualQuery`
is modeled; no factory, native setter or event consumer runs in this guard test.
Separate private offline tests executed the original Color setter bodies six
times and the three option writers/readers eighteen times with modeled scalar
fields/streams. They do not establish constructor, factory or world behavior.

See the [sanitized verification receipt](verification/gold-history-player-options-20261112.json).
The separate castle-position projection now has bounded live lobby evidence.
Local Start capture, common world start, commands/turns, Gold NEO logistics and
mixed save/cold reload remain unfinished.
