# WePlay Gateway bot

This is the dedicated bot for guild `1565306702356815962`, text channel
`1565306705535959042` (**🎮multiplayer**) and application
`1565350833854353459`. The settings file contains public IDs only. It must never
contain a token, client secret, webhook URL or private game invitation.

## On the prepared Windows development PC

1. [Install the bot with the four agreed permissions](https://discord.com/oauth2/authorize?client_id=1565350833854353459&scope=bot%20applications.commands&permissions=84992&guild_id=1565306702356815962).
   The user confirms installation in Discord. The bot checks application ID,
   channel type, guild and channel permissions at startup.
2. In an ordinary interactive PowerShell window at the repository root, run
   `./tools/discord/Start-Bot.ps1`. A private Python runtime has already been
   prepared in ignored `work/discord-bot-runtime` on this PC.
3. Enter the bot token at the local hidden console prompt. Do not paste it into
   chat, a command argument, a screenshot or a repository file. The token stays
   in process memory and is not persisted. It is needed again after restarting.
   An operator may alternatively supply `WEPLAY_DISCORD_BOT_TOKEN` privately in
   the process environment; the program removes its environment copy on entry.
4. Keep that console/PC running. Ctrl+C stops the bot gracefully. In the target
   channel use `/weplay-status`, `/weplay-hilfe` or `/spiele`.

There is no public HTTP listener and no need to configure an Interactions URL.
Only the non-privileged Guilds intent is enabled. Mentions are disabled, command
interaction data is not logged, and errors never print credentials/details.
A local file lock prevents two bot instances using the same state directory.
Commands are registered only in the configured guild. Startup sync manages the
three commands of this dedicated application in that guild; do not reuse the
application for an unrelated bot. Startup sends no public channel message.

## What works and what is still missing

The bot implementation can connect, register the three commands and respond in
the selected channel. It has not logged in or sent a Discord message during
development because no private token was supplied. Live installation, command
registration and channel permission behavior remain unverified until startup.

`/spiele` reads an optional **explicitly public status feed** on the bot host.
Without it, or after 90 seconds without a fresh observation, it says the source
is unavailable; it does not claim there are no games. The current launcher does
not yet produce or remotely publish this feed. Automatic aggregation across
players' private EasyTier rooms still needs an authenticated publisher/directory
and verified state projection. This bot does not solve that missing transport.

No match-state labels are inferred from running processes, admission leases or
session record counts. The initial feed accepts only public room titles and
aggregate counts, not IP addresses, participant identities, private codes,
network secrets or join links. Adding actual open/running matches or automatic
join actions is a separate integration with the game's verified state.

Operator feed schema (a synthetic example; `observed_at` must be current UTC
Unix seconds, and `public_id` must be independent of private room identifiers):

```json
{
  "format": "weplay-discord-public-status-v1",
  "observed_at": 0,
  "rooms": [
    {"public_id": "0123456789abcdef0123456789abcdef", "title": "Community room",
     "players": 2, "capacity": 8, "sessions": 1}
  ]
}
```

The example is intentionally stale and is not installed as live data. An
operator supplies a real feed path via `Start-Bot.ps1 -Feed <path>`; only locally
configured paths are used, never Discord command input. The parser limits file
size, age, room count, field names and count ranges and rejects known invitation
material. Existing game admission/native guards remain authoritative.

## Recreate the runtime

The tested lock is **CPython 3.14 / Windows x64**. With that interpreter, create
`work/discord-bot-runtime` using `python -m venv`, then use its Python to run
`-m pip install --only-binary=:all: --require-hashes -r tools/discord/requirements-win314.lock`.
The lock pins all eleven PyPI wheels. `requirements.txt` records the direct
dependency for other platforms; resolve and review a matching lock there.
No voice dependency is installed. Runtime/license files stay in the private
virtual environment, separate from the game/launcher installer.

Run `python -I -B tools/discord/bot.py --check` for a purely local settings check
with no token or network request. Run `test_discord_bot.py` with the private
runtime for offline configuration, command routing and projection checks.
Sixteen offline checks passed on this PC, as did `pip check`; all eleven locked
wheel hashes were compared with the corresponding official PyPI file records.
