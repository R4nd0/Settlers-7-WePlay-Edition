# RC3.20 public-lobby correction

## Missing host registration

`launch_auto_lan.run` calls its public observer after membership/content checks,
before `before_game`. RC3.18 delayed the external `invitation_ready` callback
until `before_game`. Windows and Linux still used that callback to populate a
shared room slot consumed by the earlier observer. A fresh public host therefore
created a reporter with no `HostBroker`. Reports alone cannot appear in the
joinable directory: a matching admitted host lease is required.

The Internet orchestrator now supplies `room_invitation` directly to its internal
observer. Windows and Linux require it when constructing a joinable public host.
External readiness keeps its later timing, so unreachable/incompatible rooms
are not advertised as ready. No invitation secret is added to public metadata.

## Expiring active credentials

The server accepted an existing public credential without changing its expiry.
The reporter renews every 60 seconds, but maintenance could still remove its
identity after 300 seconds. Renewal now atomically updates `expires_at` only for
the matching, unexpired public identity/token hash. Creation rate limits and
expiry after hosting stops remain in effect; no schema change was needed.

## Verification scope

- `tests/test_public_startup.py`: actual Windows/Linux entrypoints and nested
  Internet/LAN orchestrators. Machine boundaries and the native process are
  fixtures. Frozen RC3.19 fails both host cases because `broker` is absent;
  corrected source passes, including preflight/registration/coordinator failures.
- Worker Node tests use SQLite; the renewal test advances 12 minutes, invokes
  maintenance before each renewal, requires the same game to remain visible,
  then verifies expiry after stopping. The old Worker fails this regression.
- Additional private integration exercised running workerd/D1, both launcher
  startup paths, the in-game snapshot format, an independent reader, full-room
  refusal, compiled CNG helpers, encrypted join and removal after host close.
  Native process/admission boundaries were fixtures in this composition.
- Native local observation used two guarded game copies. A real native host
  was published through the corrected Windows observer/broker path to the live
  HTTPS directory. The guest browser displayed it; explicit selection and normal
  guest exit produced a nonce-bound request. A separate compiled crypto client
  received the exact room identity. The directory bridge's pending selection is
  consumed on `close`, not while it runs; an initial private harness waited at
  the wrong point, so that selection was validated by a separate completion check.
  Both native clients exited normally and their copies/profiles were restored.
- The server renewal change was bundled and deployed using the existing Worker
  and database. No synthetic games or Discord messages were posted to production.

The native check proves discovery and encrypted handoff on one PC. It does not
qualify two-PC Internet connectivity, TUN routing, relay traversal or Linux game
rendering. Those limits are separate from the reproducible missing-listing bug.
