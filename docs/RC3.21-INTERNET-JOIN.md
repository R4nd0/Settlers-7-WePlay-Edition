# RC3.21: Internet Join Hotfix

RC3.20 restored missing public listings, but Windows guests could still fail to
join with a misleading "room creator not ready or reachable" message. The
host was reachable and correctly returned HTTP 403 to a premature request.

## Fix

Before admission, the Windows guest aligns its graphics/content variant using
the coordinator's public lobby **status**. Normal membership admission follows;
only then does the launcher fetch the protected game configuration and start S7.
Previously alignment fetched that protected configuration first, which required
the membership the launcher had not established yet.

The host's access checks remain intact. No firewall, relay, VPN ACL or server
authorization rule is widened. This affects Windows public and private Internet
joining; update the joining PC and restart WePlay. An existing RC3.20 host can
remain open. Updating all participants to RC3.21 is recommended.

## Evidence and scope

The reported two-PC attempt was observed through the real guest's overlay:
the coordinator was connected through relay and later peer-to-peer; TCP 9110
connected and returned HTTP 403 in both cases. This was an application-order
error rather than an unreachable host.

The new regression fails against RC3.20's alignment and passes with the fix.
A Windows guest was also checked against the actual, unchanged RC3.20 native
Linux host service on the existing private WSL network: configuration 403 before
admission; public-status alignment succeeds; configuration stays protected;
normal admission enables configuration 200; leaving restores 403. Renderer
changes were fixtures in that service test; no native game was launched.

The corrected callback was then exercised in the actual RC3.20 packaged guest
process against the running host on the other PC. The directory, cryptography,
elevated overlay, membership, content checks and native game were real. Both
players appeared in the native multiplayer lobby with NEO enabled. Only the
alignment callback was replaced in the private diagnostic process; installed
package/adapter/helper files were retained and all normal guards remained.
This confirms two-PC joining; it does not establish a completed gameplay match.

RC3.20's directory broker and credential-renewal corrections, approved graphics,
morning haze/daytime heat shimmer, Mittsommer and optional NEO remain included.
The previous local browser/crypto and Linux gameplay qualifications keep their
documented scope. This release does not claim a new full two-PC gameplay match.

Use Setup.exe to install or update an existing WePlay application or portable
copy. Original game installations and saves are retained. Back up saves.

[RC3.20 lobby registration and verification](RC3.20-PUBLIC-LOBBY.md) ·
[Graphics implementation](VISUAL-QUALITY.md) ·
[Performance measurements](GRAPHICS-RC319-MEASUREMENTS.md) ·
[Linux instructions](LINUX.md)
