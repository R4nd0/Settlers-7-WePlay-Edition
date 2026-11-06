# NEO-Logistics: distance handoffs

Status, 2026-11-06: a native intermediate-warehouse candidate is implemented
and has completed an isolated multiplayer comparison. One matched books shipment
arrived 66 simulation turns earlier; another arrived within one turn of control.
The correction is included in RC3.18. No distance override is installed in the
already running user session. It does not change the ready-stock source selector or the
detailed carrier movement path. A general productivity gain is not established.

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

## Intermediate-warehouse candidate

The native handoff selector at Ubisoft 0x79ead0 caches its result for a
source/destination pair. On a cache miss it considers finished, same-owner
warehouses with carriers, reachable source/candidate/destination legs, progress
toward the destination and a bounded total native travel cost. After these
checks, its comparator at 0x79ed92 prefers the shortest first leg. This does not
necessarily minimize the sum of both road distances.

The new hook reranks only candidates that have already passed those native
checks. It queries both legs using the native 0x787bb0 search, with uniform
positive movement coefficients so that both the edge cost and the heuristic
measure distance. The original carrier profile is checked first: a zero,
negative or nonfinite coefficient causes native fallback, preserving movement
class restrictions. Native player access checks remain active.

The candidate requires guarded live storehouse handles and the simulation owner
thread. Unsupported endpoints retain the native comparator. Equal totals use
the full warehouse entity handle as a deterministic tie breaker. No request
order, explicit individual priority, FIFO creation turn or existing shipment
reservation is rewritten.

This is narrower than nearest-source selection: it chooses an intermediate
warehouse among the native eligible set. Existing cached handoff choices and
the native detailed movement policy remain relevant to observed routes.

Synthetic tests cover the real x86 bridge's stack, registers, flags, floating
point state and native query calling convention, both executable mappings,
invalid ownership/handles, unavailable routes and movement restrictions. These
tests establish implementation contracts, not in-game delivery improvement or
multiplayer checksum agreement.

## Controlled multiplayer observation

Control and candidate loaded the same preserved user checkpoint on **Das Rennen**,
with two human slots and two hard AI opponents. Both ran from logic turn 67349
to 70349 and exited normally. Each run produced 451 complete common native
checksums, all matching between its two clients. These are loopback tests on one
PC, not separate-PC or Steam runtime qualification. Agreement covers those
recorded checksums, not a checksum for every one of the 3000 additional turns.

The candidate's native trace at turn 67373 records source store 65552 and
destination store 65544. Both proposed intermediate stores passed the native
eligibility gates. The route via store 65539 totals 21416.890 distance units;
the accepted route via store 65606 totals 18829.788, about 12.1% shorter. This
establishes native eligibility in the loaded state, beyond the earlier
unrestricted graph calculation.

In the common observation window 67482..68473, book cargo 6949686 for request
3278393 reached yellow consumer 65554 at turn 67840 in control and 67774 with
the candidate. Control observed an extra store 65544 handoff; the candidate
observed a carrier movement from store 65606 to the consumer. Another matched
book cargo reached the consumer at turns 67519 and 67520, within sampling
resolution. The first difference is a concrete earlier arrival, not a claim
that every delivery improves.

Arrival attribution requires a stable full cargo handle, prior observed request
ownership and physical presence at its consumer. Different cargo handles bound
to a recurring requirement are not joined into one shipment. Disappearing
requests do not count as arrivals. The control reader reached its bounded file
limit early, so comparisons use only the common window. Sampled arrival counts
are not total throughput counters.

The blue weaving mill 65564 at store 65550 retains its four waiting wool requests
in both runs. The user's earlier stop/resume action recovered that mill, but
this distance candidate does not repair that stall. This checkpoint has no blue
production requests bound to store 65569, so it does not demonstrate faster
delivery at that warehouse.

The lobby capability changes to **NEO4**, rejecting the older FIFO-only NEO3
version before play. Saved NEO2 payloads remain readable. The new capability
prevents a new distance client from silently entering a match with an older
transport policy. Installer content fingerprints and release packages have not
been updated in this milestone.

A further run with the final NEO4 capability loaded the same checkpoint,
advanced 500 turns and exited normally on both clients. Its 451 recorded native
checksums match between peers and match the corresponding distance-candidate
sequence. Synthetic lobby tests also reject the old NEO3 join and configuration
fields.

The native build and synthetic checks can be reproduced without game data:

```powershell
rtk proxy powershell -NoProfile -File native/build.ps1 -OutputDirectory work/neo-distance-tests -NeoLogistics -LocalPairLab -ResumeCatchup -MaximumPhysicalCores 2
rtk proxy work/neo-distance-tests/neo_distance_tests.exe
rtk proxy work/neo-distance-tests/neo_distance_policy_tests.exe
rtk proxy work/neo-distance-tests/neo_logistics_tests.exe
```

The preserved game checkpoint, raw observer files and executable analysis remain
private and are not distributed with these sources.

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

## Remaining qualification

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
