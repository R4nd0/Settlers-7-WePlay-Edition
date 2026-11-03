# RC3.13 — Public Lobby and Network Isolation

The launcher now distinguishes **Public lobby** from **Private room by
invitation**. The public list shows published native games, player counts, open
slots and compatibility. Choose a match to join its room; select the actual
match inside S7 afterward. Private invitation rooms are not published by this
path. The in-game **WePlay Online** browser uses the same directory.

Before every new EasyTier connection, WePlay explains the virtual network,
public rendezvous/relay use, address visibility and remaining risks. Consent is
unchecked initially; **Agree and connect** stays disabled until checked. Cancel
starts no tunnel. Browsing or refreshing the directory needs no tunnel consent.
An in-game room switch closes the previous game/transport, then asks again in
the launcher before connecting to the new room.

The managed tunnel denies other services and forwarding by default. Only the
fixed game and coordinator/discovery ports and their replies are permitted.
Windows allow rules are restricted to their owning programs and ports. IPv6,
exit-node/DNS changes and optional proxy inputs stay disabled. The pinned
EasyTier build also rejects malformed or fragmented IPv4 before ACL port checks.
The runtime and its corresponding modified source accompany this release.

The repeated startup-options reset question is answered **No** automatically,
preserving existing settings on supported Ubisoft and Steam builds.

Opening **App/S7Launcher.exe** directly in a private standalone export now
recognizes and binds its bundled game, including after moving the whole folder.
It no longer opens source installation setup simply because the CMD starter
was skipped. Incomplete exports show a specific recovery message.

## Start using the public lobby

Use `/weplay-hilfe` in the WePlay Discord and select **Spieleverzeichnis
verbinden**. Paste its reader code under **Public lobby → Connect**. Publishing
uses the separate **Launcher verbinden** publisher link. Then choose **Offer a
public game**, approve the connection and create a public native S7 match.
Empty rooms and private native sessions are not listed. Private invitation play
continues to work without a Discord link.

## Verification and limits

Two real local Ubisoft laboratory clients completed public listing, encrypted
invitation delivery, guest restart without the options-reset prompt, and native
lobby admission with two players. Game traffic in that harness used guarded
loopback; the EasyTier peers verified the delivered room identity without TUN.
The separate actual-TUN receiver test passed against an unrestricted private
sender: permitted TCP/UDP arrived; controlled other ports and SMB/RDP probes
were blocked. Six production firewall rules were checked and removed, along
with the test adapter. See the release verification record for scope.
Bilingual launcher rendering and initially denied consent are checked, alongside
native, protocol and installer tests.

This remains a prerelease. These checks do not establish two-remote-PC Internet
gameplay, Steam's global-browser live flow, real Discord publication, long-match
stability or freedom from desync. Port isolation does not eliminate defects in
S7/EasyTier/Windows or denial of service. Shared room invitations are credentials;
individual transport revocation is not provided, so close and recreate a room
if an invitation must be revoked.

Install RC3.13 on every player's PC. No game files or credentials are included.
Steam-based copies retain the genuine Steam ownership/client requirements.
Earlier releases and downloads remain available.
