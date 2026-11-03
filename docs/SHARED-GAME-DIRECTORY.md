# Shared game directory and private joining

Development implementation, after RC3.12. The published RC3.12 ZIP does not
contain these new features. EasyTier remains the transport; this is original
WePlay code, with no aLobby code copied into the MIT project.

## Player flow

The launcher has a **Public games / Öffentliche Spiele** tab. It lists the same
public native matches as Discord, including player counts, open/full/started
state, version and content compatibility. Refresh the list, select an open
compatible match and choose **Join**. WePlay obtains the host's private room
invitation, joins its EasyTier room and starts the game. Select the actual LAN
match inside S7. A directory entry does not itself admit a native game player.

In Discord, `/weplay-hilfe` provides **Spieleverzeichnis verbinden** for readers
and the existing **Launcher verbinden** for publishers. Reader configuration
does not require a publisher credential. The board and `/spiele` offer a menu
of open games; selecting one returns an ephemeral `weplay-join1.` code to paste
into the launcher. No VPN invitation is posted in the channel.

To publish, link the host launcher with Discord, enable its existing publication
checkbox, create an Internet room and create a public native match inside S7.
Nothing is published just because a launcher or an empty room is open. Private
native sessions remain excluded by the local owner-filtered projection.

The native multiplayer browser also has a **WePlay Online** view. It reads the
same public directory through the running launcher. Joining is available only
from the session browser, outside a native session. A confirmation explains
that the game will exit normally and restart in the selected host's room.
WePlay closes the previous managed transport before obtaining and starting the
new room. Active worlds are not switched in place. Native LAN discovery and
native session admission still select and validate the actual match afterward.

## Data and admission

Public rows contain only a derived game ID, sanitized title, native state and
slot counts, full content ID, network revision, version and expiry. Owner IDs,
addresses, credentials, room secrets and private invitation tokens are excluded.
The full content comparison and existing native executable/ABI guards remain.

A guest generates a transient RSA3072 key pair and a random request token.
The server stores its public key, challenge and token hash. It forwards the
request to that game's authorized publisher. The host checks its current native
game is still open, then encrypts the invitation with AES256-GCM and wraps the
random AES key with RSA-OAEP SHA256. Standard .NET cryptography implements both
operations; no new cryptographic primitive is implemented here. Request ID,
game ID, challenge and expiry are authenticated as associated data. The guest
verifies them and rechecks the live compatible listing before using the room.

The existing Discord publisher credential remains in Windows Credential
Manager. Transient private keys and decrypted invitations travel through private
inherited stdin/stdout pipes, not command arguments or diagnostic output. The
server stores only an encrypted room envelope; the native game never receives
that secret through the public-data UI bridge.

Host leases last 35 seconds and refresh approximately every 10 seconds; guest
requests expire after 45 seconds. Reservations are bounded by native free slots,
64 active requests globally and 12 creations per source per minute. These are
temporary broker reservations, not native seat acknowledgements. Rate limiting
stores a short-lived keyed source hash, not the raw source address. Minute
maintenance removes expired requests/hosts and expired two-minute rate buckets.
Consumption drops the envelope immediately. Failure, expiry, incompatible data,
closed/full games and stale native UI snapshots fail closed.

The in-game bridge reads a fixed bounded ASCII data file in the managed copy's
`LocalData`. It parses title bytes as data, never as Lua or a downloaded script.
A per-run nonce binds the fixed join-request file to the current launcher run.
The native bridge accepts only fixed read/join operations, guards owner thread
and callback ownership, bounds reads to 32 KiB and rejects reparse leaves.
Ubisoft and Steam have independently mapped callback slots and Lua API addresses.
Other console calls retain the original callback and result.

## Build and operator deployment

Build the optional native adapter using `native/build.ps1 -GameDirectory
-MaximumPhysicalCores 2`. That option includes the existing network pump.
Build the current Windows SystemHelper, then package using `--game-directory`.
The package builder verifies the helper capability and exact directory adapter
and archive fingerprints. Old RC packages keep their existing fingerprints and
do not gain the feature accidentally. The hash-locked interface extension runs
after the existing multiplayer save/autosave patch.

`tools/installer/directory-service.json` deliberately has a null origin. An
operator can supply the configured HTTPS service in the private build, or a
player can paste a reader/join code. Existing host pairing metadata also supplies
the origin. Do not commit operator configuration, credentials or a hostname
containing personal identity. Use the existing Worker/D1 and Free plan.

Apply `0002_join_directory.sql` before deploying the new Worker. Existing report
and publisher routes remain compatible with old clients. The new routes are:

| Route | Access and purpose |
| --- | --- |
| `GET /v1/directory/games` | Public, bounded metadata only |
| `PUT/DELETE /v1/directory/host` | Existing authenticated publisher; lease/poll/withdraw |
| `PUT /v1/directory/host/<run>/requests/<id>` | Owning publisher; encrypted reply or rejection |
| `POST /v1/directory/join` | Public, compatible bounded guest request |
| `GET/DELETE /v1/directory/join/<id>` | Request-specific private token; delivery/consume |

Migration and Worker version `d37c0921-8814-4c0f-b70f-9b1fc30fa7e6` are deployed
to the existing service. Health and the public directory schema were checked;
unauthenticated host publication is rejected. No synthetic game was published
to the real Discord channel or public service. The existing minute Cron remains.

## Verification status

Passed: Worker route/lifecycle tests; real local workerd and D1 with two
independent .NET crypto clients; wrong keys/context and consumed-ticket rejection;
independent WebCrypto decryption of .NET envelopes; signed private Discord join
response, with all external Discord calls mocked. Python tests cover strict
metadata/codes, transport bounds, backend delegation, cancellation/cleanup,
host retries, Lua parsing and guarded menu-only restart. Both launcher languages
rendered successfully. The fully packaged app-relative helper passed an actual
encrypt/decrypt round trip. Native synthetic callback/patch/rollback tests pass
for Ubisoft and Steam.

Observed with real games so far: two guarded laboratory clients start; a real
host-created match is projected through the local Worker and displayed in the
guest's global in-game view. An initial overlap with the old LAN list was fixed.
The final visible selection/restart/rejoin test is still pending. Native game
traffic so far used loopback; the remaining integration setup uses isolated
EasyTier peers without TUN. Two-peer EasyTier admission through the new broker
has not yet been observed. These checks do not qualify Internet gameplay, two
remote PCs, a real Discord publication, or long-session simulation synchronization.
