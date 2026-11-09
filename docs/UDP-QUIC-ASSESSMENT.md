# S7 transport and QUIC feasibility

Assessment date: 2026-11-09. An independent agent investigated this separately
from the launcher fixes, using repository evidence and primary protocol/vendor
sources. No transport was changed and no game/network benchmark was run.

## Current stack

S7 already uses UDP. The configuration HTTP service must not be mistaken for
the game's complete networking stack.

| Layer | Repository evidence |
| --- | --- |
| Configuration/coordinator | HTTP/TCP 9110 alongside RendezVous/PRUDP UDP 9111. [Concurrent loops](../Source/LocalConfigServerCommand.cs#L257). |
| PRUDP semantics | Sessions, sequences, ACKs and retries; reliable and ACK-required flags are distinct. [Handshake](HANDSHAKE-PROTOCOL.md), [DATA/ACK](DATA-ACK-PROTOCOL.md), [response queue](../Source/ReliableResponseQueue.cs). |
| Native game peers | Separate Storm UDP path. Earlier local experiments recorded UDP 3074, 6200 and 9103, including actual peer sends/receives on 3074. [Recorded evidence](LOCAL-PAIR-LAB.md#L203), [Winsock hooks](../native/network_guard.cpp#L522). |
| Discovery | UDP 9112. [Port/ACL inventory](EASYTIER-SECURITY.md#L25). |
| Internet overlay | EasyTier TUN, preferred outer UDP, UDP/TCP listeners on 11010 and AES-GCM. [Configuration](../tools/installer/easytier_runtime.py#L121). |

Storm also exposes reliable messages: the adapter preserves native reliability,
recipient and checksum parameters during save transfer
([sender](../native/resume_transfer.inc#L51)). This is not a complete mapping of
all native channels, nor evidence that Storm is identical to the PRUDP service.

## What a QUIC change would mean

QUIC itself runs over UDP. It supplies reliable multiplexed streams, congestion
control and standard TLS-based connection security; it does not specify the
game's message semantics. Independent streams reduce cross-stream blocking,
while ordered delivery still matters within each stream.
[Microsoft overview](https://learn.microsoft.com/en-us/dotnet/fundamentals/networking/quic/quic-overview),
[QUIC transport specification](https://www.rfc-editor.org/rfc/rfc9000.html).

For a game that already schedules reliable and unreliable UDP messages, an
extra reliable ordered tunnel can delay later inner packets behind lost tunnel
bytes. It also leaves the inner protocol's ACK/retry logic intact. Therefore
"QUIC instead of UDP" is not intrinsically a latency improvement.

The relevant alternative is **QUIC DATAGRAM**: preserved datagram boundaries
without QUIC retransmitting those messages. It still has congestion control,
may buffer/drop packets, negotiates size limits and cannot fragment a DATAGRAM
across QUIC packets. MTU/framing and any application fragmentation must be
designed explicitly. The RFC explicitly discusses IP/VPN tunnelling as a use.
[RFC 9221](https://www.rfc-editor.org/rfc/rfc9221.html).

## EasyTier is not a drop-in DATAGRAM switch

- `enable_quic_proxy` concerns inner **TCP**, not replacement of S7's UDP
  transport. [Vendor description](https://easytier.cn/en/guide/network/kcp-proxy.html).
- The pinned upstream v2.6.4 `quic://` tunnel opens a bidirectional stream and
  uses framed readers/writers. It is not a QUIC-DATAGRAM tunnel.
  [Versioned source](https://github.com/EasyTier/EasyTier/blob/v2.6.4/easytier/src/tunnel/quic.rs#L542).
- Our [build](../tools/easytier/build-runtime.ps1#L48) uses
  `--no-default-features` with `tun,aes-gcm,smoltcp`, omitting QUIC. The current
  configuration explicitly disables its proxies/input/relay modes.

Enabling a flag is consequently neither sufficient nor the recommended design.
Upstream's particular crypto/backend configuration must also be audited rather
than assuming every implementation provides the same security properties.

## Feasible implementation paths

**Outer DATAGRAM tunnel:** leave the game and native ABI unchanged. Carry its
UDP/IP packets through a separate helper or reviewed EasyTier extension. Use
streams only for appropriate control traffic; preserve game datagram semantics.
Both endpoints need the new transport and compatible framing/versioning.

This requires authentication tied to the room, MTU handling, NAT/relay fallback,
existing ACL/routing guarantees, discovery and bounded process lifetime. QUIC
does not remove UDP blocking or the need to handle difficult NATs. Current
EasyTier encryption already exists, so encryption alone is not a new benefit.
See the deployment considerations in
[RFC 9308](https://www.rfc-editor.org/rfc/rfc9308.html).

**Native protocol replacement:** intercept before the game's own reliability
layer and replace PRUDP/Storm semantics. This is substantially more invasive:
closed x86 ABI, ownership thread, ordering, retransmission, saves, timeouts and
edition compatibility need complete understanding and requalification. Merely
replacing Winsock UDP calls cannot remove the existing protocol machinery.

For a .NET helper, default `System.Net.Quic` support requires Windows 11/Server
2022+ or Linux with `libmsquic`; this would narrow today's Windows 10 support if
made mandatory. Its public API is stream-oriented; native MsQuic has DATAGRAM
operations. Other QUIC libraries/custom builds are separate portability choices,
not proof that QUIC universally requires Windows 11.
[Platform requirements](https://learn.microsoft.com/en-us/dotnet/fundamentals/networking/quic/quic-overview),
[QuicConnection API](https://learn.microsoft.com/en-us/dotnet/api/system.net.quic.quicconnection?view=net-10.0),
[MsQuic DatagramSend](https://github.com/microsoft/msquic/blob/main/docs/api/DatagramSend.md).

## Recommendation and evidence needed

Keep the current UDP/EasyTier transport as the default. If actual network
measurements identify a problem, compare an isolated outer DATAGRAM prototype
against the same UDP path, without first changing the game protocol.

Measure p95/p99 message latency, loss/retransmissions, CPU/memory, MTU, reconnects,
save transfer and long-match behaviour under controlled delay/loss/reordering.
Then test both host directions on separate PCs, with direct and relayed paths
and Windows/Linux compatibility. Preserve rollback and the existing fallback.

No faster gameplay, desync fix, improved NAT traversal or safer deployment is
claimed from this source review. QUIC is technically possible; whether it is
better for this particular game remains a measurement question.
