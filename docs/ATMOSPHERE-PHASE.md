# Morning haze and daytime heat

Unreleased candidate, 2026-11-07. Visual acceptance and RC publication remain
pending. The earlier GPU and multiplayer results predate this clock bridge.

Maximum now uses the actual rendered Mittsommer sun angle, not process uptime.
Morning haze rises over orbit fractions .015–.07, stays full until .18, and
clears by .31. Heat rises over .32–.42, stays full until .65, and fades by .76.
There is no overlap and no dark night. Color grading, GI and edge smoothing
remain independent. Haze covers the full image; only refraction excludes compact
HUD footprints. Heat uses the existing warm/lit depth proxy, up to 0.85 pixels
before masks, rather than a physical material temperature.

## Read-only native clock

`native/atmosphere_addon.cpp` is a small x86 ReShade bridge. Exact Ubisoft and
Steam executable hashes select independently checked ASLR layouts. The reader
validates image size, bounded environment/timeline vectors, active slot/ID,
record and scalar-timeline vtables, the `WePlayMidsummer` name, 20-minute period
and five azimuth keys (0/90/180/270/360). Every pointer read is bounded and uses
`ReadProcessMemory` on the current process. Unsupported state disables haze and
heat instead of inventing another clock. It makes no game call or memory write.

The rendered angle controls the phase. The native timeline clock controls
refraction movement, so stopping that clock also stops the ripple. Time is
reduced modulo the cycle before float conversion to retain precision in long
games. The wrap occurs where heat is off. The callback saves/restores the x87
floating-point environment and SSE control/status register.

Build with `native/build_atmosphere.ps1 -OutputDirectory work/<new-directory>`.
This runs 148 synthetic reader/FPU checks, including both layouts, ASLR,
invalid vectors/vtables/keys, NaN/infinity, unreadable memory, pause and long
clock precision and x87/SSE state restoration. They establish reader behavior,
not actual Steam gameplay.
The separate `-Lab` build additionally accepts the owned diagnostic executable
and 12-second fixture period. Production accepts neither lab exception.

New Windows Mittsommer packages require the pinned build through
`--atmosphere-addon`. Installation preserves it under `Runtime/Graphics`.
Maximum alone copies it into a content-fingerprinted `ManagedAddons` profile;
the launch check rejects changed bytes or extra add-ons. Other profiles retain
an empty `NoAddons`. The official SDK revision and licenses accompany sources
and the enabled profile. No binary is committed to this repository.

## Actual local checks

The owned late-save client loaded the revised shader and bridge successfully.
A short accelerated 12-second orbit reported changing real angles and ready
state. The morning screenshot fixture stopped at 48 degrees; independent
read-only samples confirmed an unchanged native clock. At this angle haze is
full and heat is zero. The day fixture used the normal 20-minute resource and
running simulation, reporting 222–293 degrees across its bounded run. Haze was
zero; heat began full and faded later in that afternoon segment.

The same close camera and late save supplied the review images. The moving
ground excerpt uses actual screenshots at about four samples per second and
contains normal game animation too; it cannot substitute for a smooth full-frame
visual acceptance of refraction. No scene colors or effects were retouched.
Both clients exited and their private source/profile backups were restored.
No user installation, published release, save format or NEO logic was changed.

Actual new Steam, Linux, separate-PC and multiplayer qualification of the bridge
remains pending. Independent Steam disassembly/layout mapping and synthetic
checks are not runtime qualification. The RC stays on hold until visual review
and the remaining release checks are complete.
