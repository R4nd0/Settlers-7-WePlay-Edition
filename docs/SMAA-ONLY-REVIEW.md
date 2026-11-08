# SMAA-only graphics review

Development, 2026-11-08. This is a source candidate and a local graphics
investigation, not a new published release or a claim that shadow and
vegetation defects have been fixed. Published RC3.22 assets remain unchanged.

The user withdrew acceptance of the cinematic ReShade appearance and reported:

- Mittsommer shadows sometimes flicker and appear too hard at distance.
- Tree, grass and grain-field billboards appear blurred or overfiltered.
- Retain SMAA while removing the other ReShade effects for the first comparison.

## Implemented profile

Select `enhanced_aa=true` and `midsummer_tone=false`, `contact_shadows=false`,
`maximum_fx=false`. The existing independent renderer, display, model-detail,
shadow-resolution and shadow-range choices keep their meanings.

The only active technique and sorting entry is `SMAA@SMAA.fx`. The shader
directory contains only `SMAA.fx`, `SMAA.fxh`, `ReShade.fxh`, and `ReShadeUI.fxh`;
the only lookup textures are `AreaTex.png` and `SearchTex.png`. The pinned
standard ReShade 6.8 runtime remains the host. The profile uses color edges,
threshold 0.075, search steps 64/20, corner rounding 25, no depth predication,
and requests no depth copying. No authored Mittsommer pass, GI, motion
estimation, contact darkening, grading, bloom, sharpening, haze, refraction or
sun-clock add-on is loaded. Native game lighting and effects are separate.

SMAA also works independently with the older opt-in combinations. The old
simple edge filter is disabled rather than stacked with SMAA. Versioned profile
fingerprints protect existing profiles from being overwritten. Runtime/module,
lookup texture, configuration and generated-preset checks remain enforced,
including the normal-exit preset recovery introduced in RC3.22. Both launcher
languages and the legacy graphics dialog explicitly name SMAA.

## Verification and observations

Source tests: 14 Mittsommer/profile tests passed (two optional Lua tests skipped),
80 graphics integration tests passed, and 34 installer tests passed. These are
synthetic tests, including isolated SMAA assets/technique selection, all 16
effect combinations, absent clock source, preset normalization/relaunch and
modified lookup rejection. They do not establish visual quality.

Private work and native observations are recorded under
`work/smaa-review-20261108`. Game checks use the owned separate test installation;
original installation, executable and native adapter remain unchanged. A
diagnostic process substitutes only the reviewed source ReShade helper while
retaining the existing installed-package, content, executable and native guards.
Saved launcher choices were restored after both diagnostic runs. The launcher
build and bilingual UI/private-pipe self-test also passed. Initial sandbox-only
attempts failed on the local service socket/UI pipe; the unrestricted, authorized
local checks passed. No game or owned test service remains running.

## Actual game review

Two unchanged production-executable runs loaded the existing Tanholm graphics
QA save on the RX 9070 XT, 2560x1440 borderless, with the saved native high
quality/detail/range/Ultra choices. Both exited normally with code 0; profile
immutability and installed-package checks passed. The standard runtime compiled
only `SMAA.fx`, without shader errors. Its generated preset remained canonical
after both exits; these runs did not reproduce preset drift.

The first run retained global AF16 and mip bias -1.0. It inspected castle/forest
views and grass/grain fields, including an in-game change from Mittsommer to
stock static weather. Hard shadow boundaries and soft distant vegetation were
still visible without the other ReShade effects. The second run retained SMAA
and native model/shadow choices but removed only the global AF and mip overrides.
Actual DXVK logs confirm the first run's overrides and their absence in the
second, with Strict float emulation and the 60 Hz cap retained. Vegetation still
looked soft and hard shadow boundaries remained visible. This did not establish
a clear cure from disabling those overrides.

These are qualitative observations from the same save and repeated camera
zoom/map locations, not frame-locked pixel comparisons. Sun/weather phase and
the running simulation were not locked. Intermittent shadow flicker was not
reliably reproduced or excluded by these point-in-time observations. No temporal
stability, GPU benchmark, multiplayer, native Linux or other-GPU result is claimed.

The initial in-game depth-of-field checkbox displayed enabled despite the launcher's
saved `DepthOfField=0`. It could be unchecked/applied, but displayed enabled again
when reopening the menu. The shared options file read 0 after applying. This was
at that stage an unresolved settings/UI discrepancy, not proof of the actual GPU effect's
state or proof that it causes the vegetation blur. Native AA, actual depth-of-
field state, alpha-card mip/coverage and native shadow filtering remain follow-up
candidates. The subsequent [controlled investigation](SHADOW-DOF-REVIEW.md)
traced and corrected the camera's overwrite of the pending DOF choice, and
removed the global negative mip override from the source candidate. The
original qualitative observations above do not qualify those later changes.

Longer model detail initially also forced a global negative mip bias; disabling
that entire option would confound model geometry with texture filtering. The
later source change separates the model option from that sampler override.

## Local preview and profile incident

A PC-bound preview is available locally under `outputs/S7-SMAA-Preview`, started
with `Start-SMAA.cmd`. It snapshots the reviewed helper, SMAA sources/lookups and
licenses, validates those files and the installed package, runs the ordinary
guarded backend, and restores the previous launcher selection after exit. The
normal installed RC3.22 launcher is not updated by this preview. Profile-only
verification and save/option backup verification passed. This is not a public,
signed update or a portable installation package.

The first diagnostic did not back up the shared Documents save profile before
launch. Loading the QA save caused the game to replace the shared campaign
autosave. The pre-run autosave has not been recovered; available work-directory
copies are older and were not substituted. Manual saves were not deliberately
written, and their existing old timestamps remained. A backup was made after
the first run; all protected save/map bytes remained unchanged in the second
run. Do not describe this as full restoration of the pre-test user profile.

The preview now backs up existing game saves and `Options.ini` under the test
App's `State/SMAA-Preview-SaveBackup` before each launch, checks their hashes, and leaves normal
user gameplay saves in place. The project instructions now require resolving
and backing up actual shared profile paths before diagnostic launches. A separate
game executable alone does not isolate Documents or prevent autosave changes.
