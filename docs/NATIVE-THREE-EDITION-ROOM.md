# One simultaneous three-edition room

The RC4 target is one actual session containing original Gold, Ubisoft History
and Steam History clients, three humans and one AI opponent, with common original
save/reload and continued stable simulation. Gold-host and Steam-host ownership
are required separately. The [release gate](RC4-RELOAD-RELEASE-GATE.md) defines
the actual-game/package evidence. Existing two-client pairs remain component
evidence and cannot qualify the new target.

## Existing boundaries

The mixed broker currently accepts exactly two launches and the Gold/History
edition pairs. `Channel` has one remote peer. `LobbyAdmission` binds exactly two
native human members; session configuration and Ready snapshots have host/guest
fields. Those checks remain unchanged. Adding a third socket or accepting a
larger count would not establish one shared native session.

The existing History session registry already tracks independently registered
pending/confirmed clients and map capacity. Its ownership/reservation policy
remains useful. It does not supply Gold/NetZ to History/Storm native admission,
common connection namespaces, original three-member consumption or mixed turns.

## Initial shared roster model

`native/crossplay_group_roster.h` defines a separate three-member descriptor.
It binds one fresh room, one nonzero generation and content digest, all three
editions exactly once, logical slots1/2/3, distinct peer identities, display
names and castle digests. The game host is slot1 and must be Gold or Steam.
Native catalogue references are separate from transport/native participant IDs.

The native-view comparison accepts a complete caller-supplied connection map
for each engine's namespace. It requires all three original member records,
confirmed status, actual principal/station/name/slot, exactly one local member,
correct host flags and three distinct native connections. Zero remains allowed
where the original History namespace uses it. A native caller must still obtain
that snapshot from its original engine under the existing ownership/image/ABI
guards; a synthetic snapshot is not evidence of native admission.

The protocol acknowledgement barrier requires all three members. Its context
includes the complete descriptor, purpose, separate challenge and unchanged
10-second lifetime. A duplicate/unknown acknowledgement, changed context,
backward clock or timeout invalidates it. Admission, setup, NEO, world, save and
reload purposes have separate contexts. Challenges must be freshly generated
by the authenticated session owner; the model does not authenticate a sender or
turn protocol acknowledgements into proof of an original consumer result.

`native/build_crossplay_group.ps1` runs910 authored checks for both host roles,
all three native views, incomplete/extra/mismatched native rosters, wire bounds,
three acknowledgements, purpose/challenge separation and deadlines. No original
game or native membership is created by those fixtures.

## Separate three-client connection service

`tools/installer/crossplay_room_broker.py` now provides an explicit WPX2
loopback-only service for exactly one launch of every original edition. It
reuses the bounded nonblocking socket processing and drain-before-EOF behavior
of the existing pair service. The WPX1 pair handshake and two-member checks
remain unchanged.

The new handshake binds the original executable pin, common content, room,
generation and chosen Gold/Steam game host. It sends one complete three-member
snapshot only after all three independent clients authenticate. Logical game
slots are separate from transport slots and each engine's native identity
namespace. Targeted events and broadcast to both other members retain their
authenticated origin; source echo is excluded. Fanout bounds are checked for
every recipient before any copy is queued. A departing authenticated member
retires the room after already queued events drain.

Fifteen actual three-socket loopback tests cover both game hosts, complete
membership, namespace projections, targeted/broadcast delivery, queued EOF,
replay, tampering, source/epoch mismatch and retired-room rejection. Fourteen
existing pair-service regression tests also pass. These use authored clients;
no original game is started and the current native pair channel rejects WPX2.

## Integration still required

The descriptor/barrier model and three-client connection service still require
their native client channel and integration with an original three-member join
transaction, original three-client Ready/start, NEO agreement, ordered
commands/EOT and CRC collection, original save transfer or coordinated reload.
Each stage needs actual observations from every member and fresh native checks
before original calls. The canonical profile adapter under development must
resolve actual host choices in each engine's existing catalogue; a network
principal is not a `ProfileName` reference.

No ordinary parser, transport, native entry, content catalog, installer or
release-package gate is opened by this model. Three-client qualification and
reverse-world qualification remain open.
