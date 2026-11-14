# Gold/History: EasyTier transport and four-slot preparation

This is opt-in laboratory infrastructure. It does not establish four-player
Gold/History gameplay, Internet gameplay, NEO compatibility or mixed save/reload.
The ordinary launcher and published packages are unchanged.

## Encrypted transport boundary

`EasyTier(..., crossplay=True)` explicitly adds coordinator TCP port 9113 to
the existing room ACL. Ordinary rooms retain their existing ACL. Encryption
remains enabled with AES-GCM; exit-node, DNS and broadcast settings are unchanged.

`tools/gold/easytier_gateway.py` connects an owned, live EasyTier tunnel to the
existing native channel. Game sockets remain loopback-only. The host listens
only on its verified tunnel address and accepts the selected room peer. The
guest exposes a loopback entrance only to the native guest namespace. The
backend sockets bind the expected source address and tunnel interface.

On Windows, the interface alias resolves through the original IP Helper APIs.
`IP_UNICAST_IF` is set in network order and verified in host order, as specified
by [Microsoft's socket-option documentation](https://learn.microsoft.com/en-us/windows/win32/winsock/ipproto-ip-socket-options).
An unavailable or different interface fails closed. There is no physical-NIC
fallback. Buffers, connection count and lifetime are bounded; half-close and
partial writes preserve the byte stream. Tunnel exit closes owned gateway
sockets. The caller retains ownership of the tunnel process.

The gateway does not rewrite WPX1 frames, identities, authentication tags,
sequence numbers or game payloads. Native admission remains necessary. No
gateway listener opens on import. Integration into a runnable mixed-edition
Internet launcher and a real encrypted game test remain outstanding.

## Four-slot AI preparation

The laboratory archive builder accepts `--map-token`, `--ai-players 2` and
`--ai-difficulty 0|1|2`. With AI disabled, the default map remains Monhaim.
Map selection must identify exactly one unlocked original map. Two-AI
preparation requires four native slots, two observed humans and two available
original AI profiles. The original host page must observe the other human
before preparation examines the future guest slot: fresh maps can initially
assign an AI there.

The helper uses the original map/network AI setters with real map player IDs,
then waits for their readback. It does not supply Ready/start results or
acknowledgements. Re-entry, occupied human slots, invalid/duplicate IDs,
configuration drift and timeout exclude the attempt. Successful local readback
does not establish peer configuration agreement.

The actual Monhaim probe reports four slots, CPU enum 4 and nine numeric AI
profiles. Native observation confirms CPU slots 3/4 have state 4, Ready 1,
privacy 0, their own slot number and station 0. The existing native fresh-start
bridge is restricted to closed extra slots (state 2), so the four-player
configuration requires its own native transport/setup path. It is not enabled
by weakening the ordinary two-player start guard.

## Verification and limits

- Native TCP_NODELAY and the Python broker passed 20 real loopback integration
  scenarios using synthetic metadata for both History profiles. Authentication
  rejection, ordered large messages, tiny-message bursts and EOF draining pass.
- The native channel requires TCP_NODELAY readback. Winsock returned a
  one-byte enabled value on this host; one-byte and integer-sized values are
  accepted only when the zero-initialized value is exactly 1.
- One actual Gold/Steam DXVK High pair with this transport completed 1,366
  consecutive matching original checksums (finished turns 0–1365). Each client
  sent one original build and road command, all delivered natively. Both clients
  exited normally and all profiles, archives and adapters were restored.
- Both native schedulers consumed the original 5-ms duration request. Observed
  throughput remained about ten turns per second: this does **not** qualify
  effective x20 speed. A 35-second read-only counter sample averaged 126.8%
  of one CPU core for Gold and 48.8% for History. This is diagnostic evidence,
  not proof of the remaining bottleneck.
- Fifteen gateway tests cover real local stream forwarding and mocked tunnel
  ownership/interface failures. Eighteen EasyTier tests cover opt-in ACL and
  configuration behavior. An unused local socket also passed the actual Windows
  interface-option readback. None of these tests establish a live VPN or game
  connection through EasyTier.
- Eleven Lua 5.1 AI-helper tests and eighteen driver tests cover original call
  ordering, native numeric profiles, human-arrival gating and guarded failures.
  The bounded native AI discovery runs ended normally and restored their files;
  they did not enter a four-player world.

Full NEO logistics, adapted multiplayer maps, both History pairings with AI,
mixed saves/reloads and effective x20 throughput still need actual qualification.
