# Crossplay rooms with variable player counts

The RC4 target is one actual session containing original Gold, Ubisoft History
and Steam History clients, three humans and one AI opponent, with common original
save/reload and continued stable simulation. The native path must work with
any edition as host. The [release gate](RC4-RELOAD-RELEASE-GATE.md) defines
the actual-game/package evidence. Existing two-client pairs remain component
evidence and cannot qualify the new target.

The room transport supports two through eight humans, including repeated
editions and any edition as host. The eight-player transport bound matches
the existing lobby directory. Native map configuration determines which
player positions exist and which are available to humans or assigned to AI.

## Initial shared roster model

`native/crossplay_group_roster.h` binds one room, generation and content digest
to its human participants, using their edition, identity, map slot, display
name and castle digest. The first entry identifies the host. The other entries
are ordered by map slot and can have gaps for AI or unused positions.

The native-view comparison accepts a complete caller-supplied connection map
for each engine's namespace. It requires every original member record,
confirmed status, actual principal/station/name/slot, exactly one local member,
correct host flags and distinct native connections. Zero remains allowed
where the original History namespace uses it. A native caller must still obtain
that snapshot from its original engine under the existing ownership/image/ABI
guards; a synthetic snapshot is not evidence of native admission.

The protocol acknowledgement barrier binds the roster to the authenticated
transport snapshot and requires every member. It tracks each acknowledgement
by its authenticated sender slot. Its context
includes the complete descriptor, purpose, separate challenge and unchanged
10-second lifetime. A duplicate/unknown acknowledgement, changed context,
backward clock or timeout invalidates it. Admission, setup, NEO, world, save and
reload purposes have separate contexts. Challenges must be freshly generated
by the authenticated session owner; the model does not authenticate a sender or
turn protocol acknowledgements into proof of an original consumer result.

`native/build_crossplay_group.ps1` tests all 9,837 ordered edition combinations
across the supported counts, as well as incomplete and mismatched rosters,
wire bounds, purpose/challenge separation and acknowledgement deadlines.

## Group connection service

`tools/installer/crossplay_room_broker.py` and the C++ `Channel::StartRoom`
entry point implement the WPX2 loopback connection. They share bounded socket
handling with the existing WPX1 pair service.

The new handshake binds the original executable pin, common content, room,
generation and the chosen host's transport slot. It sends the complete roster
after every expected client authenticates. The transport slot stays distinct
from edition, assigned player order and native identity namespace. Targeted
and broadcast events carry the authenticated sender's transport slot so even
two participants running the same edition remain distinct. Fanout bounds are checked for
every recipient before any copy is queued. A departing authenticated member
retires the room after already queued events drain.

The integration tests run the C++ client against the Python relay on real
loopback connections. They cover two through eight connections, repeated
editions, hosts from all three editions, directed delivery, broadcast and
malformed membership. They also run the existing WPX1 pair scenarios.

## Native admission and turn routing

`RoomLobbyAdmission` uses the original participant event constructors and
clone queue. It starts from an observed local native lobby and exchanges the
original participant metadata and castle strings over WPX2. It requires
agreement on the same map slots before enqueuing member events. The host is
chosen by its authenticated transport slot, independently of edition.

Per receiver, the group adapter retains the original local CID and gives each
remote its own route. Gold uses the authenticated numeric aliases. History
assigns candidate CIDs from the receiver's numeric space. An assignment becomes
usable only on matching original roster readback and acknowledgements from the
entire room. Before resolving a world route, the adapter checks the original
started session and its complete roster again.

The ordered inbox tracks sequences by sender and event type. It translates typed
source fields through the resolved native route and preserves turn and CRC
values. The group turn codec reuses the original reader and local queue and
checks the started roster before and after decoding.

The loopback integration fixture covers eleven complete admission sequences,
including each of the three hosts, repeated editions and delayed readback from
one client. Its game callbacks are modeled.

The group session-configuration codec reads station IDs from that roster and
translates only the host event's routing ID and each human's station ID. It
keeps AI and vacant records, Ready, privacy and the map data intact. The receive
path checks the authenticated sender is the host, verifies the bound roster
and passes the translated configuration to the original reader and local
queue. The configuration fixture covers three humans with a separate AI
position and each of the human positions as host.

## Original-client lobby build

Use `-CrossplayRoomLobbyLab -PlatformIndependent` with both native build
scripts, and `-LocalSimulation` for Gold. This build accepts an explicit
`WEPLAY_CROSSPLAY_ROOM_V2=1` handoff with the room credentials, transport slot,
host slot, map slots and map capacity. It uses the original executable and
profile checks plus the same kernel isolation check used by the pair tests.
Use it only on independent test copies with the private lobby menus.

The room runtime starts from each client's original local lobby. After native
admission, it reads the host's actual configuration cache, projects the human
station IDs and observes each guest's original configuration consumer.
Each guest acknowledges the matching cache readback. A read-only test query
reports completion after the host verified every guest's acknowledgement and
rechecked its own cache. The host then sends a distinct commit message.

## Original-client startup results

In the first three-edition run, all three original executables passed native
initialization and authenticated to the room. Ubisoft History sent its
original lobby offer to both peers. Steam History then displayed the original
single-instance error. The clients closed with exit codes Gold 0, Ubisoft 0
and Steam 1. The test copies and profiles were restored.

The lobby test build applies a private, per-process mutex name in memory after
checking the original image and the exact edition-specific data slot. Each
original executable remains byte-identical on disk. On the retry, Ubisoft
History and Steam History both reached DXVK 3.1.1 in the same run. Gold
timed out before the native starter reported success. The runner can start
Gold first and start the History clients after Gold passes that handshake.
Neither run completed native group admission or the configuration exchange.

The Gold and History test builds compile and the component fixtures pass.
The complete original-client lobby run remains unverified. Group AI
preparation, Ready, setup agreement, NEO, world start and save/reload remain
open. The installed launch gate continues to require its existing qualification.
