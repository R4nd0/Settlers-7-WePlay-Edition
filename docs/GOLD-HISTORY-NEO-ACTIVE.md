# Experimental active Gold/History FIFO, 2026-11-15

The isolated native candidate now changes real Gold and History logistics
ordering. It uses a shared FIFO policy, records original individual-priority
actions and transports the original priority command in both directions.
Ordinary release adapters do not activate this candidate.

This is the FIFO/individual-priority port, with feature mask 19. History's NEO5
warehouse-distance and pickup corrections are not yet integrated into the Gold
candidate. It must not identify itself as complete NEO5.

## Native agreement and commands

`native/crossplay_neo_agreement.h` binds both admitted peer identities, the
enabled choice, feature mask, content, complete setup and saved-state digest.
Both offers and confirmations are required. The agreement supplies no native
world-start authority or readiness acknowledgement. Old or differently
configured peers are rejected.

`native/crossplay_priority_command.h` carries the original nested task-priority
event. Full task-generation handles, relative task, player, source slot and
synchronization flag remain intact. Only the independently admitted outer
connection ID is projected. Delivery shares the original turn/build/road FIFO;
the original native factory, reader/writer round trip and local clone sender
apply the event. Transport does not edit the intent ledger directly.

The compiled protocol fixtures pass 805 agreement checks and 157 command checks.
Additional private native checks cover all three original writers and 69
subclass guard mutations. Original image, method, object, thread, owner and ABI
guards remain required; protocol fixtures alone do not establish gameplay.

## Observed running worlds and real files

Two humans and two native AI players used Monhaim Valley, the original standalone
executables, DXVK and High 1920×1080 graphics. The original game checksums were
compared without changing clocks, RNG, stock or checksum values.

| Pair | Common native checksums | Mismatches | Genuine save files |
| --- | ---: | ---: | --- |
| Gold / Steam History, case 238 | 300, finished turns 0–299 | 0 | Both at LogicTurn 300 |
| Gold / Ubisoft History, case 239 | 2,111, finished turns 0–2110 | 0 | Both at LogicTurn 300 |

Each pair wrote through the original `game.save` and pickle lifecycle into an
unused diagnostic slot. Both native files contain byte-identical serialized
NEO2 metadata with two live individual-priority marks. The world files are each
edition's own original output; file-byte equality of the native world is not
claimed. Saving was slow enough to require a longer bounded test window.

In the Ubisoft pair, a read-only Gold observer saw a human construction site's
required goods arrive and its original construction progress reach 100%. The
first History observer did not report the corresponding sites. That observer
has been corrected to exclude nonbuilding task rows before constructing an
entity key, but symmetric observation and completion still require a new run.

Both games exited normally in the two tabled cases. Strict checks confirmed
restoration of profiles, archives, adapters and shared state, no replacement of
preexisting saves and no remaining test game processes.

## Remaining qualification

These files have **not** yet been loaded into a new shared Gold/History session.
Native resume admission, synchronized continuation after loading, symmetric
delivery/completion observation, full NEO5 and EasyTier/separate-PC tests remain
open. The requested x20 duration was accepted by the original API, but measured
progress did not establish effective x20 simulation speed.

A Gold host-checkbox candidate and a ReShade runtime without GUI are being
verified separately. Their source or successful compilation is not a gameplay
or graphics acceptance result.
