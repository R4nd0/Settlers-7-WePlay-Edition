# Global public directory service

The central service is running at
`https://weplay-multiplayer.weplay-community.workers.dev`.
Its [health endpoint](https://weplay-multiplayer.weplay-community.workers.dev/health)
and public game-list route were verified over normal certificate-validated HTTPS
on November 4, 2026. The existing Cloudflare Worker, D1 database, secrets and Free
plan were reused. No domain purchase, new account, paid plan or global shared
EasyTier network was introduced.

## Launcher and Discord

The RC3.14 package configures this address automatically. Opening Public
lobby reads metadata and refreshes the visible list every 15 seconds. Offering
and joining games do not require a Discord account. Each selected match retains
its bounded EasyTier room and explicit per-connection consent. Native content,
callback, executable and admission checks remain unchanged.

**Also publish on Discord** belongs in **Public Internet / Public lobby**. It is
an additional, explicit checkbox, with linking controls beside it. Its credential
does not belong in the private invitation flow. A private room's **Copy invitation
+ instructions** action remains available for privately sharing an invitation,
including through Discord; it does not publish a directory entry or a channel post.

Independent public hosts use a random in-memory secret, bounded registration and
short-lived credentials. Only its digest is retained remotely. It cannot call the
paired Discord publisher routes. Games using those independent grants are excluded
from the Discord board and its join menu. A host explicitly choosing Discord uses
the existing paired reporting and encrypted-join path for that same public game.

Registration is initially bounded to six new grants per source per minute and
64 independent credentials. Reports retain the existing 16-active-host bound.
Credentials renew during hosting and expire after five minutes without renewal;
minute maintenance removes them and their dependent metadata. Source rate buckets
contain short-lived keyed hashes rather than raw addresses. These bounds limit
resource use; they do not attest that a modified client reports truthful game facts
or guarantee availability under abuse or provider quota exhaustion.

The package's new `public_game_directory` capability ensures that the additional
host module is included and verified. Older packages remain valid and retain their
older host flow. The published RC3.13 downloads have not been replaced.

## Approved address migration

The owner explicitly approved moving the existing sole Worker to the neutral
community subdomain, invalidating the one previous publisher grant and updating
the existing Discord bot's interaction endpoint. There were no active public
reports or room leases before cutover. The old personal address is not included
in public configuration or documentation.

After Cloudflare activated HTTPS, Discord accepted and confirmed the new endpoint
`https://weplay-multiplayer.weplay-community.workers.dev/interactions`.
The temporary fixed-target activation entry was then removed and the regular
Worker redeployed. Bot secrets remained inside the existing Worker throughout.
No synthetic game was posted to Discord or the production game directory.

Previously linked publishers need a fresh private code from `/weplay-hilfe`.
Existing RC3.13 readers can obtain a fresh directory code there. New builds use
the central default without that reader-linking step. Account IDs, database IDs,
OAuth material and raw operator logs remain outside Git.

## Verification and limits

The production central service carried a real guarded Ubisoft host's actual native
lobby metadata. A second actual game client displayed it in WePlay Online, selected
it, confirmed normal restart and obtained its encrypted room grant through the
production HTTPS broker without Discord credentials. Two real pinned EasyTier
processes established mutual P2P membership with the delivered room identity.
The restarted guest joined the native host lobby; both game windows and the native
owner-filtered projection showed two players. The clients were closed through the
normal game menus, the public report/lease withdrawn and both private test copies
restored. No original installation was changed.

This was a same-PC laboratory using guarded executable aliases. Native game traffic
used the existing loopback laboratory service. EasyTier ran without TUN and without
public nodes. It verifies the real central directory, encrypted admission and native
lobby rejoin together; it does not qualify two remote PCs, a complete packaged
launcher flow over TUN, Steam live UI or a match world's simulation stability.
No active world was started and no live Discord game message was sent.

Separate local checks cover production SQL and host isolation, real workerd/D1 with
two independent .NET cryptography clients, and German/English launcher fixtures.
The existing network-isolation code and native payloads were not modified.
