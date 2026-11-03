# Private Internet play and Public lobby

The unreleased launcher separates the two user flows into different tabs.

**Private Internet play** creates a room for invited players or accepts a private
invitation. It contains player name, host/guest selection, optional connection
settings and private invitation controls. It does not publish matches to the
directory or Discord. Public directory and publisher setup are absent here.

**Public lobby** lists publicly offered matches and provides public hosting and
joining. Host access and Discord board configuration live here. Public
connections keep their progress and leave controls on this tab; the launcher
does not switch to a private invitation screen or expose its secret invitation
controls for a public operation. Joining needs no publisher credential.

Both flows retain the EasyTier explanation and explicit consent before each
new room connection. Listing game metadata does not itself join a virtual
network. Leaving while S7 is running still requires the regular game shutdown.

A public directory for everyone does not require one shared EasyTier network
for every match. The existing design keeps a common metadata directory and
automatically joins the host's bounded room after a player chooses a match.
Per-room isolation and the existing restrictive network policy are retained;
this UI change does not widen peer access or deploy a global shared network.

The shipped directory configuration currently has `origin: null`. A reachable
public directory service must be configured before an automatic worldwide list
can be offered. Existing directory codes and publisher linking remain supported.
The UI says when that service is not configured; an empty local fixture list is
not evidence of an operational public service. No new service was deployed as
part of the update repairs.
