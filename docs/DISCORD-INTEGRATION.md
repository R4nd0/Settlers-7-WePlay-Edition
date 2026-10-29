# WePlay automatic Discord directory

RC3.2 adds an optional external directory for the existing WePlay Discord app.
The user selected permanent external operation on 2026-10-29. The intended host
is Cloudflare Workers Free with D1 and a minute Cron trigger; no private PC is
needed to operate the Discord app. EasyTier and the room creator still handle
the actual game connection. An Internet room creator must remain connected while
that room is used.

Community invite: https://discord.gg/j2eQgVzssc

| Public setting | Value |
| --- | --- |
| Application | `1565350833854353459` |
| Guild | `1565306702356815962` |
| Automatic board channel | `1565306705535959042` / #🎮multiplayer |
| Community invite landing channel | `1565306705535959041` / ℹ️news |

The bot is installed and the user confirmed its initial Gateway connection and
three commands. RC3.2 implements the external replacement in
[tools/discord-worker](../tools/discord-worker/README.md). Worker, database and
minute Cron are now deployed. The publisher master secret is privately set;
BOT_TOKEN and the Discord Interactions Endpoint cutover are still required.
The public [health endpoint](https://example.invalid/removed-personal-link)
correctly reports not ready until the bot token is present. Unsigned interaction
requests and unauthenticated publisher checks return 401 in production.
The old Gateway implementation remains available under tools/discord for
migration; do not run two production directory writers.

## Player flow

1. In #🎮multiplayer run `/weplay-hilfe` and choose **Launcher verbinden**.
2. Copy the private connection code from Discord's ephemeral response into
   **Mit Discord verbinden** in the launcher's Internet tab. This is a limited
   publisher grant, never the bot token. A new code invalidates the old grant.
3. Select whether to show your own public games before starting Internet play.
   The connection action enables this preference initially; the checkbox can
   disable it before launch. Private games are omitted in all cases.
4. The launcher reports changes while the game is open, sends a heartbeat every
   minute and withdraws its report on normal closure. Missing reports expire
   after three minutes; the minute Cron removes them from the displayed board.
   Discord errors/rate limits can delay the visible refresh further.

The launcher stores the grant in the current user's Windows Credential Manager.
Its normal state file contains only service origin and the publishing preference.
Connection codes and EasyTier invitations use the existing current-user-only
private pipe; normal backend events contain neither. Disconnect removes this
PC's grant and preference; a new Discord connection code revokes the old grant
at the service. The game continues if publishing fails.

## What the board establishes

The source-bound, admission-protected `/public/lobby/v1/games` endpoint returns
only the requesting owner's public native sessions and the content ID. Its
immutable projection includes a short session ID, sanitized title, confirmed
human player count, capacity, available places and one of three states:

- **Offen:** observed positive capacity with effective free places.
- **Voll:** observed positive capacity with no effective free places.
- **Gestartet / lädt:** the native host changed slot counts to `(0,0)` after a
  positive capacity had been observed. The inspected, fingerprinted game's
  hostLobby path makes this change immediately before `Network.StartGame()`.

This last signal establishes a start/loading request, not completed loading,
simulation progress, successful networking or a desync fix. A full lobby is not
reported as started. Confirmed members and pending reservations are distinct.
A process/lease alone is never treated as a running match.

The external directory displays launcher reports; a publisher grant is not
remote attestation of a modified launcher. No raw metadata, map data, account
identity, peer IP, host address, VPN secret or private invitation is sent in the
report or displayed by the bot. Sanitized game titles are public player-authored
text; players should choose them accordingly. Cloudflare necessarily receives
the HTTPS client's source address as the service provider. Application code
does not store it; Worker application observability is disabled.

## External operation

The Worker validates Discord's Ed25519 signature over the exact request bytes,
then enforces application, guild and channel. Only signed member interactions
can mint publisher grants. D1 stores HMAC owner identifiers and token hashes,
not raw Discord user IDs or publisher keys. The bot token and publisher master
key belong only in Cloudflare Worker secrets, never the launcher or Git.
Reports are scoped to their grant, run and increasing sequence; closed runs
cannot be revived by delayed updates. Each account has one active publisher
run. Connecting a second PC replaces that account's previous grant.

A single board message is created in the configured channel and edited only if
its contents change. The service checks the app, guild, message author and board
marker before writing. Mentions are disabled. D1 leases suppress concurrent
creates, and Discord rate-limit responses postpone retries. `/spiele` displays
the current directory; `/weplay-status` and `/weplay-hilfe` reply privately.
The existing View Channels, Send Messages, Embed Links and Read Message History
permissions suffice. No privileged intents or Manage Messages are required.

The initial bounds are 256 paired accounts, 16 concurrent publishers, eight
sessions per publisher and 16 displayed game fields. The free provider quotas
still apply; exceeding them can interrupt updates. No paid upgrade is required
by this implementation. See the operator README for deployment and cutover.

## Verified locally

The service suite passed 8,868 checks, including 22 new projection/admission
checks. Worker tests use real Ed25519 and SQLite with external calls mocked;
a separate real workerd/D1 smoke test verified signed PING, private pairing,
grant authentication and report lifecycle with Discord egress blocked. Python
tests cover projection, HTTPS boundaries, heartbeat/closure, private pairing and
an actual temporary Windows Credential Manager round trip. Compiled DE/EN UI
and a real .NET/Python private pipe pass. These are synthetic/local checks.
The external deployment and rejection boundaries are verified; the production
automatic board and two-PC Internet match have not yet been verified.

Official references:

- [Discord HTTP interactions and signatures](https://docs.discord.com/developers/interactions/overview)
- [Discord messages and embeds](https://docs.discord.com/developers/resources/message)
- [Cloudflare Workers pricing](https://developers.cloudflare.com/workers/platform/pricing/)
- [Cloudflare Cron triggers](https://developers.cloudflare.com/workers/configuration/cron-triggers/)
- [D1 pricing and daily Free limits](https://developers.cloudflare.com/d1/platform/pricing/)
