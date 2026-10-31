# WePlay signed update patcher

The RC3.4 bootstrap and later launchers can check for signed preview releases at
startup and on demand. Checking does not hold the game operation lock or require
sign-in. It reads up to twenty releases from the fixed public repository
`R4nd0/Settlers-7-WePlay-Edition` and checks the newest eight releases containing
`weplay-update.json`. This intentionally includes prereleases: the existing
RC2/RC2.1 releases are prereleases and GitHub's `/releases/latest` would omit them.
Existing releases without signed metadata do not produce a false update notice.

When a signed release has a greater sequence than the installed launcher, the
launcher shows the version and release notes. Installing is a separate explicit
action. It downloads the complete `Setup.exe`; this initial patcher uses no ZIP
extraction, binary delta, remote command, arbitrary URL or extra runtime library.
The normal installer remains available for initial installation and recovery.
The home page displays a prominent update notice and install button; a footer
install link is visible across pages, including initial game preparation. The
Updates tab shows notes, a manual check and the previous failed-install status.
An unavailable feed produces a nonmodal notice without blocking game launch.

## Trust and local state

The shipped `update-key.json` pins an RSA-3072 public key. Release metadata is
canonical ASCII JSON signed with RSA PKCS#1 v1.5/SHA-256. The Python standard
library verifier checks the complete encoding and signature, fixed repository,
preview channel, version, monotonic sequence, tag, exact asset URL, size and
SHA-256. The Windows helper independently repeats signature and installer checks
with Windows cryptography before execution. Only the public verification key is
shipped. A hash in an unsigned GitHub asset is not accepted as authenticity.

HTTPS requests do not send tokens, cookies, account data, saves or configuration.
Redirects are bounded and allowed only to GitHub and GitHub's download storage
hosts. Downloads are bounded to 768 MiB and written into a new private staging
directory. Invalid, changed, cancelled or partial downloads never become an
executable update. `State/Updates/highest.json` remembers the highest verified
sequence and its payload digest, preventing a later old feed from downgrading
the offer. Cache commits use a separate short local lock so a startup check can
coexist with the main game UI. This does not prevent a provider/network attacker
from withholding updates; authenticity and replay resistance are not an uptime
guarantee.

The installed `desktop-release.json` includes `version`, `update_sequence` and
`update_channel`. The initial trusted bootstrap has sequence 1. Updates must
increase the sequence even when a displayed version is reused. Do not sign two
different payloads with the same sequence. Signing-key rotation needs a trusted
launcher migration; silently replacing the key breaks the existing trust chain.

## Safe handoff and recovery boundary

The backend checks the actual parent launcher PID, executable path and Windows
creation token, then downloads and verifies the installer. RC3.5 copies a verified
native `WePlay.UpdateHelper.exe`, public pin and signed manifest under
`State/Updates/<random-id>`. The helper embeds its public verification key and
does not depend on the application's Python/.NET runtime files, so the old runtime
is not held open during replacement. The final installer hash check and launch
retain a non-writable file handle. RC3.4 used a PowerShell helper; the new package
contains no scripts and requires no change to Windows execution policy.
The helper opens and verifies exact launcher/backend process handles, takes the
same per-app update mutex checked by the launcher, and acknowledges readiness
before the old UI exits. Reused PIDs, changed helper files and unexpected paths
are rejected. It then waits for both old processes to exit, refuses running S7
games or local S7 services, and checks the installer hash again. A reopened
launcher is blocked while the helper holds the mutex. The verified installer
runs with a fixed silent argument list targeting the same app folder; after
success the helper releases its mutex and starts the new launcher.

Preferences, Discord grants and saved games are not update payloads. Setup's
source contains no `State`, `Instances` or `LocalData`; those local directories
remain in place. A changed copied-game contract uses the existing staged
installation transaction: create and verify a separate game instance, copy
supported preferences/saves and atomically switch `installation.json` only
after verification. The previous instance stays available. GUI-only changes
do not demand another full game copy.

Recovery is deliberately limited: a failed Setup relies on Inno Setup's normal
installation transaction; a failed new game preparation retains the active old
instance. There is no automatic post-update health rollback, simulation test or
delta-patch rollback. If the installer itself fails, the helper records a fixed
error under `State/Updates/status.json`; the downloaded verified Setup and prior
game instance remain available for repair. Unknown files and personal data are
never deleted to make an update succeed. An obsolete or unsigned launcher needs
the bootstrap installer once before it can recognize signed future updates.
If an existing RC3.4 handoff is blocked by its current script policy, run the
RC3.5 installer directly once rather than changing that policy. See
[Windows helpers](WINDOWS-HELPERS.md).

## Maintainer: signing and publishing

The signing key was generated locally at `work/updates/signing-key.dpapi`. It is
an encrypted CurrentUser DPAPI blob, not a plaintext private key. Its encryption
depends on this Windows user's DPAPI profile; preserve an appropriate secure
signing-machine/profile backup. Never commit the blob, export a raw key into
logs or ship a private key. The public pin is
`tools/installer/update-key.json`. `sign_manifest.ps1` refuses to overwrite an
existing key/pin pair. No user-facing updater can mint release signatures.

After building and testing a future installer, use a new sequence and tag:

```powershell
python -B tools/updates/build_release.py --setup outputs/Your-Verified-Release/Setup.exe --output outputs/Your-Verified-Release/weplay-update.json --version 2.0.0-rc3.7 --sequence 5 --tag v2.0.0-rc3.7 --notes-file work/updates/release-notes.txt
```

The tool validates the metadata, signs inside Windows cryptography, verifies the
finished envelope against the shipped pin and writes a new manifest. It does
not upload anything. Its result prints a reviewable `gh release create` argument
list including both assets and the fixed repository. Publish only the reviewed,
verified installer and its matching signed manifest to that exact tag. Example:

```powershell
gh release create v2.0.0-rc3.7 outputs/Your-Verified-Release/Setup.exe outputs/Your-Verified-Release/weplay-update.json --repo R4nd0/Settlers-7-WePlay-Edition --prerelease --title "WePlay 2.0.0-rc3.7"
```

Publishing is an operator action; the implementation does not silently create
or overwrite a release. Existing published assets remain intact. The signed
RC3.4 bootstrap is published as v2.0.0-rc3.4 (sequence 1). RC3.5 is published as
v2.0.0-rc3.5 (initial sequence 2, privacy rebuild sequence 3). RC3.6 uses sequence 4;
RC3.7 Castle Restoration uses sequence 5.
Building/signing a local candidate does not publish it
or update a user's PC; publication remains a separate operator action.
Maintainer signing tools may use PowerShell/CNG locally; they are not shipped
and do not add a PowerShell prerequisite to RC3.5 player operations.

## Validation

The updater tests use an independent temporary CNG/DPAPI fixture key, invented
installer bytes, isolated app/state/save directories and mocked HTTPS responses.
They cover canonical/signature validation, foreign hosts and payloads, unsigned
prereleases, replay, overlapping cache commits, tampered/cancelled downloads and
Windows process identity. Windows PowerShell's validation mode rechecks a valid
signature/hash and rejects tampering while never executing an installer. A
production-mode waiting fixture verifies real process handles and the exact
named mutex, then stops the helper before any invented installer can run.
These tests do not alter a real app, saved game, registry or published release.

Five additional opt-in Windows lifecycle fixtures build harmless temporary
launcher, backend and installer executables using the local .NET SDK. They
exercise readiness, the real named mutex and exact process-exit wait, successful
installation/restart, installer failure without restart, incorrect process
creation tokens, concurrent helpers and installer tampering after readiness.
They use a disposable signing key and isolated paths containing spaces. All five
pass. The packaged RC3.4 launcher also passes its compiled UI/private-pipe test;
a real feed check reports no_signed_release until signed assets are published.
