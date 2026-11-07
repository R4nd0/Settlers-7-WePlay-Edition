# RC3.19: Graphics and Midsummer

Visually approved graphics preview. Update every participant: lighting resources change
the content fingerprint even with NEO off. Back up saves; older releases remain.

## Graphics

- Longer model, tree and settler visibility, sharper textures and an independent
  extended shadow range are available in the Windows launcher's Graphics page.
- New independent options add spatial edge smoothing, depth-based contact
  shading and Ultra shadows up to 8192, with more precision near the camera.
- Mittsommer/Midsummer adds a 20-minute bright twilight/day cycle without a dark
  night and a complete sun orbit with moving shadow directions. Choose it in
  the game's Environment menu. Natural tone is independent.
- **WePlay recommended (Modern)** selects high quality, longer detail/range,
  4096 shadows, natural tone, edge smoothing and contact shading. Ultra stays off.
  Save and restart. Existing saved choices are preserved.
- **ReShade Maximum (cinematic)** adds screen-space indirect light/color bounce,
  occlusion, SMAA, bloom, cinematic grading, morning haze and daytime heat
  shimmer. Light and haze cover the full image; only heat distortion protects
  HUD controls. Ultra stays separate. The short 1080p near-camera
  sample used about 7.3 ms GPU busy time versus 4.4 ms for recommended settings.
- Ultra is optional: it used about 250–260 MiB more dedicated GPU memory than
  the recommended candidate on the tested Radeon RX 9070 XT. GPU busy time was
  about 0.6–0.8 ms higher. Results vary by hardware, scene and resolution.

ReShade is downloaded from its official site only when the selected effects
need it. Setup is unpacked, never run. No global layer is installed. Contact
shading uses the pinned full runtime. Maximum includes a guarded read-only sun
clock; external add-ons are refused.
Missing depth disables geometric effects without diagnostic colors. Shader
sources, SMAA lookup textures and their licenses accompany the optional profile.

## Installation and updates

Use Setup.exe for a new launcher installation or an existing WePlay copy.
The managed installer retains original lighting resources for later repairs.
An earlier unpublished Mittsommer development copy without that saved original
must be prepared from the original game once. Original installations stay intact.

RC3.18's NEO FIFO, explicit individual priorities, workshop pickup correction,
eligible road-distance selection, resume fixes and Internet-room checks remain.
NEO is still optional per map. Graphics choices are local; peers need matching
game resources, with normal synchronization checks retained.

## Verification and limits

Three matching late-save camera positions at 1920x1080 were measured with pinned
PresentMon 2.6.0. All nine 15-second captures completed without lost events,
overflow or frame intervals above 50 ms. All profiles produced about 60 frames/s
at the existing cap; this is not an uncapped throughput result or displayed-FPS
guarantee. GPU busy time was measured separately. One machine and one pass per
profile do not establish universal performance.

The prior graphics multiplayer reload comparison matched 451/451 structured
checksums. Separate-PC testing of these new graphics changes remains pending;
loopback results are not a substitute. Internet data-plane and new Steam/Linux
graphics gameplay are not newly qualified. See the detailed verification docs.

The complete light orbit was checked with a private 12-second version of the
same curves at active simulation speed; the shipped period remains 20 minutes.
Paused screenshot fixtures do not qualify moving light or animation. A separate
20-minute local multiplayer attempt advanced about 9750 turns before its test
budget expired; its incomplete replay streams are not a checksum pass.

[Graphics implementation and tests](VISUAL-QUALITY.md) ·
[Performance measurements](GRAPHICS-RC319-MEASUREMENTS.md) ·
[NEO and Internet changes](RC3.18-LOGISTICS-INTERNET.md)
