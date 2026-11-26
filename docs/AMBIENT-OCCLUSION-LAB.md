# Original S7 SSAO strength candidate

The retained diagnostic profile A (Config.version1) changes PS81.x to `min(1,1.25*s)` on the
original SSAO pair only. It preserves PS81.yzw bits and restores/readbacks the
complete original float4 after each draw. No shaders, textures, kernel radius,
sample count, blur pass, game memory, engine state or save data are changed.

The source-proven output is `1+s*(A-1)`. Unoccluded `A=1` stays1. Six retained
paired t30 samples measured strengths around0.7816 and0.9170; the candidate
maps these to about0.9770 and1. Source/compiled bindings and capture fingerprints
are in `work/ssao-probe-20261125/s7-summary.private.json`. Generic Vision
deferred SSAO is a different implementation and is not this profile.

Eligibility requires exact VS452/PS5104 original hashes, a half-source-size
A8R8G8B8 target with full viewport, full-source-size single-level R32F depth
texture at sampler0, and no blending/alpha/depth/stencil/sRGB writes. The
meaningful strength is finite0..1; zero/unit strengths are explicit no-ops.
Constant scaling uses integer round-to-nearest-even binary32 arithmetic so
caller rounding/FTZ/DAZ or getter-side FP changes cannot alter the profile.

The module uses the proven ShadowFilterEffects transaction shape by exact type
alias, with an independent state-owned durable backup and positive-only32-entry
identity cache. The host must enforce mutually exclusive AO/shadow trials and
use its existing fourth draw scope. No fifth draw scope is introduced. The
host normalizes module-frame counters with its explicit frame origin. Missing
or unsafe restoration blocks observer/fallback, Release, Reset and unload
until an owner-thread verified recovery succeeds.

Config remains12 bytes: version1 selects A, version2 explicitly selects B.
Stats is internally version2/320 bytes and contains the selected profile, completed
frame, balanced applications/restorations, failure/recovery counters and raw
original/applied constant bits. FP state and LastError are preserved. There is
no asset-path transport and no additional GPU pass or texture fetch. CPU cost
is bounded identity/context/depth-descriptor queries plus constant set/restore
and verification on the matched SSAO draw.

CPU verification passes156284 pure-policy and1445 fake-COM
checks, plus strict production compilation. These cover integer rounding,
meaningless NaN/signed-zero component preservation, failed/throwing setters,
original failed draws, durable recovery after caller lifetime, nested rejection
recovery, same-frame counters, zero/profile-ceiling no-op honesty, getter-produced refs,
negative-cache starvation and full-positive-cache bypass.

The expected scene is the ordinary castle camera with wall/ground joints,
tree bases and rock/earth contacts, plus a flat open-field negative control.
Use closely spaced original/on/original captures and a slow pan to inspect
halos, noise and thin foliage. Depth-only AO cannot add geometry absent from
the original depth buffer. CPU success is not GPU or visible-quality proof.

Runtime source was promoted after the prior live checkpoint closed. The
candidate remains diagnostic-only and visually unqualified. GPU/game operation
belongs to the root agent's reserved test slot. Run the native CPU build script
with a fresh output directory under work/; it creates no GPU device or game.

## Explicit profile B

Actual ao43 read-only samples recorded original PS81.x crossing1:
0.998512983,0.999923944,1.00118852 and1.00244355. Profile A correctly refused
the values above its admitted range. The active trial had zero applications;
capture3 completed after disable and is not an ON image or quality comparison.
The owned game closed through regular Alt+F4 with exit0, healthy memory and
restored originals/options/profiles. This is not an original-menu-exit proof.

Profile B is a new fixed Config12/version2 choice: finite original strength
0..1.25 maps to `min(1.25,1.25*s)`. It never lowers an admitted original value.
It does not relax or repin profile A. Merely allowing original values above1
while retaining clamp1 would weaken original AO and is deliberately rejected.
Zero and the selected ceiling are explicit no-ops; unsupported values keep
the original. Profile3, arbitrary parameters and profile changes during an
outstanding backup are refused. All exact program/depth/target/state and
rollback guards remain unchanged; no shader asset or GPU pass is introduced.

Original S7 already extrapolates its scalar AO blend for observed strengths
above1; the required UNORM target clamps stored output. B's1.25 is an authored
candidate ceiling, not a claim about the engine's maximum. Contact halos and
over-darkening still require actual normal-view and motion review.

Internal Stats preserves all old field offsets and reserves the old tail
padding; profile_version is appended at offset312, total320 bytes/version2.
Old internal size/version is rejected before copy. Shadow Stats stays312.
The exact Shadow transaction alias and mutually exclusive fourth-scope host
contract remain unchanged. Default selection stays A; B requires the explicit
host capability and profile choice. Neither profile is visually qualified.

## Actual profile B assessment

AO44 established a fresh ON-frame26068 application, successful original draw
and verified restoration, with original scalar1.03607154→1.25 and unchanged
unused components. Local contact darkening was subtle; no convincing normal
view quality improvement or visual release was established. OFF strength
reports were not fresh, so equality of all AO inputs is not claimed.

The run exited with C0000005 after regular Alt+F4, not a forced stop. Memory
pressure stayed0 and original binaries, profiles and options were preserved/
restored without cleanup errors. There was no attached CDB or new manual save.
This is not a clean-exit qualification. The candidate is frozen; further
small strength variants are deferred. The local contact-shadow gallery keeps
lossless original/on/original images and the measurement limits.
