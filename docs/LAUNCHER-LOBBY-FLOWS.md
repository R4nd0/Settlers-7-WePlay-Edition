# Launcher game paths and Internet lobbies

The unreleased launcher presents four equal cards in a two-by-two grid on its
start page. Each entire card is a keyboard-accessible button with a readable
title, a distinct symbol, features and a specific action. Color is an additional
cue, not the only distinction.

| Game path | Main symbol | Features and action |
| --- | --- | --- |
| Solo | One player | Campaign, AI and map editor; starts the existing solo path. |
| LAN | Connected PCs | Same home network and automatic group discovery; opens LAN play. |
| Private Internet | Globe with padlock | EasyTier and private invitation; opens private room controls. |
| Public Internet | Globe with players | EasyTier and Discord publishing; opens the public game directory. |

The two Internet cards show the embedded EasyTier symbol beside its name.
The public card also shows Discord's official symbol beside its publishing
feature. Discord is not presented as the game transport or a requirement for
joining a public match. Symbol provenance is recorded in
[`assets/integrations/README.md`](../assets/integrations/README.md).

On short windows the cards use a compact layout that retains all four symbols,
titles, both feature rows and actions above the fold at the tested 880-by-620
minimum window size. Longer summaries remain in each card's accessible
description and the destination pages. Other home-page content can scroll when
screen space is limited. The full layout adds explanatory summaries. Both
layouts support German and English.

The two Internet destinations retain their separate tabs:

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

The offscreen WinForms self-test verifies the four cards, their navigation to
LAN/private/public pages without joining a network, text fit and accessible
button semantics. It also checks that all four cards remain within the home
viewport at the minimum window size. These are UI fixture checks; they do not
establish a live multiplayer connection or a deployed directory service.
