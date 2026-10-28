# Reliable RPC backpressure and native connect ownership

Verified 2026-10-28. These fixes address reproduced transport/lifecycle defects;
they do not establish a fix for the previously observed simulation desync.

## Accepted RPCs must survive temporary backpressure

The client can receive Register's response and send its next RPC before the
Register ACK reaches the service. Previously the service acknowledged the
incoming DATA, consumed its complete RPC, then rejected it because registration
was not yet acknowledged. Retransmission could not repair the loss: its reliable
sequence was already consumed. The same loss occurred when two replies were
awaiting ACKs and a third valid RPC arrived.

`PrudpLocalLoginSession` now leaves blocked messages in its existing reliable
assembler and resumes the ordered drain during service maintenance after ACKs.
No extra queue is introduced: the receive window remains 32 fragments, aggregate
plaintext remains limited to 64 KiB, and at most two responses are pending.
Existing response retries, idle expiry, lifetime and response budgets remain.
Disposal and overflow clear retained plaintext; overflow closes the session.
Unsupported or definitively budget-rejected RPCs do not gain activity credit.

The login DATA path sends its transport ACK after decryption/sequence validation
and assembler admission (or recognition as old/duplicate). A refused future
fragment remains unacknowledged, so it can be retried after window advancement.
Invalid encrypted inputs and closed contexts receive no admission ACK. Separate
ACK-only and crypto-inspection modes retain their existing behavior.

Regression tests exercise delayed Register ACKs, three pipelined RPCs with two
reply slots, out-of-order fragments, pending/completed duplicates, receive-window
advancement and retry, bad crypto, overflow, timeout and zeroing. Actual loopback
UDP tests confirm that deferred replies are delivered exactly once.

## Nested original callbacks must preserve the connect thread boundary

Forwarding a foreign-thread owner update to original `C956C0` did not fully
protect the local bridge. That original callback can itself reach the patched
`C956F5` request; original reconnect `C95840` similarly reaches `C958CF` connect.
Both replacements previously accessed requested/pending state and could start
a login job on the foreign thread. A synthetic nested-callback reproduction
demonstrated both paths before the correction.

Both local connect entries now check a lock-free, four-byte atomic bootstrap
thread token before reading ordinary bridge or game state. A foreign caller
receives the established `LocalConnectUnavailable` error in its supplied output;
no implicit owner-result or requested/pending mutation occurs. All five installed
callers use stack-local outputs. Null output retains the integrity failure.
Original owner callbacks and their own flag clearing still run; the next owner
tick can start the genuine job on its owning thread. There is no new deferred
thread queue, synthetic readiness, or fabricated successful operation.

Nested original-callback regression tests preserve callback delivery and next
owner-tick progress. Invalid-object tests verify rejection before dereferencing
the foreign self pointer. Independent private mutants removing each guard are
detected by these regressions.

## Validation and delivery

- Native bootstrap: 505 baseline, 1,083 network-pump and 1,085 lab checks; each
  build also passes 18 connect and 24 two-core checks, with warnings as errors.
- Network candidate: 92 platform, 78 network-guard and 517 LAN-membership checks.
- Service: 8,845 standard and 11,309 separate lab checks; focused runs include
  3,296 login-session, 7,817 assembler and 49 real loopback UDP checks.
- Installer/LAN Python: 184 tests, one skipped privilege-dependent symlink case.
- Package manifest, embedded-Python installer startup and ZIP CRC checks pass.

Candidate: `outputs/S7-AutoLAN-Testpaket-2026-10-28-r3.zip`, 49,935,645 bytes,
SHA256 `d103425df4aa688655454e4846eae914017f02ded6e9372671292eb502529a9f`.
The archive contains no game files or LocalData. It includes the normal
original-executable network-pump adapter, not laboratory EXEs or lab service.
Existing recipients need the same revision on all participants.

Pinned adapter: `work/native-lifecycle-review/network-pump/uplay_r1_loader.dll`,
SHA256 `14f9bc05638480a9f3cf32c6e668ce35781e47dc368a526d0c75223b5412bf16`.
Pinned service: `work/service-order-review-20261028/service/S7Patcher.dll`,
SHA256 `6b5c3c3b5cc2e3c9884f92da9a58b071e619ddd5c4fcf2fdc97d0669f00084c7`.

No new game, two-PC match, endgame, checksum-equality or desync-free result is
claimed for this revision. The previous r2 fresh-copy installation evidence is
historical, not an r3 gameplay qualification. No installed game copy was updated
with these new binaries during this work; graphics settings/wrappers are unchanged.

## Coordination boundary

At the user's request, the separate chat `D3D11-Port und Grafik prüfen`
(`01a181c0-2544-7582-aec9-afbc3ec6e810`) exclusively owns further graphics/CPU
hang investigation and live graphics tests. This chat owns the networking and
native connect-thread corrections. Do not run game measurements simultaneously
or modify that chat's `tools/d3d11` files as part of this networking work.
