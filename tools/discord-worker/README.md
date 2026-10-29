# WePlay external Discord operator

This Worker replaces the private-PC Gateway host. Worker, D1 and minute Cron
are deployed; both private secrets are set and Discord has verified the HTTP
cutover. The real automatic empty board has been created in #🎮multiplayer.
The original local Gateway bot was gracefully stopped. Launcher pairing and
actual public game reports still require validation; do not fabricate games.
The owner confirmed that `/weplay-hilfe` responds with Launcher verbinden after
the HTTP cutover.
Only use Workers/D1 Free
for this initial setup. Do not enable paid plans or create a duplicate app.

## Public configuration

`wrangler.jsonc` contains the already supplied application, guild, channel and
public Ed25519 key. Its account subdomain and D1 ID are explicit placeholders.
Copy that configuration into an ignored `work/discord-cloud/` directory and make
`main` and `migrations_dir` absolute paths to this source tree. Put the actual
D1 database ID and `https://weplay-multiplayer.<account>.workers.dev` origin in
the local copy. Keep all account/runtime configuration out of Git. The Worker
and launcher intentionally accept only this HTTPS workers.dev service shape.

Install official Wrangler in an ignored toolchain (`npm install --prefix
work/discord-worker-toolchain wrangler@4.155.0`) and set
`WRANGLER_SEND_METRICS=false`. The prepared local toolchain already exists in
this workspace. Invoke its `.bin/wrangler.cmd` on Windows. Authenticate through
Wrangler's normal OAuth flow with the owner's account; the owner completes
login and authorization. A signed-in dashboard alone is not CLI authorization.
For Wrangler 4.155.0 the minimal tested OAuth scope selection is `account:read
workers_scripts:write d1:write` (plus Wrangler's required `offline_access`). The
older `workers:write` scope did not authorize the scripts deployment API. Use
`wrangler login --use-keyring --scopes account:read workers_scripts:write d1:write`.
The official `--device --browser=false` flow avoids the short localhost callback
timeout when the owner needs more time. The owner must approve the exact scope
selection. No extra KV, routes, zone, user-details or billing-write access is
needed for this Worker.

On Windows `--use-keyring` needs `@napi-rs/keyring@1.3.0`. It can be installed
under the private toolchain; setting `npm_config_prefix` to that directory lets
Wrangler's global-package resolver find it without a machine-wide installation.
OAuth data is stored in an encrypted Wrangler configuration with its key in
Windows Credential Manager. Do not read that credential into a script or chat.
Set `WRANGLER_WRITE_LOGS=false` and keep log sanitization enabled for secret
uploads. Never capture the secret value in a command argument or local file.

1. Create D1 `weplay-directory` with `wrangler d1 create weplay-directory` and
   record the returned database ID in the local configuration.
2. Check the local configuration and run `wrangler deploy --dry-run --config
   <local-config>`; it must point only to the intended Worker/database.
3. Run `wrangler d1 migrations apply weplay-directory --remote --config
   <local-config>` to create the three new tables. No existing data is deleted.
4. Deploy with `wrangler deploy --config <local-config>`. Before adding secrets
   the board cannot post and health returns 503.
5. Privately set `PUBLISHER_MASTER_KEY` and `BOT_TOKEN` using `wrangler secret
   put <name> --config <local-config>` or the Worker dashboard secret fields.
   The master is 32 random bytes encoded as 43-character unpadded base64url.
   The bot token is the app's existing token. The owner enters it privately;
   never recover it from the running bot, clipboard, screenshots or logs.
   Do not put either secret in command-line arguments, config files or Git.
6. Verify `/health` is ready and invalid unsigned `/interactions` returns 401.
   A Cron can now create the single real empty board in #🎮multiplayer.
7. Set the Discord app's **Interactions Endpoint URL** to
   `<PUBLIC_ORIGIN>/interactions`. Discord sends a signed verification PING.
   Save only after it passes. This switches interactions from Gateway to HTTP.
   Discord also documents `PATCH /applications/@me` with
   `interactions_endpoint_url` for the requesting bot. The same signature/PING
   validation applies. This deployment used a fixed-target one-time scheduled
   activation inside the Worker so the secret never had to leave Cloudflare;
   the activation was removed from the regular deployed entry afterwards.
8. Verify `/weplay-status`, `/weplay-hilfe` and `/spiele` in the exact target
   channel. Then gracefully stop the old local Gateway bot.
9. Install RC3.2, privately pair the launcher and check an actual public lobby.
   Do not post fabricated games to the live channel as a deployment test.

The initial Gateway client already registered the app's three guild commands.
The Worker uses those same names; no extra command registration is required for
this app. A fresh unrelated app would require registration and different IDs.
Do not change bot scopes/intents or add Administrator permissions for hosting.

## Bounds, recovery and privacy

One account has one grant and one active run. A new signed pairing invalidates
the previous grant and closes its report. Normal closure withdraws a run; stale
updates cannot revive it. Reports expire after 180 seconds and Cron refreshes
the board each minute. Up to 256 paired accounts and 16 concurrent publishers
are allowed; each report has up to eight games, with 16 board fields displayed.
These are intentional initial service bounds, not a gameplay limit.

At the researched Free limits Workers allows 100,000 requests/day and 10ms CPU
per invocation; D1 allows 5M rows read/day, 100k rows written/day and 5GB stored.
Quotas are provider-wide and can change. Check official limits at deployment;
Free exhaustion can stop the directory. There is no unlimited-free or uptime
promise. The game has no dependency on a successful Discord report.

The bot edits only its own marked message in the configured guild/channel.
Deleting the board is repaired on the next changed digest; unchanged boards
skip API traffic. Discord 429 responses impose bounded retry delays. Application
logs/observability are disabled; do not enable request-body or Authorization
logging. Cloudflare is the external HTTPS data processor and sees client network
addresses even though our database does not store them. Titles/player counts
are public launcher reports, not cryptographically attested native game facts.

To pause the directory, remove its Cron and disable publishing at the launchers;
remove the bot secret to prevent further posts. Do not delete guild/channel
messages, tables or resources as part of a routine restart. Retain D1's message
ID to avoid duplicating the board. Never rotate `PUBLISHER_MASTER_KEY` casually:
existing pseudonymous owners and grants depend on it. If it or the bot token is
compromised, rotate deliberately and require fresh pairing.

## Local validation

`node --test tools/discord-worker/tests/*.test.mjs` needs Node with `node:sqlite`
(the checked toolchain is Node 24). Tests sign actual Ed25519 payloads and use
production SQL against isolated SQLite. All Discord HTTP calls are mocked.
The isolated workerd/D1 smoke test also passed without real secrets or Discord
outbound traffic. `wrangler deploy --dry-run` bundles successfully. None of these
local checks alone establishes Internet gameplay. The production deployment,
Discord endpoint verification and real empty board are separately confirmed.

With the workspace's private Wrangler toolchain installed as above, run the
real runtime check from the repository root using
`node tools/discord-worker/tests/workerd-smoke.mjs`. It creates only an isolated
local D1 database, uses synthetic keys and mocks all Discord outbound requests.
The test-only entry module exercises actual workerd request construction and a
hostile redirect; it is never the deployment entry. Workers requires
`redirect:'manual'`; its runtime rejects `redirect:'error'` before network I/O.
HTTP 3xx is a failure and never forwards the bot credential. Application and
authenticated bot IDs are fetched separately because application.bot is optional.

Official API references: [Discord app editing](https://docs.discord.com/developers/resources/application#edit-current-application),
[Workers redirect behavior](https://developers.cloudflare.com/workers/runtime-apis/request/).

See [integration and exact state semantics](../../docs/DISCORD-INTEGRATION.md).
