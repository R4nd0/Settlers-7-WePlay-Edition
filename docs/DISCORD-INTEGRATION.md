# WePlay Discord integration

Implemented 2026-10-29: the launcher has a permanently visible community link,
the suggested Discord server code in Help, a copyable server invite, and an
explicit **Copy for Discord** action for a private game room. That action copies
the room code and bilingual joining instructions; the player chooses where to
paste/send it. It does not post messages automatically or open a URL containing
the room secret. The private UI/backend pipe and normal log/error protections
remain in place. Raw room codes still work unchanged.

Community: https://discord.gg/nvcERcHpR

The public Discord invite API returned guild `1565306702356815962`, named
**Settlers 7: WePlay Edition**. On 2026-10-29 it reported expiry
**2026-11-28 11:25:34 Europe/Berlin**. The supplied code is used as requested;
it must be replaced with a non-expiring invitation before treating it as a
permanent distribution link. A Discord server invite is not an EasyTier game
invitation. No common VPN secret or fixed coordinator was silently introduced.

## Showing games in Discord

Discord supports two complementary surfaces:

| Surface | What players see | Required infrastructure |
| --- | --- | --- |
| Rich Presence | A player's current state, party size and optional game-join action in their profile | Registered application ID and running Discord client; native Social SDK integration can provide presence without account authentication |
| Server bot, e.g. `#games` | One shared list of voluntarily advertised rooms; commands/buttons to show and remove entries | Registered app installed in the guild, chosen channel, and a trusted bot/directory process or hosted function |

Rich Presence is not a global room directory. Discord cannot discover our
private EasyTier rooms on its own. A server bot needs explicit, authenticated
room announcements from the creator and a short heartbeat/expiry policy.
The bot token belongs only in the trusted bot process; never ship it in the
launcher, repository, room invitation or a shared webhook URL.

Proposed listing flow: creator opts into advertising, launcher reports a small
public projection to an authenticated directory, the bot edits one room card,
and a stale entry expires after 90 seconds without a 30-second heartbeat.
Normal closure removes the entry immediately. Default private rooms remain
unadvertised. Joining should resolve a short-lived opaque join reference rather
than publishing an EasyTier network secret in public Rich Presence. Public room
invitations require the creator's explicit choice to make them public.

### Data we can currently verify

The existing bounded/source-bound lobby status endpoint exposes `members`,
`active_members`, `max_members`, `sessions` and `content_id`. These are admitted
clients/session records. An active lease or a running S7 process does **not**
prove an ongoing match. There is currently no verified public projection for
map names, open slots per game, joinability, match progress or a distinction
between open and running matches. Do not label aggregate counts as those facts.
Adding those fields needs a deliberate read-only projection of verified game
state; no raw metadata, identities, IP addresses or packet contents should be
sent to Discord. Existing admission/native guards remain authoritative.

### Registration needed

No Discord application or bot was created by this change. The Developer Portal
available to this chat is not signed in. Registration needs the server owner's
Discord account, an application name/ID and target channel; installing a bot
needs the guild's authorization. Bot credentials must be configured privately
on its host. No token is requested in chat. The server link and clipboard sharing
work without an application ID, bot or Discord account access by the launcher.

Official references:

- [Discord bot setup and application registration](https://docs.discord.com/developers/quick-start/getting-started)
- [Rich Presence](https://docs.discord.com/developers/discord-social-sdk/development-guides/setting-rich-presence)
- [Game invitations](https://docs.discord.com/developers/discord-social-sdk/development-guides/managing-game-invites)
- [Discord lobby API](https://docs.discord.com/developers/resources/lobby)

Validation: compiled bilingual UI/private-pipe self-test, formatted invitation
round-trip and malformed/unrelated invitation rejection. No real Discord message
was sent. No live bot, Rich Presence or automatic listing is claimed.
