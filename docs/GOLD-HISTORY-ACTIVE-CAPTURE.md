# Owned active-player capture candidate

`native/crossplay_active_setup_capture.h` adds an opt-in, read-only diagnostic
candidate for invoking the [original active-player writers](GOLD-HISTORY-ACTIVE-PLAYERS.md)
on all 25 preparation rows. It is excluded from ordinary adapters. The candidate
has **not yet completed a live game capture** and cannot authorize common start.

The caller supplies the existing executable-bound participant, native host and
guest connection bindings and role. Existing checks require the actual two-human
Ready Monhaim lobby, the closed unused slots, native owner thread, matching map
cache and owned preparation. The candidate checks the original row/Castle code
regions and the existing stream/visitor method guards before writing.

Before native calls, a bounded dependency walk validates the data the audited
row writers use: selected symbolic-color entries, player strings, reward/trade/
Untouchable vectors, both blueprint lists, allies, Castle Forge strings and
part/parent/slot vectors. Linked lists require exact count, head/tail and reverse
links with no cycle or repeated node. Symbol entries and strings must be readable;
vector spans must match their element size. Castle arrays must have equal counts.
The candidate imposes explicit diagnostic limits and a total 128-KiB dependency
budget; it rejects data outside those limits rather than changing it.

The original format-8 binary writer runs separately for each row. The caller
receives only its byte count and SHA256. Each native stream is bounded to 64 KiB
and checked for its expected table, flags, position and length. On completion,
the same session/thread/preparation/cache ownership, raw local setup fingerprint,
pointed-to dependency fingerprint and original code guards must still match.
Failure clears the entire result. No native reader, event factory, queue mutation,
Ready change, identity normalization or network send is performed.

Local fingerprints intentionally retain native pointers and unknown fields.
They detect local changes and are unsuitable for cross-edition equality. Row
digests likewise retain native identities/object values; differing real rows must
be investigated, not silently rewritten to match. The owner record has no valid
rule/template-writer agreement and cannot be used as a confirmed setup checkpoint.

## Verification and remaining test

The x86 MSVC `/W4 /WX` dependency fixture passes 66 checks: valid 25-row graphs,
stable hashes, selected-color bounds, inaccessible pointers, string lengths and
terminators, pointed-to text/vector/list/Castle mutations, vector alignment/counts,
list cycles/backlinks/count mismatches, unequal Castle arrays, total byte limits,
failure clearing and changed native ownership. These tests use owned synthetic
memory. They **do not execute a positive live Capture or native row writer**;
the separate original-writer ABI audit covers those call thunks offline.

Both private Gold/History runtime-9 adapters compile with the existing invocation
and ABI checks, and hold the original Start event as before. The experimental
capture runs only after the original-writer diagnostic checkpoint has confirmed.
It writes local diagnostic hashes and supplies no world permission.

The first DXVK pair attempt was refused before any game or child runner started: only
3,521 MiB physical RAM was free against the unchanged 4-GiB preflight requirement.
Read-only resource inspection found a large Vite server and active Vitest workers.
An intended reversible working-set trim was cancelled when the Vite process
became busy on its identity/CPU recheck. No process was stopped, suspended or
trimmed.

After the parallel load subsided, a fresh sample showed 14,635 MiB available.
The coordinated retry (pair 76) passed its launch preflight with 11,508 MiB free
physical RAM and 26,509 MiB commit headroom. Both games reached the native common
roster and the original History map reload. During the run, host free physical
memory then fell to 578 MiB. The monitor recorded pressure 2/recovery required and
the runner closed the broker before Ready/checkpoint/active-row capture completed.
It recorded no monitor failure: History peak private memory was 770 MiB, with at
least 2,536 MiB free process address space and a 2,045-MiB largest free block.
This does not establish a defect in the new capture, which was never reached.

Both games closed normally with exit code zero. The runners verified restored
History options/adapter/Interface, unchanged saves/cloud/shared profile files,
and restored Gold entry point/shared files. Fresh window inventory showed no S7
window. The test slot was released to the separately coordinated Standalone chat.

The pending positive test must establish actual capture on Gold and both History
data sources, then examine genuine row differences. This is a point-in-time
diagnostic, without an atomic native setup lock or continuous mutation watch.
It does not capture every preparation/root/content dependency or qualify the
actual fresh-start format path. Full setup/bootstrap/content agreement, native
common world start, synchronized turns, Gold NEO and mixed save/cold reload remain
unimplemented or unqualified. Original Steam-executable runtime qualification is
separate from both History data sets on the isolated Ubisoft runtime.

See the [sanitized candidate receipt](verification/gold-history-active-capture-20261112.json).
