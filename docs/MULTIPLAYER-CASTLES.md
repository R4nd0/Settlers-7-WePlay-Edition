# Multiplayer castle restoration candidate

Verified on 2026-10-31 in two isolated clients on one Windows PC. Integrated into
RC3.7's normal native build and matching installer pins. Existing RC3.6 releases
and installed games are not modified in place.

## Behavior

The selected Castle Forge design is supplied to the local player's multiplayer
setup event instead of its empty castle argument. The existing native constructor
still performs the deep copy with the game's allocator. Selecting no castle
retains the empty design.

Restoring only that local event leaves other players' castles empty. The complete
candidate also adds a bounded cosmetic field to the existing Storm join and
player-data messages. The guest supplies its design with the join request; the
host associates it with the sender supplied by the existing transport. Message
copies retain the companion design; new requests clear any record left at reused
message storage. The host distributes each player's design
with that player's setup data. Player zero is valid and is distinct from an unused
cache entry. Receiving a new session's player list does not exclude an identifier
just because it belonged to the local player in a previous session.

The transfer carries asset identifiers, parent indices and attachment slots. It
omits profile names, castle names and process pointers. The original player name,
avatar, colors, team and other message fields retain their native behavior. No
new socket, configuration-service endpoint or Ubisoft SDK call is introduced.

## Guards and bounds

- Original Ubisoft and Steam executable fingerprints and exact patch-site guards
  remain mandatory. Steam has its own address and vtable profile.
- Local profile access requires the startup thread, native object types and
  consistent bounded vectors. Remote designs never come from peer pointers.
- The versioned dictionary format is limited to 2,048 bytes, 512 parts and
  128 bytes per asset identifier. Identifiers exclude paths and traversal;
  parents must precede children, with the native root marker allowed.
- Invalid, truncated or oversized received fields fail the native field result.
  The next-RC candidate uses the same canonical design locally and on peers.
  A design that exceeds the transfer budget becomes empty on both sides.
  Large-design coverage remains limited.
- Companion records are bounded to eight player designs and 32 join-message
  entries. Access across native message threads uses a mutex; the pending
  callback handoff is thread-local and consumed once.
- Original callback arguments, return values, native allocation and Windows
  last-error boundaries are retained. Turn horizons, clocks and desync checks
  are unchanged.

All participating clients must use the same candidate. It changes the native
message format. Retain the launcher's existing content fingerprint checks; do
not mix this DLL with an older installed package or weaken its manifest checks.

## Desync follow-up, 2026-10-31

The latest local game log confirms a LAN simulation desync at turn 461 on
Fiery Chasm, with three active players. The game produced a desync save. The
save and replay are preserved privately. The local report alone does not
identify the first divergent object or establish the other PC's state.

Review found two concrete asymmetries in the released castle implementation:
the local native constructor copied the profile's castle name while peers
received an empty name, and an oversized design remained complete locally
while peers received an empty design. The guarded native constructor copies
the name as well as the type, parts, parents and attachment slots. Whether the
name difference contributes to the simulation checksum is not yet established.

The next-RC candidate constructs the local castle through the same bounded
representation used for received castles. Its borrowed source storage lives
through the original native deep copy. Outgoing join requests also retain
their own captured selection through delayed serialization and message copies;
they no longer fall back to the most recently constructed request's selection.
A missing captured request fails its field result without writing substitute
data. The wire format, numerical modes, turn timing and desync checks remain
unchanged.

The actual native bridge passes 1,360 synthetic bootstrap/ABI checks. Replacing
only the two castle bridge files with their previous versions causes eight
failures in that same suite, covering name equality, request snapshots and
oversized/no-castle parity. These are reproduced data-consistency defects and
a tested correction; they do not prove that turn 461 was caused or fixed by
this change. The earlier visual match observations below describe the previous
candidate. New physical-PC and extended multiplayer verification remains open.

The combined candidate's real Steam process opened a Fiery Chasm lobby with
two AI slots. Bounded passive reads found the selected 121-part design in the
local pre-start configuration. That configuration still retained its profile
name, so the lobby observation does not prove end-to-end canonical equality
after the hooked event. The engine refused match startup without a second
human player; no synchronization check was bypassed. This was a startup/lobby
check, not a new match or a desync-free result.

## Build and verification

For the original-executable development adapter:

```powershell
powershell -NoProfile -File native/build.ps1 -LocalNetworkPump -CastleWirePrototype -MaximumPhysicalCores 2 -OutputDirectory work/native-castles
```

`CastleWirePrototype` enables both local restoration and transfer and now
defaults to true. For the local-only diagnostic variant, explicitly use
`-CastleWirePrototype:$false -CastlePrototype`. Laboratory builds accept only their existing private
test executable identities and loopback slots; never distribute those DLLs.

Observed behavior:

| Host | Guest | Both clients' loaded castle part counts | Visual result |
| --- | --- | --- | --- |
| Ubisoft | Steam | 106, 25 | Each selected castle visible locally and from the other client |
| Steam | Ubisoft | 25, 106 | Each selected castle visible locally and from the other client |

Both directions joined and started Tal von Monhaim. The observations came from
actual running matches and camera changes to the other player's starting sector.
Typed passive reads independently confirmed the two complete setup records on
both clients. Two different saved designs distinguish real per-player transfer
from copying one player's design into every slot.

Final synthetic checks: laboratory profile 1,305 patch/ABI and 717 Steam checks;
original-executable profile 1,284 and 708. Both also pass 48 profile-selection,
160 wire-format, 94 platform, 24 CPU-affinity and 18 connection-state checks.
The session-ID and message-storage reuse corrections have synthetic coverage;
the visual runs used freshly started clients.

This does not qualify three or more players, two physical PCs, Internet rooms,
save/reload, maximum-size edited designs or long-match synchronization. The
previous desync investigation remains open. RC3.7 is a preview with these
qualification limits; normal payload and installer fingerprints are updated
together. No release binary or game data is committed here.
