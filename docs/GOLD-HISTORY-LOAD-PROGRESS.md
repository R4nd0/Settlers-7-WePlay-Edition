# Original Gold / History loading progress

`crossplay_load_progress.h` frames only the original `CLoadProgressEvent`.
The observed native type is `0x000a0007`: subtype 7 and category 10. Its factory
class identifier is independent. The 17-byte original stream contains the class,
type, source CID, asynchronous flag and single-precision progress bits. Projection
changes only the admitted peer CID. Progress and completion values are never
invented, rounded or substituted.

The authenticated channel remains responsible for transport authentication.
Framing binds both channel identities, exact lengths and a bounded increasing
sequence. Replays, sequence wrap, malformed flags, nonfinite/negative values and
values above 100 are rejected. Negative zero is preserved exactly. Forwarding a
valid value does not establish that either world has completed loading.

The opt-in native codec checks owner thread, original method signatures, event
vtable, exact type, admitted source and a two-member confirmed native session.
`ParticipantEvent::CaptureStartedSession` separately requires native phase 2;
`CaptureLobby` still strictly requires phase 1. Callers must bind the current
verified session after native map reload and before the original start send.
The admission session pointer can be stale after History reloads its map.

The original writer serializes outgoing messages. The receiver uses the original
factory and reader, checks exact consumption, reserializes for byte equality,
rechecks session ownership and invokes the original local clone sender. The
original destructor releases the decoded event. The codec preserves x87, SSE and
MXCSR state; integration must also preserve FP state around its outgoing observer.
Local-only delivery must not be forwarded again.

Bounded offline execution passed 30 original writer cases, 90 cross-profile
reader cases, 180 malformed-input rejections and 29 compiled method-guard checks
per edition. Windows fixtures passed framing/replay/numeric checks, rejection of
unowned native calls and FP-state preservation. These checks execute serialization
bodies with synthetic storage; they do not execute a native factory or local
sender inside a running game. See the [verification receipt](verification/gold-history-load-progress-20261113.json).

Actual DXVK diagnostics reached original start delivery and visible 3D world
construction on Gold and original Steam History. Initial load forwarding failed
closed on the phase, type and stale-session mistakes above. The corrected DXVK
run delivered all 11 Gold progress values to History and all 22 History values
to Gold through the original local clone sender. Both received sequences match
the source progress bits exactly and end at the original value 1.0. History now
shows its peer at "Blocking 100%"; Gold retains its loading overlay. Both games
exited normally and the isolated files/profiles were restored.

The native loading barrier has not exited on both clients. Synchronized turns, Gold NEO and
mixed save/cold reload remain unqualified. Original checksums and failure handling
remain active. Ordinary release adapters do not include this diagnostic relay.
