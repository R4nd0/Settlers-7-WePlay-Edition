# History multiplayer crash report, 2026-11-17

## Both supplied saves reach the restored lobby

The original Ubisoft History game now opens a resume lobby from each supplied
save with the corrected Lua and native NEO bridge. The lobby shows the saved
host configuration, the normal AI in slot 2, two open human slots, and NEO
Logistics on. The same process returned to the browser between the autosave
and manual save attempts, then closed normally. The local comparison used one
host and the private, validated resume-index import described below. Loading
the world with all three players and continued play remain to be tested.
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
3. Qualify both supplied saves with three Ubisoft History clients, matching
   native turns and checksums, AI and NEO activity, and a subsequent save/reload.
4. Investigate the original mid-match departure separately. The reproduced
   failure explains the reload crash but leaves the match interruption open.

Private inputs, extracted saves, replays, dumps and native inspection artifacts
remain under the ignored `work/support-desync-20261117/` directory. No account
cache was inspected. The diagnostic adapter, archives and profile data were
deployed only to the isolated test copy, which was restored after each run.
