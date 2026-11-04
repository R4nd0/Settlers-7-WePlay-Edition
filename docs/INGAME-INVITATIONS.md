# In-game invitations (development candidate)

The session lobby's **Invite / Einladen** button previously called
`Network.ShowInviteOverlay`, which depends on the unavailable Ubisoft overlay.
The candidate replaces that callback and the legacy friend-invite callback in
the hash-locked host-lobby Lua member. Unsupported SDK exports remain guarded.

The replacement behaves as follows:

| Current mode | Button action |
| --- | --- |
| Private Internet room creator | Open the launcher invitation dialog; copy the current private room code explicitly. |
| Public Internet room creator | Open the launcher dialog; copy a public directory code. Recipients connect that directory and select the match. This is not a game-specific deep link. |
| LAN | Explain that other players choose LAN in WePlay and join the match. |
| Internet room guest | Ask the room creator to share the invitation. |
| Solo, missing helper or older launcher | Show safe guidance; never call the Ubisoft overlay. |

Clicking the button does not post to Discord, join a network, change firewall
rules, exit S7 or start another game. Existing explicit EasyTier consent still
applies when the recipient joins. Codes use WePlay's existing paste flow;
they are not clickable web URLs.

## Local bridge

The native console bridge accepts two fixed invitation commands. Its snapshot
contains only a fresh per-game nonce and mode. Its request contains only that
nonce. Fixed filenames, bounded reads, non-reparse file guards and exclusive
request creation apply. Requests are checked again by the owning backend and
rate limited. Neither Lua nor these files receives an EasyTier room secret.

The launcher accepts a request only for its current running game and creator
role. Private codes stay in the existing current-user named pipe and launcher
memory until an explicit clipboard copy. Public mode shares only a public
directory code. Game exit closes the dialog, clears its retained code and
removes the bridge files. Repeated clicks reuse one dialog; clipboard failure
keeps it open for retry.

## Verification boundary

Lua 5.1 fixtures exercise each mode and malformed/missing snapshots. Archive
readback verifies that only the global browser and host-lobby members change,
and compiles both modified members. Native fixtures cover both Ubisoft and
Steam address mappings, nonce/mode rejection, missing files and thread/init
guards. Backend fixtures cover mode selection, stale requests, cleanup and
rate limiting. The packaged bilingual launcher self-test uses synthetic codes
and an injected clipboard function, leaving the actual clipboard untouched.

The actual Ubisoft lab game-button click was checked in two clean cold runs:
LAN showed its guidance and returned to a responsive lobby; private-mode
fixture dispatch delivered exactly the current nonce and mode to the real
Python handler. Both clients closed normally with exit code 0 and healthy
memory sentinels. The private-mode test stayed loopback-only and did not
establish an Internet room. The launcher dialog and copy action were tested
separately with synthetic credentials. A real Internet recipient join and a
Steam game-button click remain unverified. This candidate is not a published
RC and has not been installed over the user's active game copy.
