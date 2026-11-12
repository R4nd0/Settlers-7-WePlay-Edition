# Gold / History diagnostic setup checkpoint

The experimental adapters now confirm the original rules/template-writer
snapshot through an authenticated challenge, acknowledgement and confirmation.
Actual DXVK High pairs passed with both History data sources on the isolated
Ubisoft History standalone runtime. No Steam SDK was loaded. This is a
diagnostic checkpoint; no mixed world has started.

`native/crossplay_setup_checkpoint.h` binds one revision, reciprocal channel
identities, writer lengths/digest and two fresh 128-bit challenges. Each frame
is exactly 120 bytes. The complete exchange has a ten-second deadline, which
cannot be extended by traffic. Rebinding, repeated/out-of-order messages,
changed snapshots, clock rollback and expired intervals fail closed. Send
phases advance only after the caller reports successful channel queuing.
Host confirmation means its final message was queued; guest confirmation
additionally requires receiving that message. Both results were observed in
the actual pairs.

The opt-in native wrapper uses Windows system randomness and recaptures the
original writers before each protocol action and final confirmation. It also
checks the owner thread, native session, preparation storage and configuration
cache. Its local fingerprint retains the preparation-buffer bytes, rules-root
storage, all eight cache rows and admitted roster fields. Two captures must
agree around each original-writer invocation. Native pointers and unknown
fields are retained locally, rather than normalized between editions. No raw
native storage, pointer values or roster names are sent in checkpoint frames.

This is an immutable diagnostic snapshot, not a UI lock. Callers must recheck
it when consuming the result and retire it when the session closes or setup
changes. The wrapper does not continuously watch memory, recursively capture
all pointed-to objects, compare the active multiplayer configuration between
editions, or qualify the gameplay-content dependency closure. It cannot
produce a fresh-start agreement or release a held start event. Equal original
writer output still excludes important active player options and content;
see the [content audit](GOLD-HISTORY-CONTENT-AUDIT.md).

Verification used MSVC x86 `/W4 /WX`: 5,916 checks covering full transcripts,
every-byte frame corruption, lengths, challenge replay, invalid ownership,
state changes, deadlines and send ordering. The complete public crossplay
build also passed; adjacent fresh-start, closed-slot and writer-signature
fixtures passed another 665 checks. These are synthetic protocol
checks; the separate real pairs observed original Ready, map reload,
[closed-slot consumers](GOLD-HISTORY-CLOSED-SLOTS.md), subsequent team values
and both checkpoint confirmations. The native code guards matched independent
read-only inspection. Each pair relayed 20 records with no rejection during
the functional exchange; one rejection occurred during normal disconnection.
All clients exited normally and profile, save, cloud, adapter, Interface and
shared-file restoration checks passed.

Two earlier attempts used incomplete laboratory build flags: the Gold local
login variant could not create a lobby, and the next History variant omitted
the Directory/Lua bridge needed by the guest UI. Neither reached this
checkpoint. The corrected private build records its invocation and rejects
these incomplete configurations. These attempts are retained separately from
the successful pairs.

The [verification receipt](verification/gold-history-checkpoint-20261112.json)
records source and binary fingerprints, results and limits. Ordinary adapter
builds and public releases remain unchanged. Common world initialization,
command/turn delivery, Gold NEO and mixed save/reload remain unqualified.
