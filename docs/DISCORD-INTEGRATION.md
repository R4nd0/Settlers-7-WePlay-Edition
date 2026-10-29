# WePlay Discord integration

Implemented 2026-10-29: the launcher has a permanently visible community link,
the suggested Discord server code in Help, a copyable server invite, and an
explicit **Copy for Discord** action for a private game room. That action copies
the room code and bilingual joining instructions; the player chooses where to
paste/send it. It does not post messages automatically or open a URL containing
the room secret. The private UI/backend pipe and normal log/error protections
remain in place. Raw room codes still work unchanged.

Community: https://discord.gg/j2eQgVzssc

The public Discord invite API returned guild `1565306702356815962`, named
**Settlers 7: WePlay Edition**. The replacement invitation supplied by the user
on 2026-10-29 has `expires_at: null` in the public Discord API. Its landing
channel is `ℹ️news` (`1565306705535959041`), not the requested bot channel
`#🎮multiplayer`; do not reuse that channel ID for bot output.
A Discord server invite is not an EasyTier game
invitation. No common VPN secret or fixed coordinator was silently introduced.

## Showing games in Discord

Discord supports two complementary surfaces:

| Surface | What players see | Required infrastructure |
| --- | --- | --- |
| Rich Presence | A player's current state, party size and optional game-join action in their profile | Registered application ID and running Discord client; native Social SDK integration can provide presence without account authentication |
| Server bot, e.g. `#games` | One shared list of voluntarily advertised rooms; commands/buttons to show and remove entries | Registered app installed in the guild, chosen channel, and a trusted bot/directory process or hosted function |

The user selected a server bot in **#🎮multiplayer**. Initial transport will be
an outbound Discord Gateway connection, so no public interaction endpoint,
OAuth redirect URL, client secret or HTTP-signature public key is needed for
this phase. The bot process still needs a running host; it can initially be the
user's PC. This does not add a dedicated gameplay server.

The user supplied the target channel ID `1565306705535959042` on 2026-10-29.
It is recorded with the verified guild ID in `tools/discord/settings.example.json`.
The application ID is still missing; this example is configuration preparation,
not an active bot. The token will be read privately from
`WEPLAY_DISCORD_BOT_TOKEN` on the bot host, never from the launcher or this file.

## Developer Portal setup for the owner

1. Create an application named **WePlay Multiplayer** at
   https://discord.com/developers/applications. Copy its **Application ID**
   from General Information.
2. On **Bot**, use the application's bot user. Keep **Presence Intent**,
   **Server Members Intent** and **Message Content Intent** off. Generate the
   bot token if needed and keep it privately for the bot host, never in chat,
   the launcher, screenshots or Git. Do not enable OAuth2 Code Grant.
3. On **Installation**, enable **Guild Install**. User Install is unnecessary
   for this server bot. Select **Discord Provided Link**. For Guild Install,
   select scopes **bot** and **applications.commands**.
4. Grant **View Channels**, **Send Messages**, **Embed Links** and
   **Read Message History** (combined permission value `84992`). Administrator,
   Manage Messages, voice and moderation permissions are unnecessary for this
   design. It updates/deletes its own listing messages.
5. Use the install link to add the bot to **Settlers 7: WePlay Edition**.
   Ensure those four permissions are also allowed by the target channel's
   permission overrides.
6. In Discord, enable **Settings > Advanced > Developer Mode**, right-click
   **#🎮multiplayer** and select **Copy Channel ID**. That channel should be a
   normal text channel for the first implementation.

Only the application's non-secret ID is still needed in chat:

```text
Application ID: <from General Information>
```

The guild ID is already verified as `1565306702356815962`, and the target
channel ID is supplied as `1565306705535959042`. The bot token is
needed only as a private credential on the selected host. The application ID
is not a token; Client Secret, user tokens and webhook URLs are not needed.

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
