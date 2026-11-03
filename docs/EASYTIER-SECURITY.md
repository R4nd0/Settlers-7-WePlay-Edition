# Managed EasyTier connection boundaries (RC3.13)

The public lobby and private invitation rooms share the managed EasyTier
transport. Public listing is discovery; it does not itself establish a tunnel.
Each new connection requires the launcher's unchecked consent box, including
an in-game request to switch rooms. Cancel creates no connection. Consent is
not retained as a permanent preference. The backend independently rejects
connection requests without the explicit consent field.

The explanation covers the temporary virtual adapter, encrypted peer traffic,
public rendezvous and possible relay use, visibility of connection addresses,
and remaining game/runtime/Windows and denial-of-service risks. Private rooms
are not published by their launcher path. Public hosting requires a separate
publisher link; browsing uses the reader link.

## Tunnel policy

The pinned, modified runtime uses enabled input, output and forward chains
with default deny. Input is limited to the room subnet and this peer's exact
overlay address. Output is limited to this peer and the room subnet. Forwarding
has no allow rules. IPv6, exit node, DNS changes, UPnP, KCP/QUIC proxy inputs
and relay options are disabled. Explicit configured routes remain limited to
the room subnet rather than accepting arbitrary peer-advertised proxy routes.

| Overlay traffic | Allowed destination |
| --- | --- |
| UDP 3074, 6200, 9103 | Game traffic inside the room |
| UDP 9111, 9112 | Lobby and discovery inside the room |
| TCP 9110 | Room coordinator only |

Replies use the existing runtime's reverse-flow tracking and bounded output
source-port rules. No inbound ephemeral port range is opened. The strict ACL
patch rejects malformed IPv4/TCP/UDP lengths, all fragmented IPv4 and packets
that cannot be classified; it does not treat an unparseable packet as allowed.
The production build includes `tun,aes-gcm,smoltcp`, without the optional
SOCKS/port-forward feature used by the private adversarial test sender.

Room secrets and invitations are credentials. Encryption and knowing a room
secret do not establish that a peer is trustworthy. There is no individual
transport credential revocation: close and recreate the room if necessary.
Do not use untrusted custom EasyTier configurations with this launcher.

## Windows rules and process boundary

Six inbound program rules are installed: the game's three fixed UDP ports,
the .NET coordinator TCP 9110 and UDP 9111, Python discovery UDP 9112, and the
EasyTier core's outer TCP 11010 and UDP transport. Game/service rules also
restrict the virtual interface, local overlay address and remote room subnet.
The core needs varying outer UDP ports for NAT traversal; that rule is scoped
to its pinned executable, rather than granting arbitrary applications access.
Old owned rule names are removed before applying the narrower rules.

Management RPC binds a random loopback-only port with a loopback whitelist.
The launcher verifies bundled runtime hashes and strips CLR/runtime injection
environment settings from managed helper launches. It retains existing native
executable, callsite, object and ABI guards. Stopping the managed room removes
its owned rules and adapter. No blanket disabling of Windows Firewall occurs.

## Evidence and limits

The actual-TUN test used the shipping receiver and an unrestricted private
sender with no ACL. TCP 9110 and UDP 3074 reached controlled listeners from the
virtual peer. Controlled TCP 27445 and UDP 27446 did not, and SMB/RDP port
probes were blocked. A temporary broad test application rule ensured those
negative results were not merely Windows blocking the controlled listeners.
The final helper's six production rules were inspected separately. All test
rules and the owned adapter were removed afterward.

Two actual local Ubisoft clients also completed public discovery, encrypted
room delivery, a guest restart and native lobby admission with two players.
That game harness used guarded loopback; it did not play a world over TUN.
The private directory used the production Worker routes with mocked Discord.
No test game was posted to real Discord. Steam's global UI flow and two remote
PCs were not live-tested in this run, and no long match/desync claim follows.

These boundaries reduce exposure; they do not guarantee freedom from game or
runtime defects, malicious game-protocol traffic, resource exhaustion or
disconnects. Use the same release on every peer. See the sanitized
[verification record](verification/rc313-lobby-security-20261103.json) and
[modified runtime build instructions](../tools/easytier/README.md).
