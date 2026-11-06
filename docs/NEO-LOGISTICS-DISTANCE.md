# NEO-Logistics: distance investigation

Status, 2026-11-06: investigation only. No distance override is installed or
included in a release. The existing NEO option still changes transport ordering
and group priority handling, not source selection or pathfinding.

## Requested policy

Choose requests by explicit individual player/AI intent and then FIFO by request
creation turn, as in the existing group-free NEO policy. For the selected
request, prefer the eligible stock source with the shortest usable road distance.
Distance must not let a younger ordinary request overtake an older one.

Candidates must pass the native ownership/access, goods, reservation and
carrier/warehouse handoff checks. An unreachable nearby source is not a
candidate. Euclidean distance is not road distance. Equal-distance choices must
remain deterministic across multiplayer peers. Existing cargo must not be
cancelled or rebooked solely because a shorter alternative appears.

## Verified native behavior

Static inspection of the pinned Ubisoft executable found that the request offer
selector at 0x7a7b10 already compares native route-search costs. Available-stock
search at 0x7a7020 and a separate reservation-related search at 0x7a74d0 build
candidate lists and use the shared path search at 0x99fc60.

Their edge-cost callback at 0x6f0310 divides the edge distance by a profile speed
and an edge-class multiplier. Their heuristic at 0x99f380 also uses speed.
Consequently, this part of the native selection does not simply choose the first
warehouse, nor does its cost necessarily minimize physical distance. Producer
offers have additional cost terms; those values cannot be mixed with raw distance
without examining their units and semantics.

The search expands the compressed successor at byte 8 of each 16-byte low
resolution adjacency record. Byte 0 is the first detailed neighbor, byte 4 the
street handle, and byte 12 the accumulated segment length. Treating byte 0 as
the compressed successor incorrectly makes reachable destinations appear
unreachable. Detailed nodes can be converted into compressed nodes by native
search operations, so an external reader must not invoke the native search in
a live session.

Changing only the edge callback is insufficient: the heuristic, source-offer
comparison, actual carrier routing and warehouse handoffs must agree with the
chosen objective. The source-selection routines alone do not prove that a
physical shipment can bypass an intermediate warehouse.

## Read-only observation

A guarded external read of the active LAN game captured 2123 street-graph nodes,
1973 streets and 1444 compressed adjacency records within one simulation turn.
The read took approximately 37 ms and requested only process query/read rights.
No native call, injection, game write or UI input was performed.

An offline shortest-path calculation found a graph-only distance of about
18830 native world units between the investigated source and destination stores.
The sum of graph-only shortest distances via the two previously observed
intermediate stores was about 21417 units.

These numbers are unrestricted graph bounds, not a demonstrated transport
improvement. Player access and carrier handoff eligibility were not reconstructed.
The snapshot was taken later than the recorded books shipment; roads and sectors
may have changed. Neither the approximately 2587-unit difference nor the
approximately 12.1% reduction establishes an avoidable detour in that shipment.

## Acceptance before enabling

1. Record eligible source offers, native choices and usable route distances in
   the same simulation state, including the carrier/warehouse handoff graph.
2. Compare group-free FIFO against FIFO plus shortest-distance source selection
   from the same starting state. Measure delivery delay and production as well
   as travelled distance; a shorter route may take longer on slower roads.
3. Verify explicit player/AI individual priorities, unavailable/reserved stock,
   blocked routes, route changes and deterministic ties.
4. Verify save/load and native multiplayer CRC agreement with the same behavior
   on all peers before updating content fingerprints or publishing an adapter.

Game data and raw disassembly remain in ignored private work directories.
