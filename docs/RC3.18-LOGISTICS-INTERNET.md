# RC3.18: logistics and Internet fixes

Windows installer/ZIP and native Linux x64 package. Update every participant,
including when NEO is off: this revision requires the NEO5 lobby capability and
rejects older clients. Keep backups of your saves before updating.

## NEO-Logistics

- Fixes the reproduced blue weaving-mill stall: an inconsistent production-ready
  task no longer prevents collection of its unfinished wool input. The saved
  comparison remained blocked for 1,500 turns without the fix; with the fix,
  refinement resumed within 100 turns and all four old wool requests cleared.
- Prefers the shortest total usable road distance among eligible intermediate
  warehouse handoffs. Original ownership, carrier and reachability gates remain.
  This does not change stock-source selection or promise universally shortest
  routes. One matched books delivery arrived 66 simulation turns earlier.
- FIFO still uses order creation age. Explicit player and AI individual
  priorities remain effective; group priorities remain disabled in NEO.
- Existing NEO2 save payloads and individual-priority restoration remain supported.

NEO remains optional per map and off by default. These changes do not activate
NEO in a save that recorded it as disabled.

The build enables bounded save-transfer catch-up used in the verified reload
tests. The nominal fragment rate and per-update cap remain enforced.

## Internet room readiness

- Replaces the failed EasyTier bootstrap with a working, verified community
  relay. Assisted hosts require an established handshake to that relay.
- An invitation and **room ready** appear only after coordinator and room
  preflight checks succeed. A local adapter alone no longer counts as ready.
- Relay failures produce a connection-specific message. Create a new invitation
  after updating; old invitations retain the retired endpoint.

The community relay can become unavailable. Direct mode retains its existing
behavior and does not independently prove inbound public reachability.

## Verification and limits

The weaving-mill control/candidate runs each advanced 1,500 turns from the same
multiplayer checkpoint; the final NEO5 reload advanced another 500 turns. Each
run's 451 recorded peer checksums matched, with normal exits and no production
toggle. This is sampled diagnostic coverage, not every turn of the longer runs.
The new NEO corrections were exercised with local Windows clients, not a new
separate-PC or Linux gameplay qualification.

Two isolated no-TUN EasyTier cores established real relay handshakes and found
each other through the replacement relay. This proves rendezvous, not an actual
Internet S7 match or TUN/firewall data-plane integration.

RC3.17's native Linux tools, Windows/Linux content contract, Map Forge variant
fixes and workshop-resume desync correction remain included. Earlier Linux
qualification used Ubuntu 24.04/WSL2, WineHQ 11 and Ubisoft History Edition.
Linux Mint, genuine Linux Steam/Proton and hardware Vulkan gameplay remain
unqualified. All executables remain experimental and not Authenticode-signed.

See [workshop evidence](NEO-WORKSHOP-PICKUP.md),
[distance evidence](NEO-LOGISTICS-DISTANCE.md),
[Internet readiness](INTERNET-READINESS.md) and [Linux setup](LINUX.md).
