# Original Gold / History turn batches

The opt-in `crossplay_turn_batch.h` and native codec carry captured original
`CEndOfTurnBatchEvent` messages. This primitive does not carry gameplay commands
or establish a synchronized world. Ordinary release adapters exclude it.

The original serialized batch contains a scalar and a bounded vector of inline
end-of-turn events. Each nested event contains five opaque scalar values. All
turn/checksum bits, original types and asynchronous flags remain unchanged.
Only outer and nested source CIDs are projected through the independently
admitted peer. Foreign nested identities, malformed types/flags, oversized counts,
trailing data and replayed or wrapped channel sequences are rejected. The caller
must retain the authenticated channel and verified original world-start interval.

The native codec requires the owner thread, exact phase-2 session, two confirmed
admitted participants, original event/vector layout and bounded readable storage.
Method guards cover both original event vtables, all twelve method prefixes and
the complete class getters and serializers. The original factory/reader creates
the received event; exact consumption and byte-identical original reserialization
precede the original local clone sender and destructor. FP state is preserved.
No turn, command, native acknowledgement or checksum is synthesized.

Headless verification runs the original serialization bodies and vector
resize/constructors with preallocated synthetic storage. It passed 24 original
writer cases, 72 cross-profile reader cases, 144 malformed-input rejections,
15 vector-boundary checks and 56 compiled method-guard checks per edition.
Framing fixtures separately preserve every opaque scalar while projecting only
CIDs, reject all truncated lengths, and enforce identity/sequence bounds.
Fixed and relocatable x86 builds and unowned-call rejection passed. See the
[verification receipt](verification/gold-history-turn-batch-20261113.json).

These fixtures run no native heap, factory, sender or game process. Diagnostic
Gold and original Steam adapters compile with their original entry/isolation
guards, but native turn-batch delivery and synchronized gameplay remain under
runtime investigation. The [loading milestone](GOLD-HISTORY-LOAD-PROGRESS.md)
established bidirectional original progress to 100%; it did not clear both native
loading barriers. Gold NEO and mixed save/cold reload remain unqualified.

Headless tests remove desktop-focus and rendering requirements for format, ABI
and bounds checks. A fully automated native game startup or renderer-free game
mode has not been established; real world/graphics/logistics qualification still
requires the actual game clients.
