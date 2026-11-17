# History multiplayer crash report, 2026-11-17

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

No adapter fix, save repair or successful reload is claimed. Skipping the
null dereference alone would not restore an owned multiplayer session.

## Required follow-up

1. The two received launcher diagnostic reports already identify RC3.25.
   Preserve their unknown player assignment; the user says it is unrecoverable.
   Do not request repeat exports merely to reconfirm their versions or roles.
   Preserve the already supplied saves, replays, logs and host dumps;
   another full historical profile export is unnecessary.
2. Determine whether each failing load was started as multiplayer resume or
   single player, and whether the same host was used. The logs alone do not map
   the three later failures to a particular save or UI action.
3. Once the reserved copies are available, reproduce with a separately guarded
   RC3.25 setup and isolated profiles. Capture handler/session ownership at
   SetSlotCounts and original session create/leave transitions before proposing
   a lifecycle correction. Retain native checksum and executable/ABI guards.
4. Qualify any correction against both supplied saves, continued native turns,
   AI/NEO activity and a subsequent save/reload. A menu, a skipped crash or
   synthetic fixture would not establish recovery of this match.

Private inputs, extracted saves, replays, dumps and native inspection artifacts
remain under the ignored `work/support-desync-20261117/` directory. No account
cache was inspected, and no runtime/profile files were deployed or changed.
