# RC3.22: Graphics Preset Recovery

Fixes graphics saving failing with "The WePlay graphics profile was changed;
save your graphics selection again" after an ordinary game exit. ReShade
rewrites its generated preset, including line endings, key order, technique
defaults and transient clock uniforms. Previously saving an existing profile
only verified it and could not restore that generated file.

Saving and preparing the next launch now reconstruct the managed preset from
the selected WePlay options. The user's graphics-choice record, display, GPU
and quality choices remain the authority. The first changed preset is retained
once as `WePlay-Midsummer.before-repair.ini`; repeated game exits do not create
an expanding backup history.

Runtime DLL and layer hashes, shader/texture bytes, configuration paths and
external/add-on restrictions are checked before any preset repair. The game
must be closed before rewriting it. Public profile verification remains strict.
Changed DLLs, shaders, layer manifests or unapproved add-ons are still refused.

Regression tests cover repeated runtime rewrites with Maximum off/on, saving
and launching again, a missing generated preset, a running game, and changed
runtime/shader/add-on files. The reported PC's two existing profiles were
restored after their other files passed verification.

RC3.21's Windows public/private Internet join correction and RC3.20's public
registration/renewal corrections remain included. Approved graphics, the
Midsummer cycle, morning haze/daytime heat shimmer and optional NEO are retained.
This hotfix changes profile management, not shader appearance or simulation.

Use Setup.exe to update the existing application or portable copy, then prepare
the existing owned game when requested. Original installations and saves are
retained. Back up saves before updating.

[Internet joining and two-PC lobby verification](RC3.21-INTERNET-JOIN.md) ·
[Graphics implementation](VISUAL-QUALITY.md) ·
[Performance measurements](GRAPHICS-RC319-MEASUREMENTS.md) ·
[Linux instructions](LINUX.md)
