# WePlay RC3.3 Community Preview

**Support WePlay** in the launcher footer opens the owner's PayPal page at
https://example.invalid/removed-personal-link. The Help page explains that this is voluntary
support for R4nd0 R4nd0 and operation of the WePlay bot. Clicking opens a
browser; the launcher never chooses an amount or submits a payment.

This preview adds integrated EasyTier Internet rooms to the existing local and
LAN modes. It is a new build; the published RC2.1 installer is unchanged.
Internet match compatibility and the known simulation desync require separate
gameplay qualification. Matching files and a connected VPN do not prove either.

The launcher links to the WePlay Discord community and suggests the supplied
server code `j2eQgVzssc`. **Copy for Discord** copies a private room invitation
with joining instructions; paste/send it yourself to the intended players.
The replacement Discord invitation has no expiry according to Discord's public
API. RC3.2 adds the optional automatic public game listing through an external
Cloudflare Worker. The external service is deployed and its Discord HTTP
connection is verified; see
[DISCORD-INTEGRATION.md](DISCORD-INTEGRATION.md). After activation use
`/weplay-hilfe` in #🎮multiplayer, choose **Launcher verbinden**, and paste the
private connection code into **Mit Discord verbinden** in the Internet tab.
The public-display checkbox can be turned off before launch. The grant is kept
in the current user's Windows Credential Manager. The bot runs externally and
does not require an always-on private PC.

## Start an Internet room

Install the new preview, select your original History Edition when asked, and
let WePlay prepare a separate game copy. Open **Internet play**, enter your
player name and choose **Create room and start**. Windows asks permission for
the network helper. Copy the invitation with its dedicated button and send it
privately to your friends. They paste it into **Join room and start** using the
same WePlay build. Select the actual match once inside S7's multiplayer menu.

The default **No own server · public-assisted** mode uses public EasyTier nodes
for discovery/NAT traversal and, when necessary, relay traffic. It needs no
account or rented server. **Direct connections only** uses no public bootstrap
or STUN nodes and refuses relay paths; its creator needs a reachable numeric
endpoint. Some Internet connections cannot establish direct peer traffic.

The room creator remains its coordinator and must keep the PC connected. If the
creator exits its own game while guests still play, WePlay keeps the room open.
After those guests leave it closes automatically. **Close Internet room** closes
an idle hosted room; finish your game first. Closing the starter during a game
keeps it in the tray, as before.

## Invitations and Windows permission

An invitation is a group secret. Share it only with trusted players. It is
masked in the interface and travels over a separate current-user-only Windows
pipe, never through ordinary backend output or diagnostics. It is not saved as
a player preference. The copy button changes the clipboard only when clicked.

Only the network helper is elevated. The game remains a normal child of the
unelevated launcher. The helper owns its EasyTier process, temporary protected
configuration and scoped firewall rules. Its IPC directory explicitly grants
inheritable access to the invoking user, administrators and SYSTEM. It watches the exact parent process
creation so a reused process ID cannot keep an orphan tunnel alive. Regular
cancellation, parent exit and failed startup close its owned resources.

Game/Python/.NET inbound firewall permission is scoped to this room's adapter,
local address and /24 subnet, including when Windows classifies it as Public.
Outer EasyTier permission is scoped to its own executable. Default routes,
system DNS, unrelated adapters and unrelated firewall rules are not changed.

## Development and verification

The runtime is a pinned, modified Windows x64 EasyTier v2.6.4 build without the
stock Npcap dependency. See [runtime build instructions](../tools/easytier/README.md)
and [architecture research](INTERNET-LAN-OPTIONS.md). The game/native guard and
its exact-peer admission leases remain unchanged.

The initial verification includes targeted Python regressions, a compiled
bilingual interface check, a real private .NET/Python invitation-pipe exchange,
and two real EasyTier processes connecting directly over loopback with no TUN
adapter, STUN, public bootstrap or game. A separate minimal copy also passed
the actual Windows UAC/Wintun lifecycle: one /24 adapter and eight own firewall
rules were created and removed, private files were removed, and default routes
and DNS were unchanged. That check used no public nodes and started no game.
These checks do not establish an Internet match or a desync fix. Actual
Internet gameplay with two PCs must be qualified separately before release.

The installer includes `Source/EasyTier-corresponding-source.zip`; the same ZIP
is supplied alongside Setup.exe. It contains the patched source, Cargo.lock,
patch and build instructions, with upstream native binaries excluded.
