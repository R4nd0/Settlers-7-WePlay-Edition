# History multiplayer crash report, 2026-11-17

## Shared three-client reloads, 2026-11-18

Both supplied saves loaded into one live session with three Ubisoft History
laboratory clients and the original AI. The host confirmed both guest
admissions. All three clients restored the saved NEO state and advanced the
game. Each process bound the native UDP ports on its own loopback address.
The laboratory variants differ from the original executable only in the
fingerprinted instance and profile isolation fields.

| Save | Time after the monitored start | Native replay evidence | Observed outcome |
| --- | --- | --- | --- |
| Manual, saved turn 18,103 | 20:00 | 506 common checksums, all equal | Three clients continued and closed normally |
| Autosave, saved turn 27,001 | 6:46 to the departure sample | First 3,193 equal, then two differences on the first guest | The first guest returned to the browser, and the host reported a player departure |

The last common live autosave sample was at native replay turn 30,197. The
host and second guest retained matching checksums across all 3,505 records
they shared, including after the first guest departed. The host wrote a new
autosave at logic turn 30,002. That gives an earlier checkpoint for the next
investigation. The test establishes the order of the save and divergence.
The cause of the divergence remains open.

All three native desync saves record logic turn 30,195. Between the last
matching checksum and the first differing checksum, the replays contain no
additional serialized event. The host and second guest have identical sets
of 4,398 coordinate records in the AI `Storehouse` collection. The first guest's
collection is empty. Other world fields also differ. This locates a difference
in the snapshots taken at detection. It does not establish where the first
simulation difference started.

The newly written host checkpoint at turn 30,002 loaded on all three clients
and all three restored NEO. The host displayed a victory at 50:00 of game
time and a departure notice. Both guests stalled at replay turn 30,004 while
the host advanced. The three replays shared only one checksum, and all three
clients exited normally with verified restoration. Use the supplied turn-27,001
save to compare coverage through the original failure.

The original autosave reproduced the same failure on a second cold start.
All three desync snapshots again identify turn 30,195, the first guest
diverged at replay record 3,193, and the same AI coordinate collection was
empty only on that guest. The native random state read on all three clients
at replay turn 30,196 was equal. The two runs produced identical per-client
checksums through the two failing records.

A private control loaded the same native world bytes with NEO Logistics
disabled in the save cache and the matching lobby fixture. All three clients
reported NEO off and continued together for 20:39, through replay turn 34,758.
All 7,893 common replay checksums matched, and the copies and profiles were
verified restored after normal exit. The NEO-on and NEO-off runs already have
different replay checksums at record 23. This control verifies a different
simulation path through the failure window. The offending operation in the
NEO-on path remains open.

The native checksum trace reproduced the same failure. All 42 directly
recorded inputs agree across the three clients. On turn 30,195, the first
accumulator difference follows the original addition of the per-player AI
checksums. Only the AI checksum for player ID 3 differs on the first guest.
Both AI checksums and every recorded intermediate value agree from replay
turns 30,186 through 30,194.

A further trace located the first difference in the AI goal list. At turn
30,195, the host and second guest add `Storehouse.xml` as the fifth goal for
player ID 3. The first guest retains the previous six goals in the same
order. The original goal evaluates `MaxStorehouseDistance` and uses the
storehouse placement helper. Its activation input still needs to be
compared. The harness was interrupted after recording the divergence. The
three payloads and profile fingerprints were verified after recovery. Exit
codes were unavailable.

An experimental adapter preserved the native access mask around each NEO
route query and returned to the original comparator if the shared search was
busy. The three-client autosave reproduced the same checksum failure at
record 3,193 with this adapter. The experiment was removed from the checkout.

A separate control preserved NEO and used the original warehouse handoff
comparator. After the initial memory preflight delay, it loaded the original
autosave, restored NEO on all three clients and ran for 20:30 through replay
turn 39,166. All 13,098 common replay checksums matched. All 20 recorded
storehouse-distance samples and the 22 native checksum passes around the
previous failure also matched across the three clients. This control changes
the simulation from replay record 23 and leaves the NEO handoff policy to be
fixed. All three test processes exited normally and exact restoration passed.

A read-only road solver prototype kept NEO handoff selection enabled and
passed turn 30,195 with matching native checksums. After 15:38 of continued
play, the three clients stopped advancing at replay turns 35,608, 35,609 and
35,608. The first 8,605 common replay checksums match. The first guest's
checksum differs at the final shared record, ordinal 8,605. All three clients
closed normally and restoration passed. This prototype did not pass the
20-minute acceptance test.

The original autosave reproduced that later failure on the diagnostic NEO6
build. All 42 native checksum inputs and accumulator values agree from replay
turn 35,580 through 35,606. At turn 35,607, the first guest has consumed
one more value from the simulation random stream. Its random counter and seed
are the only recorded direct checksum inputs that differ. Both recorded AI
inputs still match. All 49 observed handoff comparisons and five sampled
storehouse-distance evaluations per client match. The first guest returned
to the browser, each participant wrote a desync snapshot, and all three
exited normally with verified restoration.

A second NEO6 run traced the original random operations and reproduced the
same replay mismatch. At turn 35,607 the streams agree for the first 288
recorded operations. Host and second guest then enter the native weighted
choice at 0x57b4f4. The first guest enters the native spawn-point calculation
at 0x72f2fa with the same incoming seed. Both paths use the original random
generator. All three closed normally with exact restoration again.

A subsequent run identified the affected wild boar and the earlier call
that changes its random input. All three clients have the same measured boar
state from replay turn 27,002 through 35,606. On turn 35,607 they enter the
same spawn-point call for that boar with matching position, path cursor and
movement state. The independently observed terrain cell and update flags
also agree before that call. The first guest has already skipped one weighted
animation draw on a different controller. The next weighted draw for actor
65,550 still happens on all three clients, but with different incoming seeds.
The resulting spawn-point draw produces a different path for the boar on the
first guest. The parent animation caller for the skipped draw remains to be
identified. This third reproduction had two differing replay checksums at
ordinals 8,605 and 8,606. All three clients produced desync snapshots, exited
normally and passed exact restoration.

The next caller trace identifies the missed weighted draw as the native
`CCarrier` animation transition from 3 to 2 at Ubisoft call `0x7eaf33`
(return `0x7eaf38`). The local class table and the animation component
offset were confirmed against the original executable. The host and second
guest make this call. The first guest's next captured carrier draw uses a
different dispatcher, which all three clients enter, but its counter is
already one behind. The two carrier dispatchers and saved state layouts
have independently mapped counterparts in Steam History and Gold.

The three snapshots identify carrier 65,562 with the same home storehouse.
The host and second guest have current, next and UI state 6 and destination
`0x1000f`. The first guest has state 14 and destination `0x1000e`. The
original Ubisoft dispatcher reaches the omitted animation branch from
state 6. In the original autosave the same carrier starts in state 13 with
destination `0x10022`. The first difference in the transition or
destination assignment still needs a trace before the desync. The
reproduced run matched 8,605 checksums before the first of two mismatches
and restored the three test copies exactly after exit.

The full carrier trajectory found the first differing gameplay value
22 turns before the checksum error. Carrier 65,562 matches on all three
clients through turn 35,584. At turn 35,585 only its destination ID differs.
The first guest is assigned consumer `0x1000e`; the other two are assigned
`0x1000f`. Both consumers have valid native entries on every client. At
this point the carrier is still moving to the same home storehouse with
the same position, path and speed. All seven recorded NEO intermediate
warehouse comparisons for this carrier agree across the three clients.
The last was at turn 34,915.

The original state-8 callback requests another job from the home storehouse
when the carrier is empty. The native selector sorts that store's transfer
requests before assigning the consumer. Ubisoft, Steam History and Gold map
to independently verified consumer-selection and destination-write sites.
The native History comparator is strict on numeric task priority, and the
native merge preserves list order for equal priorities. The same comparison
also bounds continued selection. At turn 35,585 all 35 entries of the home
storehouse's main list matched on all three clients before and after sorting,
including their order, full IDs, task ranks and creation turns. When the
main list was compared with the earlier preferred request, that preferred
request already differed. Its target was `0x1000e` on the first guest and
`0x1000f` on the host and second guest. Neither preferred request was in
the main list.

The preferred request is drawn from a separate temporary list of requests
from connected consumers. Independently mapped native code in all three
editions checks other carriers' assignments before adding a consumer's
requests, sorts the result, and chooses the first eligible request. The
eligibility check reads the assigned carrier, two task flags and the home
storehouse's available stock for the requested good. The captured run
reached 8,608 common checksum records, with the first mismatch at record
8,605 (zero-based). All three clients closed normally and the test profiles
and deployed files were restored and verified.

An earlier attempt in the same test window ended while the guests were
still joining the lobby. The first guest returned Windows status
`0xc0000374` and produced no replay. After verified restoration, the
reproduction transferred the save to each guest in sequence before starting
the world. This isolates the animation trace from the earlier lobby failure.

An earlier autosave attempt started before the second guest could restore its
world. In the two completed reloads, both guests joined before the first
Ready action, the host finished draining its native send queue, and each
participant advanced beyond the loaded turn. The manual save advanced
slower than real time under the three-client load. The timer above measures
wall time, and the replay counts give the actual simulation coverage.

Both decisive runs exited 0/0/0. The test payloads, local data and shared
Windows profiles were verified restored. [Three-client loopback receipt](receipts/support-history-three-loopback-20261118.json)

## Both supplied saves reach the restored lobby

The original Ubisoft History game now opens a resume lobby from each supplied
save with the corrected Lua and native NEO bridge. The lobby shows the saved
host configuration, the normal AI in slot 2, two open human slots, and NEO
Logistics on. The same process returned to the browser between the autosave
and manual save attempts, then closed normally. The local comparison used one
host and the private, validated resume-index import described below. The later
three-client results are recorded above.
[Runtime and build verification](receipts/support-resume-mode3-20261117.json)

Both saves reproduce the same rejection after the original `ChangeSavegame`
call. The game still reports an active network mode and host role, but changes
its game mode to 3. The original start script identifies mode 3 as multiplayer
Empire, alongside multiplayer Skirmish in mode 2. The resume code and the
native NEO bridge now recognize both multiplayer modes while continuing to
require their original host, session, and start checks. Resume also passes the
saved cooperative setting to `SessionCreate` and applies it to map setup after
session creation, in the same order as the original UI.

The standalone build also links the shared save inflater once when both
standalone support and NEO are enabled. The verified adapter remains a private
test build. The published RC3.25 assets and release approval are unchanged.

## First verified correction: resume-lobby lifecycle

The supplied autosave now reproduces the exact reported host failure locally:
`0xc0000005` at `0x00ed6ae1`, null ECX, read address `0xc0`. A controlled comparison
with the corrected Lua lifecycle removes that crash and exits normally, but
still reports a failed resume. This is a verified crash-path correction, not
a playable-save or sustained-multiplayer qualification. See the
[bounded comparison receipt](receipts/support-resume-lifecycle-20261117.json).

The original host-lobby destroy routine replaces its private state table.
The resume extension previously retained the old table, continued after an
original update that could synchronously change the page, and refreshed native
slots after its deferred callback could leave the session. The correction reads
the current state through an accessor, retires deferred work on lost ownership,
guards late slot refreshes, and disables START when no owned host lobby remains.
The browser extension also uses an accessor rather than assuming its state
object is permanent. Original Ready results and native CRC checks are unchanged.

The comparison used the verified original Ubisoft executable with the existing
normal standalone adapter (`f4f2db08e92cd0229c2ac749a3cede76724955277c0c3ba68b5fdc8e76e33112`)
and DXVK. This is not an exact RC3.25-package reproduction. Both variants used
the same explicit private import of the saves' CRC-validated embedded roster
and NEO choice into the missing local resume index, bound to the original
engine's slot/date/map/name. No save-supplied Lua was executed. Without that
controlled import, the supplied Documents-only export is rejected before the
original resume path because LocalData's index is absent, even with the supplied
Documents profile copied into the isolated test profile.

After the original `ChangeSavegame` returns a nonempty result, `applyRoster`
rejected its `host()` predicate. Its original query sequence is network mode,
host role, game mode 2 and local slot 1. The later comparison above identified
the game-mode condition. That rejection causes the normal session leave. The old
resume extension then reaches the invalid native slot update. The correction
prevents that follow-up crash without overriding the rejection or claiming the
underlying resume state is valid. The manual save and live three-player continued
play have not yet been qualified with this correction.

All actual comparison processes are closed; fixed runs exited 0. Prior LocalData
files, options, saves and profile bytes/mtimes, unchanged executable/adapter/archive
pins and shared Documents profiles were verified restored. Private comparison
archives, logs and receipts remain under `work/support-desync-20261117/`.

Source checks: 39 resume, 7 directory, 35 NEO and 15 Midsummer tests pass; three
historical Midsummer fixture checks skip. All five complete source archive
variants match their recomputed transformation hashes, and all 14 changed Lua
members compile. These source hashes do not approve or publish a native adapter,
installed package or RC4 release gate.

## Initial triage and subsequently supplied evidence

Offline triage of privately supplied host/client archives for a reported
three-human match with AI, followed by failing autosave/manual-save reloads.
Two later launcher reports identify RC3.25. No diagnostic game was launched: the current
Crossplay work reserves the test copies (see DEVELOPMENT-HANDOFF.md).

## Observed evidence

Times below are the supplied machines' log/file times; clock synchronization
between machines has not been verified.

| Evidence | Observation |
| --- | --- |
| Both game logs | Original Ubisoft History image, version 1.12.1395, PE timestamp `0x5d1b15c1`, image size `0x010e7000` |
| Host match | Throne of Tandria; three state-engine players initialized at 20:29:01 |
| First host crash | 21:15:00, exception `0xc0000005`, instruction address `0x00ed6ae1`, read address `0x000000c0`, ECX zero |
| Later host crashes | 21:18:48, 21:21:42 and 21:23:35, same exception, instruction, read address and zero ECX |
| Host dumps | All four supplied minidump exception streams independently agree with their corresponding logs |
| Client crash | 21:16:15, exception `0xc0000005`, instruction address `0x00d35ab0`, write address `0x00000030`, EDX zero |
| Client dump | No current dump supplied; its log says no dump path was available. Included client dumps are historical |
| Host autosave | `s1001.s7s`, modified 21:14:28, LogicTurn 27001 |
| Host manual save | `s1009.s7s`, modified 20:59:33, LogicTurn 18103 |

The first host failure occurs approximately 46 minutes after its logged match
initialization. The client failure is later by the supplied wall-clock times;
this timing does not prove a causal relationship or exclude an earlier desync
on the third machine. No explicit desync/checksum-mismatch record was found in
the supplied logs for this match. The client's file named DESYNCSAVE is dated
2021 and is not evidence for this incident.

## Version attribution

The host dumps and both current game logs identify an adapter with PE timestamp
`0x6af080ed` and image size `0x00186000`. These match the retained RC3.25 staged
adapter. That supports the user's version estimate, but these metadata fields
are not an exact package/content fingerprint. The archives contain game-profile
data rather than the launcher package identity. The later reports below identify
the launcher release, but do not verify the installed game/adapter hashes.
[RC3.25's release notes](RC3.25-STANDALONE-UPDATE.md) record that its
native adapter/gameplay contracts were unchanged from the preceding package.

## Subsequent player clarification and launcher diagnostics

The supplied conversation reports that all three players use Ubisoft History
Edition. This incident is therefore not a reported Gold/History or Steam/Ubisoft
mixed-edition match. Player 1 was host. Player 2 saw Player 3 leave, while Player
3 saw Player 2 leave. These reciprocal departure notices do not establish their
cause or a simulation-checksum mismatch. The host's crash ordering relative to
these notices remains unknown. The player also says the manual save was made
roughly 20 minutes before the crash and failed to load too; that approximate
recollection is separate from the file/log times retained above.

Asked about NEO Logistics, the player replied that they did not think it was
enabled. Treat the option as unconfirmed, reportedly probably off. Saved NEO2
metadata alone is not proof that the option was active in the reported match,
nor grounds to attribute the incident to NEO.

Both `S7-Diagnose(2).json` and `S7-Diagnose(3).json` declare
`s7-desktop-diagnostics-v1`, release `2.0.0-rc3.25`, source commit
`bac77a2180efb211513eaceed473fd491f764e20`, network metadata revision 9,
directory revision 7, NEO wire revision 5 and content contract
`s7-play-content-v3`. There is no declared version/protocol mismatch between
these two reports. The user confirms that their player/host assignment cannot
be recovered; retain them as anonymous reports rather than inferring roles.
Neither includes an incident timestamp or session identifier, so their memory
summaries cannot be assigned to a particular match or load attempt.

| Diagnostic field | Report (2) | Report (3) |
| --- | --- | --- |
| Renderer selection | native | native |
| Graphics probe | probe_failed; no devices listed | No failure reported; NVIDIA GeForce RTX 3060 listed |
| Memory samples | 865 | 44 |
| Peak private memory, MiB | 1564 | 329 |
| Minimum free process address space, MiB | 2236 | 3438 |
| Maximum measured pressure level | 0 | 0 |
| Monitor failed / recovery required | true / true | false / false |

Neither summary records a memory-pressure warning. Report (2)'s failed monitor
limits the coverage of that observation. RC3.25's memory guard sets both
`monitor_failed` and `recovery_required` on a monitoring exception, independently
of measured pressure. These flags do not prove an out-of-memory crash or save
corruption. A graphics-probe failure likewise does not establish the crash cause.
Renderer selection is configuration, not a captured incident module trace.

`known_lan_desync_resolved: false` is a constant returned by RC3.25's diagnostic
exporter, not an incident detector. Its presence in both reports must not be
interpreted as detected desyncs. These interpretations were checked against
the actual RC3.25 source commit's `desktop_backend.py` and `memory_guard.py`.

The launcher action is **Help > Save diagnostic report...**, or German
**Hilfe > Diagnosebericht speichern...**, with default filename
`S7-Diagnose.json`. The earlier request for a "connection report" referred
imprecisely to this export. It supplies launcher/configuration and bounded
memory-summary data, not a packet/session lifecycle trace.

## Save and replay checks

Both relevant host saves successfully passed the existing `tools/lan/bba.py`
archive reader's encrypted-directory checksum and every contained file's
decompression, size and CRC checks. SaveGameInfo identifies the same map and
an empty MapForge template in both. The presence of a separate Map Forge
template in the profile therefore does not establish its use in this match.

Both saves contain the WePlay multiplayer-resume cache and saved NEO2 metadata;
the autosave retains one intent mark and the manual save two. This establishes
saved metadata presence, not its causal involvement or semantic correctness.
Valid archive checksums do not establish that the original engine can reload
the serialized world correctly.

Both current match replays decompress successfully with gzip integrity checks.
Their decompressed sizes differ (host 1,463,002 bytes; client 1,461,606 bytes).
Their first differing byte is at offset 9,112. Raw file equality/difference is
not a simulation checksum comparison; no replay-based desync determination was
made.

## Narrowed native failure path

Read-only inspection of the locally available original executable used its
verified SHA-256 `4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f`,
matching the logged image timestamp/size. Fault-site bytes also match the
supplied host and client logs. Original game code/disassembly remains private.

The original Lua registration identifies callback `0x00ef15e0` as
`SetSlotCounts`. Its original call chain reaches `0x00ed2730`, then
`0x00ed6ab0` (the operation labeled SetJoinAndDiscoveryOverride in the original
executable). The host fails there when dereferencing a null handler loaded
from the network manager's `+0xa4c` field. The same chain appears in all four
host crash logs. This is a concrete network/lobby-lifecycle failure site;
it is not evidence of a failed archive CRC or an identified world-reader bug.

The client fault is instead a null link during an original linked-list removal
operation. Without a current client dump or lifecycle trace, its origin and
relationship to the host crash remain undetermined. The host minidumps do not
provide a usable captured memory range for the fault context's stack/manager
fields in this inspection; the earlier cause of the missing handler was not
reconstructed.

The later Lua and adapter corrections resolve the observed failure through
resume-lobby setup. The original save files were used unchanged.

## Required follow-up

1. The two received launcher diagnostic reports already identify RC3.25.
   Preserve their unknown player assignment; the user says it is unrecoverable.
   Do not request repeat exports merely to reconfirm their versions or roles.
   Preserve the already supplied saves, replays, logs and host dumps;
   another full historical profile export is unnecessary.
2. Determine whether each failing load was started as multiplayer resume or
   single player, and whether the same host was used. The logs alone do not map
   the three later failures to a particular save or UI action.
3. Isolate the first guest's divergence near replay turn 30,197. The retained
   host autosave from logic turn 30,002 makes a shorter reproduction possible.
4. Compare the bounded loopback departure with the original mid-match report.
   Further verification needs stable three-client play from the autosave and
   a test on separate machines with the intended release.

Private inputs, extracted saves, replays, dumps and native inspection artifacts
remain under the ignored `work/support-desync-20261117/` directory. No account
cache was inspected. The diagnostic adapter, archives and profile data were
deployed only to the isolated test copy, which was restored after each run.
