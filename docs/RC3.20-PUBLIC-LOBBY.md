# RC3.20: Public Lobby Hotfix

RC3.19 could start a public host without registering its joinable room, leaving
the public game list empty. Install RC3.20 on the hosting computer; updating all
participants is recommended. Setup.exe updates existing launcher and portable
copies while retaining the game and saves. Original installations stay intact.

## Fixes

- Windows and Linux now pass the admitted host's room identity directly to the
  directory broker before starting the game. The external room-ready callback
  remains gated by membership, content and launch checks.
- Public-host credential renewal now extends its expiry atomically. Previously
  renewal returned success while leaving the five-minute expiry unchanged.
  This server correction is deployed to the existing directory service.
- Startup regression tests exercise both shipped launcher paths together with
  their real Internet/LAN orchestration. Directory Worker tests now run in CI.

## Verification

The new startup regression fails against the frozen RC3.19 Tools on both
platforms and passes with this fix. A running workerd/D1 instance was checked
with both startup paths, an independent reader, compiled cryptography, full-room
refusal and removal after close. Twelve minutes of simulated server time cover
credential renewals and scheduled expiry without a long idle game run.

Two owned local native clients were also exercised against the live HTTPS
directory: the real host appeared in the guest's **WePlay Online** browser;
its explicit Join selection produced a nonce-bound request and the compiled
crypto helper recovered the exact host room. Native game traffic used the
guarded loopback laboratory service. This verifies public discovery and join
handoff locally; a separate-PC Internet data-plane test remains pending.

## Graphics and NEO

RC3.19's approved graphics, full-image morning haze, daytime heat shimmer,
Mittsommer cycle and optional NEO logistics remain included. This hotfix changes
launcher/directory behavior. Back up saves before updating.

[Lobby defect and test scope](PUBLIC-LOBBY-RC320.md) ·
[Graphics implementation](VISUAL-QUALITY.md) ·
[Performance measurements](GRAPHICS-RC319-MEASUREMENTS.md) ·
[Graphics release details](RC3.19-GRAPHICS.md) ·
[Linux instructions](LINUX.md)
