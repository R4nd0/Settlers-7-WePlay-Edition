# Gold / History fresh-start rendezvous

The opt-in `crossplay_fresh_start_barrier.h` coordinates one original fresh-start
event after both participants have verified the same fresh native setup. It is
excluded from ordinary game adapters. Successful admission, a setup reader test,
or a queue receipt does not establish a running mixed game.

Each participant first sends an authenticated receipt binding the two channel
identities, revision, complete native setup digest, actual diagnostic content
contract, and host/guest role. The host then forwards its captured original
21-byte asynchronous start event. The guest projects only the admitted host CID
and attempts its original local clone sender once. Only a receipt that this send
returned permits the host's own single attempt. There is no retry after an
attempt, invalid receipt or native failure. The rendezvous expires after ten
seconds; caller-side original method, owner-thread, object and fresh-setup guards
remain required before invoking either native sender.

The content contract identifies the actual edition-specific input archives,
including known differences. It is not a declaration that their payloads are
identical or compatible. In particular, the different global-script startup
remains unresolved. Diagnostic integration must preserve native checksum/error
handling and must not reuse the earlier synthetic lobby-only content constant.

The compiled Windows fixture passes the successful ordering path, mutations of
every byte of both 112-byte receipts and the 97-byte start frame, and negative
checks for replay, malformed native events, wrong phase, empty contracts and
backward/expired time. These are protocol tests: no native game or world executes
inside the fixture. See the [receipt](verification/gold-history-fresh-start-barrier-20261113.json).

The private Gold and original Steam diagnostic adapters also compile with their
original initialization and forwarding guards. The original Steam image passes
all eleven image tests, and its adapter still rejects an ordinary host before
native initialization. The first live attempt was stopped by the existing RAM
preflight before any game launch or deployment. Shared world start, mixed turns,
Gold NEO and mixed save/reload therefore remain unqualified.
