# Internet room readiness correction, 2026-11-06

The published RC3.16 client still pins `tcp://public.easytier.cn:11010`.
During an actual failed invitation attempt that hostname returned NXDOMAIN,
while unrelated DNS resolution worked. The host had a working local tunnel
adapter but its peer list contained only itself. The previous host readiness
condition accepted that local adapter without any established external path.

New source replaces the endpoint with `tcp://38.76.179.190:11010`, listed in
[EasyTier's relay registry](https://github.com/EasyTier/etcat/blob/main/relays.toml).
It retains the fixed assisted endpoint allowlist, private encrypted room,
scoped game/lobby ACL and numeric-only direct endpoints. The relay is a
community service; availability is checked at runtime rather than assumed.

An assisted host now requires its configured connector to report an established
EasyTier handshake. The pinned 2.6.4 API defines CONNECTED=0, DISCONNECTED=1 and
CONNECTING=2; unknown statuses, unexpected endpoints and malformed or duplicate
entries fail closed. This status is checked again in subsequent host snapshots.
An unavailable relay produces a specific error instead of blaming Windows
permission. Guests still require the actual room coordinator in their peer list.

Tunnel startup reports **connection established, checking room**. The launcher
publishes the invitation and **room ready** only after the normal coordinator
membership and content/configuration checks have succeeded, immediately before
starting the game. A failed coordinator or preflight publishes neither.
Direct mode retains its existing behavior and does not claim an independent
test of public inbound reachability before a remote participant joins.

## Verification and deployment

Two isolated instances of the exact bundled Windows core established handshakes
to the replacement server and discovered each other's private virtual address
through `relay(2)`. The probe disabled TUN, listeners and hole punching; it made
no adapter, route or firewall changes and closed both owned processes afterward.
This establishes real relay rendezvous, not an actual Internet S7 match or
separate-PC qualification. The running LAN game was left unchanged.

Regression coverage checks adapter-only false readiness, connecting and
disconnected states, malformed connector replies, loss after connection,
guest coordinator requirements, secret-free status queries and delayed
invitation/UI readiness. The affected Windows Internet suites pass 47 tests.

This correction is included in RC3.18. It does not change an already running
installation. All participants need a package containing this change
and a newly created invitation. Existing assisted invitations point to the
retired server and are not silently redirected. Older release tags and assets
remain unchanged.
