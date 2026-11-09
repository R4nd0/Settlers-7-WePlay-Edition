# RC3.25 - Standalone Update Hotfix

RC3.24 could download successfully, close the launcher and then fail before
updating an existing standalone installation. Its silent installer ran a new
preflight script against the installed RC3.23 backend and called the missing
`Desktop.verification` method. The installer stopped before replacing launcher
files. This was an upgrade compatibility defect in RC3.24.

RC3.25 makes the scoped verification cache optional for older backends. The
existing lock, complete package verification, game-file checks and ownership
guards remain required. The failure is reproduced against the actual frozen
RC3.23 backend, and the corrected script also rejects modified game files.

## Updating

Reopen the existing launcher and check for updates. It offers RC3.25 directly;
installing RC3.24 first is unnecessary. Alternatively run **Setup.exe** and choose
the existing standalone outer folder or its **App** folder. Game copies, local
settings and saves remain in place.

Windows: use **Setup.exe** or **WePlay-RC3.25.zip**. Linux:
**WePlay-RC3.25-Linux-x64.tar.gz**; see the [Linux guide](LINUX.md).

Includes all [RC3.24 launcher fixes](RC3.24-LAUNCHER.md) and earlier graphics and
lobby corrections. The native adapter and gameplay/network contracts remain
unchanged. [Verification](verification/rc325-20261109.json) records the legacy
preflight evidence; the publication receipt records final artifact and upgrade
checks. No new game match or FPS improvement is claimed.

Remaining shadow flicker and vegetation sharpness across all scenes are still
open qualification limits. Background: [visual-quality guide](VISUAL-QUALITY.md),
[measured RC3.19 investigation](GRAPHICS-RC319-MEASUREMENTS.md),
[Internet join correction](RC3.21-INTERNET-JOIN.md).
Update metadata is cryptographically signed; executables are not Authenticode
signed.
