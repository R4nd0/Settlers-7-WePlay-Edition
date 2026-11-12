# Native Gold to History castle positions

The experimental preparation path now applies genuine Gold host position
notifications through History's original castle-position setter. Actual DXVK
High pairs using Steam History data and Ubisoft History data both showed the
two admitted humans swap A/B in their native preparation records. History's
original guest lobby displayed the resulting positions. A second change in
the Ubisoft-data run swapped them back. Guest Ready and the original native
rules/player signatures subsequently agreed at both ends.

Both History data sets ran the same isolated standalone Ubisoft executable
without a Steam SDK. The original Steam executable was not launched: its native
setter profile was checked offline. Both pairs exited normally, restored their
options/adapters/Interface, and left saves, cloud files and shared profiles
unchanged. The Steam-data run recorded memory pressure; this establishes bounded
lobby functionality, not a stable long match. Neither pair started a world.

## Original setter and guarded ownership

`CPlayerStartPositionChangedEvent` has class `0x0ea630cc`, type `0x00310007`.
The laboratory transport retains its original 18-byte serialization and native
factory/clone path. History's original event consumer only invalidates lobby
presentation, so the owned preparation record also needs the original setter.

`crossplay_history_position_codec.h` exposes `ProjectHistoryCastlePosition` for
an already authenticated and originally enqueued host notification. It calls
Ubisoft `0x5863e0` or Steam `0x585ea0`. Each calls its original lookup helper
(`0x585c20` / `0x5856e0`), searches two native player-index lists and, when the
requested castle is occupied, swaps the former occupant into the target's old
position. A direct write of only the target field would lose that behavior.

The complete 87-byte setter and 121-byte lookup helper are guarded together.
The existing admission, native method, preparation identity, session/backend,
owner-thread and Monhaim-map checks remain prerequisites. The two root pointers
of the configuration must reference the same owned row buffer.

`crossplay_preparation_positions.h` reads the two original list sentinels and
at most 25 nodes in total. It rejects unreadable/overflowing addresses, broken
backlinks/tails, cycles, repeated nodes or player indices, foreign map indices,
duplicate occupied castle positions, missing admitted humans and malformed
four-slot preparation data. It records all 25 position bytes and the exact list
structure. Before the native call it rereads the snapshot and ownership roots;
afterward it verifies the complete predicted swap and unchanged list structure.
Only changed position fields need to be writable; it never writes them itself.

## Scope and remaining work

The projection is limited to the two admitted human target slots on Monhaim.
Its native bounds accept castle IDs 1–4; actual transport/UI qualification here
covers the occupied-human A/B swap and its reverse. Changing into a neutral/AI
slot, guest-authored requests, other maps, 25-player maps, saved worlds and
active-world mutations are unqualified. The diagnostic caller retires the path
on failure and must not replay an already enqueued event.

The public Color/Team receiver and this position projection were compiled into
fresh private third-candidate adapters; ordinary adapters include neither path.
The original template-player signature changed after the swap and agreed across
the pair. It still omits Color and the complete admitted roster. A fresh complete
content/setup agreement and native Start/turn bridge remain necessary. This
change does not implement Gold NEO logistics, mixed saves or cold reload.

## Verification

`native/build_crossplay.ps1 -OutputDirectory <fresh-work-folder>` includes fixed
offline-analysis and relocatable Windows position test executables. The focused
build passed MSVC x86 `/W4 /WX`; the Windows process exited 0 after 115 list,
swap, malformed-data, failure-clearing and exclusion checks.

`tools/gold/verify_position_guards.py` takes the two owned pinned analysis images
and fixed `crossplay_position_tests.exe`. It executed 16 compiled guard checks
and 12 original setter/helper calls: human swaps, AI swaps, free positions,
identity no-op and invalid native player exclusion. Native lists/rows are
synthetic and `VirtualQuery` is modeled. This offline test does not execute the
high-level ownership gate, event factory, game process or a world. Their bounded
positive preparation projection has the separate actual-pair evidence above.

See the [sanitized verification receipt](verification/gold-history-positions-20261112.json).
