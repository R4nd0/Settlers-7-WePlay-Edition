# Gold NEO logistics binding, 2026-11-15

The Gold transport binding now passes a bounded real Gold/Steam History
standalone DXVK world. At the last recorded status, the read-only probe had
validated **12356 native transport comparisons, with no binding exclusions**.
The two clients produced 570 consecutive common original checksums, finished
turns 0–569, with no differences. Both exited normally; strict verification
confirmed restoration and no remaining owned game process.
The [sanitized verification receipt](receipts/gold-neo-binding-20261115.json)
records the exact scope and source fingerprints.

**NEO was disabled and every original sorting decision was retained.** The
detached policy would have changed 1045 of those recorded decisions. This is
evidence for usable native object bindings and policy inputs, not NEO gameplay,
delivery stability, distance selection or mixed save/reload qualification.

## Corrected Gold layout

The earlier synthetic model produced no accepted live comparisons. Bounded
diagnostics on the original sort-entry path established that its caller,
stack shape, comparator owner and complete source identities were correct.
Its object interpretation was wrong:

| Native property | Correct Gold interpretation |
| --- | --- |
| Request task `+0x4c` | Goods type; not the reservation handle |
| Request task `+0x70` | Reciprocal transfer reservation handle |
| Intrinsic group 9 | Present on real request tasks; not an invalid object |
| Native unset group 10 | Preserved as unsupported/default ordering |
| Production-backed transfers | Multiple records may refer to one production task |

Independent inspection of the original constructors, base-task initialization
and reservation operation corroborates those fields. The corrected model keeps
complete generational IDs, both registries, reciprocal request binding, typed
objects, parent ownership/links, bounded vectors and native thread/lifecycle
requirements. A production task does not receive a fictitious one-to-one request
reservation field.

## Components and integration contract

`native/crossplay_gold_logistics_binding.h` provides the read-only object model
and ten original prerequisite region checks. `crossplay_gold_logistics_policy.h`
provides a detached metadata ledger, the shared History FIFO comparator and the
shared NEO2 state codec. Ordinary adapters do not include or activate them.

The session adapter must verify the full supported executable, the original
region guards, authenticated NEO capability/choice and the owned live world. It
must serialize all operations on the simulation owner thread. `CaptureWorld`
alone is not authorization to start a world or install a policy. Keep the large
ledger in stable owned heap/static storage rather than a small callback stack.

Ordinary eligible requests use their original creation turn and inherited
individual intent. Equal birth turns remain equivalent, retaining native stable
arrival order. Production-backed transfers and native unsupported/unset roots
use History's default native-Prefer key. No default group precedence is added.
Saved intent is fully decoded and rebound to the live task identities before
replacing the ledger. Native task records, stocks, reservations, checksums and
game time are not rewritten by these components.

The original native comparator bridges remain experimental. In particular, the
second inlined path was not observed during this armed world window. The live
result validates the private prototype core from which these components were
extracted; the public fixture validates the extraction independently.

## Verification and remaining work

`native/build_gold.ps1` builds/runs `gold_logistics_binding_tests.cpp`. It checks
the corrected fields, full identity exclusions, request/production semantics,
parent intent, stable ties, cycles/ownership, NEO2 round trips, atomic failed
restore and ordinary-host guard rejection, without game data.

The private integration additionally passed both real x86 comparator bridge
fixtures, including actual diagnostic callbacks preserving original arguments,
stack, results, registers, flags, FP/XMM state and LastError. Its sixteen combined
original regions passed fifty compiled mutation/unreadability checks.
The extracted public guard passed a further 32 checks across its ten regions,
including LastError preservation and an unscoped negative control. The verifier
also retained all 128 checks of the earlier movement guard.

Still required before advertising working Gold NEO: authenticated common mode
and saved-state integration, original individual-priority event handling,
distance handoffs, targeted workshop pickup integration, actual shared
deliveries, adapted-map coverage and mixed saves/reloads with both History
editions. Separate-PC EasyTier and effective x20 remain unqualified.
