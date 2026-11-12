# Gold / History closed-slot event bridge

The experimental bridge now carries the original closed-slot notifications for
unused Monhaim positions 3 and 4. Real paired tests now confirm original
factory/queue delivery and native cache adoption with both History data sources
on the isolated Ubisoft History standalone runtime, using DXVK High. It is
excluded from ordinary adapters. No mixed world has started.

## Why another configuration event was insufficient

The original History configuration handler for subtype 25 adopts the map; it
does not copy the incoming player-slot vector into its configuration cache.
In an earlier real pair, Gold closed positions 3 and 4 while History retained
open positions despite receiving that configuration. Equal setup-writer hashes
did not detect the difference because those writers omit these cache fields.

The original `CChangePlayerSlotEvent` subtype 55 handler performs the required
transition. It sets the selected position to closed, Ready and assigned to its
index, clears privacy and station/participant references, and marks the cache
dirty under the original lock. The bridge uses that event and its original
factory, reader, writer and local-only clone queue. It never writes cache fields
or calls an internal branch directly.

## Bounds and ownership

`native/crossplay_closed_slot_codec.h` accepts only the original class, event
type, asynchronous flag, admitted host source and positions 3 or 4. Its 61-byte
frame binds the reciprocal peer identities and strictly ordered revisions 1
and 2. Only the source connection ID changes between native namespaces. The
enclosing authenticated channel supplies transport authentication.

Native delivery requires the owned two-human Monhaim preparation interval,
actual Ready states, valid original cache fields, and the expected complete
eight-position state before each event. Position 3 must finish its native
transition before position 4 can be delivered. Closed rows must also have a
zero participant reference. The original factory output must consume all bytes
and reserialize identically before cloning; the temporary is then destroyed.

The caller must bound the interval and deadline, retain session/cache ownership,
and observe native postconditions before following team notifications. These
requirements are exercised by a separate private laboratory integration; they
are not supplied by the framing helper alone. Complete content/setup agreement,
acknowledgements and world-start coordination remain separate work.

History integrity checks cover the complete subtype 55 path: original dispatch
prologue, selector/jump tables, selected branch, shared clearing tail, dirty flag,
return and index getter. Other event branches retain their existing adapter
guards. An earlier full-function fingerprint rejected WePlay's own separately
guarded resume-fragment changes in an unrelated branch. The corrected guard
uses immutable ranges without byte masks; offline execution checks every
executed original instruction against those ranges.

The [fresh-start policy](GOLD-HISTORY-START.md) now recognizes the original
closed state at positions 3 and 4. It still rejects open or AI rows, misplaced
closed positions, non-Ready humans and stale station/participant references.
This policy does not grant permission to start a world.

## Verification

The x86 Windows `/W4 /WX` fixtures pass 151 framing/ordering/exclusion checks
and 328 fresh-start checks, including 64 new cache-policy cases. The compiled
closed-slot integrity oracle passes 129 checks against the three pinned
analysis profiles, including mutated and unreadable ranges.

`tools/gold/verify_closed_slots.py` runs 16 original native visitor/consumer
checks: writer and reader for both positions in all three profiles, plus the
complete original configuration dispatch and index getter for each History
profile. It checks the exact cache changes, event preservation, lock calls,
stack and nonvolatile registers. Objects and streams are synthetic and Windows
critical sections are modeled. This does not exercise a live host factory or
real queue consumer. The fresh-start integrity oracle still passes 69 checks.

In two independent real pairs, History first adopted Gold's Monhaim map from
an original Tower Island lobby. Both admitted humans became genuinely Ready.
One Gold Start click produced two authentic closed-slot events and two team
notifications; the original fresh-start event was captured and held. History's
original factory/reader/writer/local-only clone path delivered each closed-slot
event once, and its original cache consumer completed each transition before
the next delivery. Both subsequent team values were observed in preparation.

Independent read-only process inspection confirmed matching states, readiness,
assignments, privacy and station values across all eight cache rows; closed
positions also cleared their participant references. The guarded original code
matched its pinned analysis ranges in both running clients. The original
2,152-byte rules and 41-byte player writers produced equal post-transition
digests. Those writers omit cache fields and do not grant world permission.

One pair used Steam History data and the other Ubisoft History data; both used
the same isolated Ubisoft History runtime and loaded no Steam SDK. This does
not qualify the original Steam executable. The History page still displayed
the unused rows as `Freies Feld`; the native cache, rather than that label,
establishes the closed state. Both clients exited normally in each pair,
restoration passed, and both memory summaries reported no pressure/recovery.

Earlier attempts were stopped by host memory pressure or the full-function
fingerprint exclusion described above. They are not positive runtime evidence.

See the [sanitized receipt](verification/gold-history-closed-slots-20261112.json).
Common world start, synchronized turns, Gold NEO and mixed save/cold reload
remain unqualified.
