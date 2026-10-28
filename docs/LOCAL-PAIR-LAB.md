# Two real clients on one development PC

This is a development laboratory, not the LAN installer. It was exercised on
2026-10-26 and prepared again on 2026-10-28 after the user authorized local game
operation without further manual restart requests. The original installation
is unchanged.

## Isolation

`tools/local-pair/lab_executable.py` derives two fixed, fully fingerprinted EXEs
from the supported original. It changes only two fully guarded data slots:
the singleton mutex and the My Documents profile suffix. The latter has a
36-byte slot (33 original characters plus three NULs); the current 34-character
work suffix and two NULs fit without moving or changing the adjacent `\\DLC`
literal. All three callers use fixed copy lengths, so nine individually guarded
immediate operands change their allocation from 34 to 35 bytes and copy/end
lengths from 33 to 34 bytes. Instruction opcodes and control flow stay unchanged.
Both complete EXE fingerprints and adjacent bytes are checked. This changes no
simulation, timing or protocol behavior. The normal adapter
rejects both resulting hashes. The separately compiled laboratory adapter
rejects the original and every unknown hash, retaining PE, callsite, ABI and
thread checks.

Build the adapter with `native/build.ps1 -LocalPairLab`. The matched complete
EXE hash selects A = 127.0.0.2 or B = 127.0.0.3. Wildcard and explicit 127.0.0.1
binds use that slot address, as do initially unbound senders. Both games can
therefore bind their own UDP 3074, 6200 and 9103. No Windows adapter, hosts or
firewall settings are changed. The normal build has no slot-address behavior.

Build the separate service with `-p:DefineConstants=S7_LOCAL_PAIR_LAB`. Only
this build recognizes `S7_LOCAL_PAIR_ADDRESS`: a canonical private IPv4 address
actually assigned to this computer. When Storm advertises that physical address
but the registered client uses IPv4 loopback, project it to that client's
registered address, retaining the game port and descriptor. Registration/RVCID
ownership remains required. Unrelated addresses are not projected. The normal
service ignores the environment flag and keeps its original ownership policy.

Do not use laboratory binaries in production bundles or PC 2 update packages.

## Local artifacts

Under the current repository's ignored `work/` directory:

- `pair-A` and `pair-B`: independent Base copies, not links; fresh LocalData
  identities/saves; `lab-manifest.json` checks EXE, adapter and Interface archive.
- `prepare_local_pair.py`: already-run private preparation script. It refuses
  existing target folders; do not rerun over these copies.
- `nomap-pair-20261028/launch_pair.py server|A|B`: current private process
  controller (`check` validates without launching). A/B additionally take the
  explicit `--simple-menu` switch for the optional paired test. Start server
  first, wait for its ready line, then A and B. It writes loopback runtime
  configuration, locks each slot and records its owned PID/exit status. It starts
  no Ubisoft launcher. Service lifetime is one hour; registered-peer limits
  still apply. Stop the exact owned service when finished.
- `nomap-pair-20261028/ready.json`, runtime INI files and raw logs remain private
  and untracked. `desync-pair-20261028` retains the earlier measurements;
  `operate_local_pair.py` and `pair-ready.json` are also historical controllers.

The current fixed EXE data suffixes are `\Claude\Projects\7S_Patcher\work\A`
and `\Claude\Projects\7S_Patcher\work\B`, relative to the actual Windows My
Documents folder. On this machine that folder is on Z:, so both remain in the
current repository's ignored work directory. Old laboratory EXE hashes are
rejected. Historical profile folders from the former Codex workspace may only
supply selected Options settings; identities, Profiles.xml and saves are not
copied into the new profiles.

## Reverse-host run with AI players, 2026-10-28

Using the same pinned adapter/service below, B hosted a fresh public Turminsel
lobby and A joined as guest. Two normal-difficulty AI players, Bischof Matheusz
and Ramirez, filled the remaining slots. The launcher again used the explicit
simple-menu/VSync combination with native D3D9, 1280x720 and two physical cores.

The passive observer completed 601 paired samples over 600 seconds, including
the initial loading transition. At the end A was at turn 5123 and B at 5122.
After turn 100 every observed A horizon was 5; B retained 1. Initial A startup
values included 1 and 2 before settling. Individual sequential samples differed
by up to six turns; they are not atomic snapshots or simulation checksums.
Private memory peaked at 1423.7 MiB for A and 1431.2 MiB for B. The bounded
native diagnostics emitted no FP-change label during the trace.

The real UI showed active AI play and completed construction: a guest farmhouse
and a host residence. Root enabled the host tavern's three-gold/one-tool trade;
a later host view showed gold 1 and tools 23 versus initial 10/20. Independent
user input was detected in both windows; Root then stopped gameplay input and
continued only the passive observation. No measured command-latency or exact
attribution of all user actions is claimed.

Both clients continued beyond the earlier reported turn-2154 failure point.
This bounded one-PC scenario does not establish the cause of that two-PC
desync, checksum equality, network-loss tolerance, three real clients or large
endgame transport-network stability. The 600-second trace includes loading;
it is not a claim of ten full simulation minutes or an all-hardware CPU result.

After the user requested closure, both games and all three owned controllers
completed with code 0. Host inspection found no remaining S7 game, the exact
owned service was stopped after process/start/path verification, and both
temporary runtime INIs were absent. The graphics chat received the explicit
free slot for its newly requested display-options qualification. Root performs
no parallel live or CPU-heavy work while that slot is in use.

Private evidence: `work/nomap-pair-20261028/reverse-ai-observation-01.jsonl` and
`reverse-observer-manifest.json`. Game identities, raw logs and images remain
private. No package or installed production copy was changed by this run.

## Initial functional smoke run: simple menu plus VSync, 2026-10-28

The current source (`373fe12`, including the networking fixes) was built as a
separate two-core lab adapter with a matching linker map. Its full DLL SHA256 is
`0c3470745a302477c5db4e2661023479fadc04118aaa2e2ec976ee6684e7c1b6`;
the matching map is
`4490f271018ed86f863e9f26a4198ec8ce3750974511f2c7de203e49743c5178`.
The lab service SHA256 is
`e5f14d615464aff694d7c7543273b68d29036a672ac460587c33ca7cfdd551cd`.
No lab binaries belong in the normal installer.

The private launcher uses the current shared graphics argument helper and a
validated boolean to select exactly `-vSynch -noMapInMainMenu`. Neither copied
game uses a rendering wrapper. The original installation was not changed, and
profile contents were not inspected during deployment. Only the closed copies'
adapters/manifests were updated, with backups. Checks passed: 1,085 lab
bootstrap, 71 original/EXE derivation, 18 connect, 24 CPU and three
strict-argument checks.

Observed directly through the game UI: A opened a public Turminsel lobby, B
found and joined it, both names were visible in the same lobby, B became ready,
and A started the match. Both games loaded the map and displayed gameplay.
The guest camera zoom and build selection were usable. One attempted building
placement was rejected as a locked area, so no successful guest construction
command or command-latency result is claimed.

An initial 180-second numeric trace covers the menus/lobby/loading transition.
A subsequent 90-second trace contains 91 paired samples: observed game turns
advance from 31 to 894 on both clients, with host horizon 1 and guest horizon 5.
Individual paired reads differ by up to four turns; the snapshots are sequential
rather than atomic. These observations establish neither equality nor mismatch
of simulation checksums. Private memory
peaked at approximately 1,368 MiB per process. No FP-change diagnostic label
was observed within the bounded window; this cannot exclude unobserved changes.

This is a one-PC smoke test of initial lobby admission and match startup with
the optional menu mode. It establishes no long-session/endgame, reverse-host,
two-PC, three-player or desync-free qualification. Initial camera placement and
all reward/castle preview compatibility were not exhaustively tested. The mode
remains experimental and disabled by default.

When Computer Use detected independent user input in the guest window, Root
stopped app input and asked whether to leave the test running or close it. The
user requested regular closure of both games. Both game exit records and all
three controllers then completed with code 0; host process inspection found no
S7 game. The exact owned service was stopped after process/start/path checks,
and both temporary runtime INIs were absent. The graphics chat was explicitly
given the free test slot for its separately authorized water-quality comparison.

## Earlier menu-only run: 2026-10-28

Both newly derived games reached the main menu with distinct visible names,
Lab A and Lab B. Separate profile folders were created under `work/A` and
`work/B`; only filenames were inspected. Each process used two allowed physical
cores (mask `0x5`), native D3D9 and a 1280x720 window. The real configuration
service used the separate LocalPairLab build.

A passive, hash-pinned observer collected 91 paired samples over 90 seconds.
It read only typed numeric fields/counters, process resource counters and fixed
FP diagnostic labels; no peer/profile identities or packet bodies were read.
Median owner updates were 15 Hz in both games. Median process CPU use was
0.469 and 0.586 core-equivalents; observed maxima were 1.61 and 1.86. These are
short two-client menu observations, not proof of acceptable long-run CPU use.

At elapsed seconds 8/9 and 59/60, both clients had zero progress in the core,
owner, manager, echo and screen-loop counters while each consumed roughly one
CPU core. Thus this run also contains real engine stalls; it is incorrect to
describe the entire run as smooth. The progressing-core/skipped-owner signature
was not present in these stalls. Their cause remains unproven. Screen-loop
completion counts are not displayed FPS or simulation checksums.

After A opened the session browser, clicking Create Session was followed by
Computer Use capture failures: `FrameArrived timed out: timed out waiting on
channel`. Reselecting the windows and resetting the JavaScript helper did not
restore capture. Later numeric samples still progressed, so persistent game
failure is not established. Capture-call timestamps were not recorded alongside
the samples; the temporary stalls cannot be causally attributed to capture.
No map, lobby join or match was completed in this run. No observed FP-change
label appeared within the bounded diagnostic window; that does not exclude an
FP issue outside the observed calls/window. Both games were closed regularly
with exit code 0; the exact owned service was then stopped.

Verification: 71 original/derived EXE checks, 82 network checks, 92 platform
checks, 1,051 bootstrap checks and 18 private observer boundary tests passed.
The original executable remained unchanged. This fixes the laboratory profile
migration, not the previously reported two-PC desync.

## Historical observations, separate from tests

1. Two actual games authenticated concurrently. A created a public Turminsel
   lobby and B displayed it. A shared loopback address initially caused host
   descriptor ownership rejection.
2. With the laboratory descriptor projection, the real JoinSession request was
   reserved and answered. The client reported **link-reach-failed**, kind 0,
   description length 71. UI remained `Sitzungsfehler (0)`. Evidence is in the
   private `pair-shared-loopback-link-failed.jsonl` log. This narrows the failure
   to the native peer connection; it does not establish its cause on two PCs.
3. On the shared address, the guest lacked a persistent UDP 3074 listener. The
   distinct-address build subsequently authenticated both real games and OS
   inspection confirmed each had UDP 3074, 6200 and 9103 on its own address.
4. Screen capture then failed repeatedly with `FrameArrived timed out`, including
   after reselecting windows and resetting the Computer Use session. No further
   UI input was attempted without a valid observation. Both owned games and
   their service were stopped. The user subsequently authorized launching both
   copies while they perform the lobby/join clicks; this is the current workflow.
5. Distinct-address joining initially still sent game traffic to the physical
   NIC. The lab catalogue now projects a copied snapshot through the same
   registered-host route validation as JoinSession; the stored record remains
   unchanged. Both real games subsequently logged sent/received UDP 3074
   datagrams to/from the other slot. This is not proof of a completed handshake.
6. D4781C/D49B10 returns a positive native request result, but the user still
   sees Sitzungsfehler (0). The D3F928 completion and seven selected Session
   rejection branches have not been observed; neither join report RPC arrives.
7. A guarded post-core manager tick matching ED7A40/ED7A47 runs in both games,
   but did not fix joining. Vtable 10934EC+14C targets CA6380, which drains
   completed jobs and invokes callbacks. Count actual virtual updates to avoid
   duplicates and revalidate owner/core/manager after callbacks.
8. The blocking Join Session wait at ED68B9..ED68D3 calls only ED3BA0 and sleeps;
   it does not return to UPLAY_Update. A lab-only replacement at ED68C4 supplies
   the existing local pump, retaining startup/thread/ownership/reentrancy guards
   and the original master fallback. The real wait bridge ran; joining still
   failed with no completion observed.
9. Bounded external status reads found master started=1, core tick enabled=1,
   master state=1, four children and network phase policy=1 on both games.
   C97FE0 calls BAF5D0 only when the master state exceeds 1. The new lab-only
   fallback dispatches the same Echo phase 3 operation when that gate skips it;
   BAF5D0 retains its own real SDK initialization check. It changes no readiness
   flags or result. A guarded observer at C98023 prevents duplicate dispatch,
   with owner/core revalidation after callbacks. The fallback and dispatcher
   C33020 both ran, but the real join still failed.
10. Network+30 is 1, so ED3BA0 omits its CB90C0 phase 1/2 boundaries. The lab
    pump now supplies those around the master tick when the policy selects
    external boundaries and the core is enabled, with teardown checks after
    each call. Both fixed delivery labels were observed. Joining still failed.
11. The request result is specifically 2 (pending). Static RTTI resolves its
    B64190/1087F1C event to player::inEvent::RequestJoin, not
    session::inEvent::RequestJoin. Therefore the absence of C24050/D3F928 is
    not sufficient evidence of an unprocessed queue: those are later stages.
    The correct player handler B833E0, called at B7D92F, was observed entered
    and pending. Its Peer Conflict, Pending Request, Invalid GUID and missing
    player-data branches were not observed in that run.
12. The normal player path constructs B643C0/108737C:
    session::inEvent::RequestTicketSessionId. C0BC30 initializes it; Session
    dispatch C0E809 invokes C25610. That function looks up an existing native
    session; the failed lookup branch C256DA constructs an empty diagnostic
    before producing an asynchronous failure. The current candidate observes
    entry and this exact branch without reading the key, GUID or ticket.
    Its live test failed: guest entry and pending were observed, while the
    missing-reference branch was absent. The user's initial success selection
    was accidental and explicitly corrected; there is no successful join.
13. Followed the found-reference path into C24BC0. Nine lab-only diagnostic
    callsites now classify state/flag rejection, empty peer, transport pool,
    host insertion, existing request, ticket pool, no ready peer and host
    reference rejection. Original constructors, arguments, results and error
    slots are preserved. Callsite addresses classify empty diagnostic strings;
    no lookup keys, peer identities or ticket contents are read. Live result:
    joining still fails, with none of these rejection branches observed.
14. The next candidate observes the two onward paths at C252AC (C2BEB0,
    thiscall RET 1C) and C25277 (C0BAE0, thiscall RET 1C), plus the original
    result getter at C2C269. Opaque arguments are only forwarded. Tests verify
    stack arguments, original return values and last-error preservation. Live
    result: guest transport entered/created and link accepted, followed by
    ticket/player pending; no internal event construction was observed. The
    user still sees the session error. A positive link result only establishes
    that initial transport request, not a completed connection or join.
15. Static RTTI identifies C0BAE0's event as ReceiveTicketRequest. Added fixed
    observations for the C0E961 -> C22790 event handler, C22819 -> C223F0 wire
    handler and C22611 -> C15E80 application-approval dispatch. No event payload
    or identity is read; forwarding and result/error preservation are tested.
    Live: host wire receive, approval dispatch/queued and wire accepted were
    observed; guest transport creation/link acceptance and pending request were
    observed. Join still fails. This proves the request crosses to the host,
    but does not prove that the application approved it or membership completed.
16. Static dispatch follows RequestTicket through PlayerManager_Network's
    B86710 to session::outEvent::JoinRequest, then SimpleSessionHandler's
    virtual callback. The game override ED3070 rejects when its C0 full/in-game
    boolean is set, otherwise invokes original CA36C0. The new candidate
    observes B7DA97 -> B86710, B86770 -> C0A8B0 session lookup (after the native
    metadata type check), and virtual slot 1140060 -> ED3070. Only presence,
    result and the known gate boolean are classified; nothing is overridden.
    Live: player-ticket entered, metadata-qualified session found, positive
    return. The game-specific full/in-game gate was not observed; join fails.
17. B86710 routes its JoinRequest at B86886 -> CFA5E0, but ignores the returned
    result. That route can reject an invalid destination or unavailable engine
    context. The current candidate classifies its original result, without
    reading the destination or payload. CA285B -> C9A280 additionally observes
    a true original JoinRequest type match, covering base handlers as well as
    the game override. This avoids interpreting absence of the game-specific
    gate alone as absence of all application dispatch. Current candidate
    Live: host routing returns queued, but the JoinRequest type match does not
    reach the handler. Joining still fails.
18. Known-field, read-only runtime counters show the host's main queue holding
    ten events while its scheduler frame counter stays fixed; the guest frame
    counter advances and its queues are empty. The existing global
    natural_updates_seen latch disables PumpOfflineBootstrap forever after any
    owner-only C956C0 update, even when no later core dispatch occurs.
    Current lab-only candidate work/native-pair-core-resume removes that
    permanent owner-based stop condition. It observes the exact validated
    scheduler's +78 native frame counter instead, skips a fallback when real
    intervening core progress is seen, and resumes when it stops. Interrupted
    supplied ticks invalidate the snapshot. Thread, recursion, executable,
    native object, callback teardown and default-build isolation remain guarded.
    The blocking join uses the same policy. Regression tests cover queued work
    after an isolated owner tick and recovery after an external core tick.
    Live result pending; this change does not establish admission or gameplay.

Bounded external type/state inspection also found the main Echo implementation
as the scheduler's only child, initialized state 3 on both slots. Its dispatcher
is C0CD90; PlayerManager_Main dispatch is B7D570. The observed active task-loop
counts were 1 on the host and 0 on the guest. These are observations, not proof
that the guest needs a fabricated loop or readiness value. Inspect only known
typed state/counters; do not read local peer/profile identities.

Diagnostic wrappers emit only fixed port/direction/result/branch categories,
preserving original return values, arguments and error slots. They inspect no
peer/profile identifier or packet body. D49B10 is stdcall with seven stack
arguments (RET 1C); D3F928 calls the simple result getter 4DA460.

A short attempt using the physical LAN address triggered a Windows firewall
prompt. It was not acted upon; those owned processes were stopped and the
laboratory remained loopback-only.

The current real run uses work/native-pair-core-resume with
work/server-pair-activity. Slot B hosted and A joined. Original host gate open,
positive guest completion, service ReportJoinSuccess reply and subsequent host
update were observed without rejection/expiry. The user confirmed both players
visibly occupy the same lobby. This establishes a genuine one-PC lobby join.
The earlier native-only positive result was insufficient until the service
success report was supported; one intervening run expired before creation.

The next real test is starting this shared lobby, then reversing host direction
and eventually validating the two-PC LAN build. Read fixed classifications and
subsequent RPCs. Do not fabricate success or suppress native failures. Match
start, host notification details, long-run synchronization and the two-PC LAN
join remain unverified.

The user subsequently confirmed both instances loaded the map and the match
is running. Both stored game processes remain responsive; the service accepted
further host metadata updates and continues answering PINGs, with no recorded
rejection/expiry in the observed run. The user then confirmed five minutes of
play with actions on both sides and no desync/disconnection message. The user
then closed both games; A was launched alone to establish the reverse-direction
host lobby before B started. Host A logged its original gate open, guest B
logged positive completion, and the service confirmed ReportJoinSuccess. The
user confirmed joining and match startup work in this reverse direction too.
This establishes initial match startup in both one-PC host directions; it does
not establish checksum equality, long-run stability, same-process leave/recreate
behavior or a working two-PC LAN build.

## Historical verification (2026-10-26)

Laboratory build: 876 bootstrap checks (including both exact hashes, per-byte
digest mutation rejection, native ABI/result preservation, pump order,
duplicate suppression, reentrancy and teardown); 69 network checks, including
same-port distinct-address binding, UDP transfer and automatic source binding;
88 platform checks; 18 connection-state and 14 CPU checks. Service: 5,776 checks,
including actual-NIC projection, port preservation, both slot addresses,
idempotence, invalid/nonlocal inputs and encrypted synthetic peers.

Default build: 359 bootstrap, 61 network and 5,755 service checks independently
verify that laboratory hashes and environment flags cannot enable the laboratory
behavior. The real original also passes 13 read-only derivation/immutability
checks. These tests and the two-process
startup evidence do not establish a successful native join or desync-free play.
