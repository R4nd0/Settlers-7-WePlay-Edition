# Internet play with integrated peer to peer networking

Research date: 2026-10-29. This is an implementation handoff for WePlay. The
user requested research and notification of the networking chat, including free
alternatives. Installing a VPN, creating accounts or networks, and implementing
Internet mode are outside this research milestone.

## Recommendation

The user clarified that the solution should be serverless and directly
integrable. **Evaluate a bundled EasyTier helper first** for that requirement;
the next section distinguishes direct-only operation from public bootstrap and
relay infrastructure. This is an integration candidate, not a tested WePlay
transport.

For the earlier separate-client approach, **externally installed ZeroTier One
and a private RFC1918 IPv4 network** remain an option. Its free Personal plan covers
10 devices and one network for personal, non-commercial use. This is the
short candidate path through the existing address guards. It still requires
adapter selection and a real Internet test; installing ZeroTier alone does not
establish working WePlay multiplayer. [ZeroTier pricing](https://www.zerotier.com/pricing/)

For a free Windows gaming alternative, **Radmin VPN** is credible, but its
26.0.0.0/8 addresses are rejected by the current launcher and native adapter.
For an independently operated, open-source solution, **SoftEther** provides
Ethernet VPN support and automation, at the cost of running a server.

The known simulation desync remains a separate defect. Changing the tunnel
does not demonstrate a fix. See [DEVELOPMENT-HANDOFF.md](DEVELOPMENT-HANDOFF.md)
and [NETWORK-RELIABILITY.md](NETWORK-RELIABILITY.md).

## GitHub candidates for an integrated serverless mode

**Serverless has two materially different meanings.** With no dedicated server
operated by WePlay, public bootstrap/STUN nodes and fallback relays may still be
used. With no external infrastructure at all, players need a reachable entry
point and a way to exchange its address. Arbitrary restrictive NAT/firewall
combinations cannot be promised to connect directly. ICE explicitly uses STUN
and TURN to obtain candidate paths, including relay paths.
[ICE standard](https://www.rfc-editor.org/rfc/rfc8445.html).

| Project | Integration choice | Infrastructure condition |
| --- | --- | --- |
| [EasyTier](https://github.com/EasyTier/EasyTier) | Windows-capable mesh VPN, LGPL 3.0; first evaluate its core executable bundled and controlled by our launcher | Connect to a reachable player for direct bootstrap, or use community shared nodes for discovery/NAT traversal and relay fallback. No mandatory WePlay-operated server or Central account. |
| [EasyTierGame](https://github.com/EasyTier/EasytierGame) | Existing Windows game launcher bundling EasyTier; useful process/configuration reference | Inherits the underlying network's dependencies. Its WinIPBroadcast helper also shows why game discovery must be tested separately. |
| [libjuice](https://github.com/paullouisageneau/libjuice) | C library, Windows support, MPL 2.0; ICE-based UDP connectivity for a custom embedded transport | Requires candidate/signaling exchange; STUN and optional TURN help with NAT. Not a VPN or a replacement for all existing sockets. |
| [rustp2p](https://github.com/rustp2p/rustp2p) | Rust library with QUIC streams/datagrams, discovery and NAT primitives; more custom bridging work | Needs reachable bootstrap peers and configured STUN where required; reachable peers can relay. Production certificate verification must replace the documented testing default. |
| [n2n](https://github.com/ntop/n2n) | Peer VPN with Windows build support | A publicly reachable supernode is explicitly required for discovery and fallback relay, so it does not meet the strict infrastructure-free requirement. |

Sources: [EasyTier README](https://github.com/EasyTier/EasyTier/blob/main/README.md),
[EasyTierGame README](https://github.com/EasyTier/EasytierGame/blob/main/README.md),
[libjuice API and scope](https://github.com/paullouisageneau/libjuice/blob/master/README.md),
[rustp2p architecture and defaults](https://github.com/rustp2p/rustp2p/blob/master/README.md),
[n2n topology](https://github.com/ntop/n2n/blob/dev/README.md).

### Proposed EasyTier integration

WePlay would own a pinned `easytier-core` helper's start, readiness, cancellation
and cleanup. Players would use a shared invitation instead of a separate VPN
GUI. Generate a strong group secret; keep it out of command-line diagnostics,
logs and published artifacts. Use a locally protected configuration file and
restrict management RPC to loopback. Do not enable exit-node, subnet-export or
remote web-management features for game connectivity. Explicit private IPv4
addresses and a dedicated adapter name are configurable. Current documentation
also describes a `p2p-only` flag, which restricts communication to established
direct peers; verify flags against the pinned release before implementing them.
[Configuration reference](https://easytier.cn/en/guide/network/configurations.html).

Offer two explicit connectivity policies: direct bootstrap to a reachable player
with no public shared node, or public-assisted connection with a disclosed
bootstrap/relay dependency. Direct-only mode must report an unreachable peer
instead of quietly falling back to a relay. A player-hosted coordinator is
still needed by WePlay itself; bundling a P2P tunnel does not remove it.
[Decentralized setup](https://easytier.cn/en/guide/network/decentralized-networking.html).

The bundled helper approach retains normal Windows sockets for the game,
Python and .NET. Windows release packages include driver/DLL dependencies;
admin rights for a virtual adapter remain a deployment requirement. Record
exact artifact hashes, architectures, LGPL source/notices and the separate
driver licenses before redistribution. Current stable releases and current
`main` documentation need not expose identical interfaces.
[Windows packaging](https://github.com/EasyTier/EasyTier/blob/main/README.md),
[Release examined](https://github.com/EasyTier/EasyTier/releases/tag/v2.6.4).

A library-only design with libjuice or rustp2p needs bridges for discovery,
TCP control, service UDP and native game UDP, as well as authenticated group
membership and payload encryption. In particular, ICE connectivity by itself
does not encrypt application payloads. This is more invasive than bundling a
VPN core. An EasyTier FFI tree exists, but neither its existence nor an embedded
device component establishes stable Windows support or equivalent broadcast
behavior for this game. Prove those properties before choosing a DLL approach.
[Native FFI tree](https://github.com/EasyTier/EasyTier/tree/main/easytier-contrib/easytier-ffi).

The existing adapter-selection, RFC1918, firewall and native exact-peer guards
below still apply. Verify directed broadcast over the chosen EasyTier release;
if unsuitable, implement a bounded unicast directory using validated overlay
peers. Do not install a global broadcast repeater or disable the firewall as a
substitute. No candidate has been run against S7 in this research milestone.

## Free alternatives and project fit

| Option | Current free offer | Fit for WePlay |
| --- | --- | --- |
| ZeroTier One with Central Personal | 10 devices, one network, personal non-commercial use; hosted management API excluded | Separate-client option: configurable private IPv4 addresses and Ethernet broadcast support. Add explicit overlay selection. |
| Radmin VPN | Vendor states completely free, no ads or paid features, no gamer-count limit; Windows | Good external gaming tool. Current RFC1918 guards reject its 26/8 network. Needs deliberate overlay support across layers. |
| SoftEther VPN | Apache 2.0, free personal and commercial use | Good candidate for an independently operated Ethernet network. Requires VPN Server plus clients and server administration; hosting resources may cost money. |
| Tailscale Personal | Six users, unlimited user devices | Good IP connectivity, but a different fit: its layer 3 model does not supply the Ethernet LAN semantics assumed by current discovery. Standard 100.64/10 addresses also fail current guards. Plan unicast discovery and overlay-aware validation first. |
| Hamachi | Up to five computers per network | Smaller free group than ZeroTier. Its common 25/8 addresses would also fail the present RFC1918 restriction; not the preferred integration path. |

Sources: [ZeroTier](https://www.zerotier.com/pricing/),
[Radmin](https://www.radmin-vpn.com/),
[Radmin network and firewall documentation](https://www.radmin-vpn.com/help/),
[SoftEther](https://www.softether.org/),
[SoftEther license](https://www.softether.org/5-download/src),
[Tailscale pricing](https://tailscale.com/pricing),
[Tailscale network model](https://tailscale.com/docs/concepts/tailscale-osi),
[Hamachi](https://vpn.net/),
[Hamachi mesh address range](https://support.logmeininc.com/nl/hamachi/help/setting-up-an-android-device-as-a-hamachi-mobile-client-hamachi-t-hamachi-mobileclient-android).

Free use of a separate VPN client, redistribution of that client, embedding an
SDK, and operating a community service are different choices. Do not infer a
right to bundle or rebrand software from its free download. For Radmin, the
first useful integration can guide installation and recognize its adapter;
automatic account/network control needs a supported vendor interface to be
established before it becomes a product promise.

## Existing code and concrete gaps

The source inspection used the current working tree on `main`, whose HEAD was
`a02e908` at inspection. Other chats have unfinished installer/UI changes in
this shared checkout. This milestone adds this document only.

| Location | Existing behavior | Required Internet work |
| --- | --- | --- |
| `tools/installer/lobby_directory.py` | RFC1918-only addresses; interfaces /8 through /30; endpoint must be in the selected subnet | Configure ZeroTier with a non-overlapping RFC1918 subnet and supported prefix. Keep canonical-address and source checks. |
| `tools/installer/lan_interfaces.py` | Enumerates active IPv4 adapters; physical default-route adapters sort first | Match the chosen ZeroTier network to its actual adapter and assigned address. Adapter name alone is insufficient evidence. |
| `tools/installer/launch_auto_lan.py:103` | Creates a new coordinator on `interfaces[0]` when none is found | An Internet starter must use the selected overlay, or the first player may host on Ethernet/Wi-Fi where remote friends cannot discover it. |
| `tools/installer/lan_discovery.py` | Directed subnet broadcast on UDP 9112, reply source validation, per-socket source/interface binding | Exercise discovery over the overlay in both directions. Scope search to the chosen network so unrelated physical LAN groups do not win. |
| `tools/installer/lan_autoconnect.py` | Stable election, active-group priority, bounded status checks, partition rejection | Verify simultaneous Internet starts, slow/relayed peers and interrupted discovery. A missed beacon must not silently create competing groups. |
| `tools/installer/Configure-LAN-Firewall.ps1` | Exact-copy executable rules on Private profile and LocalSubnet only | A Public-profile VPN adapter can remain blocked. Add owned rules scoped to the selected adapter, overlay addresses and required programs, or explicitly configure that adapter's profile. |
| `tools/offline-ui/launch_local.py` and `native/lan_address.h` | Reject non-RFC1918 peer addresses, including 26/8 and 100.64/10 | ZeroTier can satisfy the existing guard. Radmin/Tailscale require a separate reviewed overlay policy, native rebuild and equivalent service/runtime checks. |

Do not simply accept every public address or allow an entire 26/8 or 100.64/10
range as game peers. Preserve admission leases and exact-peer authorization.
For a future overlay policy, validate membership, the actual local adapter and
the exact remote endpoint consistently in Python, C# service configuration,
runtime parsing and C++ send/receive paths. Read
[NATIVE-LOCAL-START.md](NATIVE-LOCAL-START.md) before native changes.

## ZeroTier components and network setup

1. **Client on every player PC.** Use the official Windows MSI; the download
   page currently lists 1.16.2 for x86/x64. Capture the selected release, URL,
   Authenticode signer, SHA256 and notices when preparing an installer. Driver
   installation needs elevation. The 32-bit game can use an OS virtual adapter
   without loading a VPN DLL into the game.
   [Download](https://www.zerotier.com/download/),
   [MSI deployment](https://docs.zerotier.com/intune-deployment/).
2. **One private network owned by the group organizer.** Create it in Central,
   join PCs using its 16-hex-digit network ID, then authorize each member.
   Assign one IPv4 per participating adapter, in one RFC1918 subnet that does
   not overlap any player's physical LAN or other VPN. For example, an unused
   `10.203.77.0/24` can work; inspect for conflicts before choosing it. Avoid
   addresses ending in .0 or .255 under current guards. Choose enough usable
   addresses for players and any dedicated coordinator.
   [Client configuration](https://docs.zerotier.com/config/),
   [Membership authorization](https://docs.zerotier.com/intune-deployment/).
3. **LAN discovery must cross that network.** Check that broadcast is enabled
   in controller configuration and traffic policy permits it. ZeroTier has an
   explicit broadcast flag; adding broad managed routes is not a substitute
   for verifying actual UDP 9112 discovery. Keep every PC directly on this
   overlay; physical LAN bridges and exit nodes are unnecessary for this plan.
   [Upstream broadcast flag](https://github.com/zerotier/ZeroTierOne/blob/dev/node/NetworkConfig.hpp).
4. **Retain split routing.** Proposed network settings are `allowManaged=true`,
   `allowGlobal=false`, `allowDefault=false`, `allowDNS=false`. They allow the
   private overlay without replacing normal Internet routing or DNS. Do not
   change global adapter metrics to make discovery work: this project already
   binds its discovery sockets to a source/interface.
   [Settings](https://docs.zerotier.com/config/).
5. **Keep a coordinator reachable.** It can run on a participating PC. The
   current coordinator does not migrate when its PC disappears, even if another
   PC hosts the actual game. No rented server is needed for the initial
   ZeroTier friends-group experiment.
   [Existing coordinator contract](AUTOMATIC-LAN.md).

The Windows launcher should present an optional Internet mode with one-time
network setup and readable states: client missing, service stopped, awaiting
authorization, no suitable IPv4, adapter unavailable, unreachable coordinator,
or ready. Preserve the existing single match selection inside the game.

## Ports and firewall scope

| Traffic | Scope and purpose |
| --- | --- |
| UDP 9112 | WePlay discovery broadcasts and unicast replies inside the selected overlay; clients use ephemeral source ports |
| TCP 9110 | WePlay coordinator configuration and admission inside the overlay |
| UDP 9111 | Existing coordinator/game-service protocol inside the overlay |
| Native game UDP | Separate peer traffic. Observation categories include 3074, 6200 and 9103; these are not a verified exhaustive port list. Confirm the actual bind paths before narrowing game rules to ports. |
| ZeroTier UDP 9993 and additional UDP ports | Outer encrypted transport; ZeroTier also uses high ports. Direct connections require outbound UDP and return traffic, not merely a game-port mapping. |
| Local TCP 9993 | ZeroTier management API. Keep it local; it is distinct from UDP 9993 transport. |

Project sources: `lobby_directory.py`, `lan_discovery.py`,
`native/transport_observation.h` and [AUTOMATIC-LAN.md](AUTOMATIC-LAN.md).
Vendor sources: [Router and firewall guidance](https://docs.zerotier.com/routertips/),
[Local API authentication](https://docs.zerotier.com/tokens/).

Do not forward WePlay ports on the home router or disable the firewall. Account
for the selected adapter being classified Public without opening all physical
Public networks. Scope any new rule to the prepared copy and selected overlay,
record ownership, and remove only owned rules on uninstall. Confirm that
LocalSubnet actually includes intended remote overlay addresses rather than
assuming the existing helper is sufficient.

## Launcher automation and embedding

Prefer **external ZeroTier One plus a launcher adapter** for the first version.
Use its supported CLI or local API to query status, join the selected network
and wait for authorization/address assignment. Example diagnostic commands are
`zerotier-cli -j info`, `zerotier-cli -j listnetworks` and `zerotier-cli peers`;
network mutation would use `zerotier-cli join NETWORK_ID` and the documented
`set` commands. Validate the ID as exactly 16 hexadecimal characters and pass
arguments as an array with bounded timeouts, cancellation and output sizes.
[CLI reference](https://docs.zerotier.com/cli/).

Local API access and hosted Central API access are separate. An optional local
integration may need a narrowly scoped privileged helper; never ship a Central
administrator token, copy a node's secret identity, or include tokens in logs.
The first version can use manual Central authorization and therefore needs no
hosted API token. New Central service accounts/API automation require Essential
or above; Personal currently excludes hosted API access.
[Token separation](https://docs.zerotier.com/tokens/),
[Central API](https://docs.zerotier.com/api-central/),
[Current plan matrix](https://www.zerotier.com/pricing/).

The alternative **libzt / ZeroTier Sockets** supplies its own socket API. It
does not automatically tunnel existing Python, .NET and game Winsock sockets.
Embedding it would require a transport redesign or bridge across all three
paths, plus architecture/build/lifecycle work. Do not make an SDK dependency
the first integration step. [Socket API](https://docs.zerotier.com/sockets/).

Pin and inspect the exact SDK and transitive license files before embedding.
The libzt README describes BSL 1.1, while its current top-level LICENSE names
a change date of 2026-01-01 and Apache 2.0 as the change license. The applicable
version and included dependencies matter; a blanket claim that every SDK use
requires a paid license would be unjustified. Hosted Central pricing remains
a separate service constraint.
[SDK repository](https://github.com/zerotier/libzt),
[SDK license](https://github.com/zerotier/libzt/blob/main/LICENSE.txt).

## Verification and implementation sequence

1. Add a transport choice and an overlay-specific adapter resolver; preserve
   normal LAN behavior. Test physical default route plus overlay, multiple
   overlays, missing authorization/IP, duplicate IPv4s, overlapping routes and
   cancellation. Never fall back silently to physical LAN in Internet mode.
2. Add detection/setup instructions and owned firewall handling. Keep the
   installation optional, respect an existing ZeroTier installation and unrelated
   networks, and leave shared VPN software intact when uninstalling WePlay.
3. On two PCs on different Internet connections, verify selected-source TCP 9110,
   UDP 9111 and directed-broadcast UDP 9112 before game launch. Ping alone does
   not prove any of these application paths. A LAN-only or single-PC test cannot
   qualify the Internet mode.
4. Check the actual peer's DIRECT/RELAY path using `zerotier-cli peers`, measure
   RTT/jitter and exercise CGNAT or restricted-UDP conditions where practical.
   Relay and TCP fallback can hurt latency. Test MTU/large datagrams on the real
   path; retain defaults until a measured failure justifies a change.
   [Connection diagnostics](https://docs.zerotier.com/routertips/).
5. Exercise simultaneous starts, lost replies, coordinator restart, lease expiry,
   disconnect and cancellation. Current status inspection has a .65-second
   timeout, discovery commonly one second, and admission leases 15 seconds with
   three-second renewals. Assess these under measured delay/loss before changing
   them; preserve bounded waits and exact-peer revocation.
6. Run actual shared lobby, guest readiness, map loading, construction commands
   and a sustained match beyond the previously reported desync point. Record
   Internet gameplay separately from protocol probes and synthetic checks.
   Keep simulation/checksum guards enabled. Swap coordinator/host roles and
   check supported player counts within map limits.
7. Only after that evidence, package an Internet-capable release with pinned
   dependencies, notices, rollback and documented provider limits. Remote
   operation of the second PC requires access or an operator; current local
   test-copy authorization does not grant remote access.

For the original separate-client approach, the first milestone is an explicitly
selected ZeroTier network working across two actual Internet connections. For
the clarified integrated/serverless requirement, first qualify the bundled
EasyTier topology described above, keeping direct-only and public-assisted
results separate. Radmin and SoftEther are optional alternatives.
