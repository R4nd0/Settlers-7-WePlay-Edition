# WePlay RC2 verification

Recorded 2026-10-29. This is an experimental installation/launcher candidate.
The known multiplayer desync is **not fixed or qualified as fixed**.

## Verified

- Full Python regression: 274 cases, 273 passed and one optional real-game
  integration skipped. Two HTTP fixture failures in the restricted execution
  environment passed with normal loopback access; the complete suite then
  passed with that access. After the test-only early-EOF regression was added,
  its entire 20-case suite passed again. These runs cover 275 distinct baseline cases. Three additional destination-change
  regressions were then added and passed in the complete 26-case desktop suite.
- Automatic LAN policy: 21 checks; launcher: nine checks. Coverage includes
  simultaneous idle election, active priority, stale beacon/live content
  differences, known renderer variants, conflicting/full groups, NIC source
  binding, cancellation/cleanup and full file verification under the copy lock.
- Desktop integration: 26 checks; installer: 20; graphics launcher: eight.
  These are subsets of the regression coverage, not extra real-game sessions.
- GUI build succeeds. Bundled desktop runtime self-test succeeds on a host
  whose global desktop runtime is older. English/German pages render with
  matching translation keys, an encoding guard and labeled input fields.
- Generated GUI fixture images were inspected: the LAN page has a player-name
  field and one LAN-start button, without IP, network or external room choices.
  These images are component-test renders, not a capture of the live game.
- Real Setup installation and update exit successfully. The supported original
  installation is detected, a separate approximately6.3GB copy is prepared,
  graphics probing/profile activation and full manifest verification complete.
  Real repair also exits successfully and publishes its completed installation
  state. Personal-file preservation fixtures are included in the update/repair
  checks. The original game installation is not modified.
- The final LAN modules retain their frozen working-file SHA-256 values:
  `launch_auto_lan.py`: `ba4a8a884ce3d66fb3bba53705e30250a344716ae8d79662a273a35127fec2a2`;
  `lan_autoconnect.py`: `b37e0d0b1ebce3e57e1f956954acf08fa96b17286eb63675103cebb9d0786e41`.

## Destination-change correction

A real pre-release GUI test exposed a metadata defect: after changing the game
copy destination, the earlier instance IDs were resolved against the new
container. The completed current copy was intact, but the launcher reported
`installation_invalid`.

Installation record v2 stores the container for every previous instance.
Same-container repair, relocation, cleanup and uninstall now use those separate
references. Legacy recovery accepts only exact instance IDs with this app’s
ownership marker in its original default location; it does not scan drives or
adopt foreign folders. The original record is backed up byte for byte.

The affected installed copy was recovered without copying or patching the game
again. Its real backend reports `installed=true`, `needs_repair=false` and
`update_available=false`, including the active copy in the user-selected
container and the older copies at their original location. The launcher was
reopened at the user’s request. The full desktop suite passes 26 cases,
including relocation, legacy recovery and cleanup/uninstall with personal data
retained across containers.

The compiled maintenance entry point also completed with exit0 against a
separate small, owned fixture. It removed the unchanged owned executable and
retained personal, modified and unknown files. This fixture test did not remove
the user’s prepared game or substitute for a live match test.

## Still open

The graphics chat's final capture of the installed starter timed out while
Codex reported a pending approval. No alternate UI channel was used to bypass
that state. The final click-through from this installed GUI into S7, automatic
LAN and return/cleanup therefore remains **unconfirmed** at this source
publication. The setup candidate is not described as fully live-qualified.
The graphics chat retains ownership of that test and final Setup publication.

An optional elevated private-firewall change and removal of a full real-game
installation have not been established by the final live pass. The compiled
maintenance fixture and automated ownership checks do not substitute for those
actions. The user’s current prepared copy was deliberately retained.

There is no new multi-PC qualification of this combined automatic-launch RC,
three-client run, endgame load run or long-session checksum comparison. Earlier
two-PC match starts and the confirmed reverse-host desync remain documented
in [TWO-PC-DELIVERY.md](TWO-PC-DELIVERY.md). With different renderer profiles,
start one PC first, then the others; simultaneous first starts can require
retrying LAN start after the desktop aligns a known profile.

See [RC2-START.md](RC2-START.md), [RC2-BUILD.md](RC2-BUILD.md) and
[AUTOMATIC-LAN.md](AUTOMATIC-LAN.md) for usage, pinned build inputs and bounds.
