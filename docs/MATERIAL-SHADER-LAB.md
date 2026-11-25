# Original material shader lab

This is an offline investigation tool, not an enabled graphics option. It does
not replace shaders, load a DLL, start the game, change a game archive or alter
release qualification. Supersampling remains the first graphics milestone.

## Evidence and first target

S7's current renderer is D3D9 translated through the pinned DXVK runtime. The
existing [Gold graphics investigation](GOLD-GRAPHICS-PORT.md) observed shader
fingerprints at scene/UI boundaries. Those fingerprints do not identify an
individual water, foliage or building material, and cannot safely select a
replacement target. Existing [native shadow work](SHADOW-DOF-REVIEW.md) changes guarded
parameters at an established shader call, not a general shader replacement API.

Water is the proposed first material target because the existing
[DXVK quality investigation](DXVK-QUALITY.md) documents a real water rendering
dependency on strict D3D9 floating-point emulation. Better water would be a
visible candidate after supersampling. A read-only survey of the owned graphics
archive on 2026-11-24 found actual resources rather than assuming that generic
Vision SDK samples match S7:

- Its checked directory contains 181 `.ShaderLib` and 50 `.inc` resources among
  10,870 entries. Vendor/quality variants include water, terrain, SpeedTree and
  shadows.
- Explicit selected water, SpeedTree and shadow libraries have a printable
  `FILE` header and HLSL text markers. The initial generic scan found no raw
  SM2/3 token envelopes because compiled programs are serialized as hexadecimal
  text. The later dedicated decoding milestone below resolves that format.
- The selected ATI/NV high-quality water libraries both reference `Water.inc`.
  That include references existing Defines, Fog, Globals and Light includes in
  the same shader resource directory. Its source text contains sampler-register
  hints 0 through 9 and float-constant register hints. These are static text
  observations and may span mutually exclusive preprocessor branches.
- The smaller shared archive contains checked terrain material XML; inspection
  found path references but established no runtime shader binding.

All original code stayed in memory and in the existing private game archive;
no original shader source or binary was exported or committed. Private reports
are under `work/material-shaders-20261124/`. The large graphics archive used an
explicit directory SHA256 pin plus selected resource CRC/size and payload SHA256;
its complete archive fingerprint was not qualified. The initial full-file hash
experiment was interrupted without changing the input after excessive IO.

The concrete next target is the original high-quality water-library/include
path. The actually selected vendor variant, compiled program binding and live
texture/constant inputs still require runtime observation. No active material
binding or enabled shader replacement is claimed by this milestone.

## Compiled water decoder and authored binding marker

`tools/material_shaders/shaderlib.py` now decodes the actual text format observed
in S7's selected resources. Its supported header is `FILEVERSION = 2.3.0.0;`
followed by `PLATFORM = DX9;`. A delimited SOURCECODE field contains include
references. Named SHADER records carry render-state and constant metadata plus
`vs_bytecode[vs_3_0]` / `ps_bytecode[ps_3_0]` fields. Those hexadecimal bodies
decode to a four-byte little-endian byte length followed by the compiled D3D9
program. The length prefix is not part of the program given to D3D9.

Actual read-only verification decoded each of the ATI, NV and ATIx high-quality
water libraries into 11 named records and 21 compiled SM3 programs (10 vertex,
11 pixel). Every serialized length, stage, model and full token envelope
matched. The selected shadow library likewise decoded into five records and
eight programs. This establishes that serialization shape for the checked
resources, not complete support for every Vision library or shader semantics.
The private `water-compiled-manifest.private.json` contains the per-program
SHA256 identities needed by a future D3D9 shader-creation/binding observer.

The parser masks the source fence, comments and quoted strings when identifying
records. Unknown versions/profiles, duplicate records/stages, malformed hex,
wrong lengths and trailing/incomplete programs are refused. Its metadata API
exports program hashes, stage/model and sizes, never decoded original programs.

`marker_pixel_shader()` is an authored 44-byte SM3 pixel program that outputs
opaque magenta without textures or game constants. It is a diagnostic binding
marker, not the proposed water-quality effect. Windows' system D3D compiler
accepted its disassembly with HRESULT zero and recognized SM3 and the color
output. This compiler check does not establish GPU rendering or game binding.

`replace_with_marker()` constructs an in-memory candidate for one named pixel
program, requiring both the full original ShaderLib resource SHA256 and the
exact original compiled-program SHA256. It updates the encoded program length,
preserves every byte outside that one hexadecimal span, reparses the candidate,
and verifies that all unrelated programs and records survived unchanged.
The actual OceanWater record round-tripped this way in all three checked vendor
libraries. Original source, constant metadata, render state, include references
and other programs were retained. No game archive or resource file was written.

The function implements no file export, archive patch, deployment or runtime
activation. Its candidate report states `diagnostic_only: true`,
`runtime_binding_observed: false` and `deployment_permitted: false`. Before an
actual marker run, an original game draw must be observed using the matching
compiled-program identity and supported original resource/edition. Subsequent
visual improvements can then target that verified material path while retaining
its required bindings and a checked original fallback.

## Read-only inventory

`tools/material_shaders/inventory.py` accepts one explicitly selected production
BBA v6 archive or one extracted resource. Payload-capable inspection requires
either the exact full input SHA256 or an explicitly selected directory SHA256
pin. The latter supports only read-only metadata exploration and is labeled
`full_input_fingerprint_verified: false`; it cannot qualify a deployment input.
The separate `--directory-only` mode requires no full-file hash and cannot read
payloads. An archive scan reads its checked directory and inventories shader/material name
candidates and extension counts. It reads no resource payload unless an exact
`--resource` name is supplied. It uses the existing MIT S7Packer-derived BBA
reader through a read-only memory map, limits directory and selected payload
sizes, bounds gzip expansion, verifies selected payload CRC/size and checks that
the mapped input still has the selected full-file or directory identity after
inspection. Each inspected payload receives its own SHA256.

Names and extensions are discovery hints, not confirmed formats. For selected
resources the tool searches for bounded D3D9 Shader Model 2/3 token envelopes:
version, instruction/comment lengths and an END token. It emits stage/model,
offset, size and SHA256. Additional metadata comprises a printable header FourCC,
counts of HLSL markers, include-path hints and numeric register-index hints,
plus path-like references from explicitly selected UTF8 XML. It never exports
bytecode, shader comments, disassembly, constant names or source declarations.
It does not semantically
validate programs, recover texture/constant bindings or identify a material.
Model 1 and unfamiliar containers remain unsupported. Malformed candidates and
bounded-scan failures must not be treated as proof that a resource has no shader.

Reports are local diagnostics: the output must resolve under repository `work/`
and must be a new file. Existing reports cannot be overwritten. Input paths are
not copied into reports. Keep reports and all original resource data out of Git.

```powershell
rtk proxy python -B tools/material_shaders/inventory.py --archive work/<owned-copy>/Base/BBA/gfx.bba --expect-sha256 <64-lowercase-hex> --output work/material-shaders-20261124/inventory.private.json
rtk proxy python -B tools/material_shaders/inventory.py --archive work/<owned-copy>/Base/BBA/gfx.bba --expect-sha256 <64-lowercase-hex> --resource <exact-name-from-private-inventory> --output work/material-shaders-20261124/selected.private.json
rtk proxy python -B tools/material_shaders/inventory.py --archive work/<owned-copy>/Base/BBA/gfx.bba --directory-only --output work/material-shaders-20261124/directory.private.json
rtk proxy python -B tools/material_shaders/inventory.py --archive work/<owned-copy>/Base/BBA/gfx.bba --expect-directory-sha256 <directory-sha256-from-checked-private-report> --resource <exact-name> --output work/material-shaders-20261124/selected-scoped.private.json
```

The CLI does not compute and silently accept a supplied expected identity. Take
a full-file pin from the independently checked manifest or an explicit read-only
fingerprint. A directory pin comes from the private directory inventory and
describes only that checked metadata, not every archive payload.
An archive directory scan is not game-data indexing and should stay limited to
the explicitly chosen graphics archive. Do not traverse account caches or point
TokenSave at copied games.

## Required next evidence

Before a material experiment is enabled, identify its real source resource or
runtime D3D9 shader, bind it to an observed material/draw phase in each supported
edition, and measure its texture/sampler and constant-register inputs. Then
develop an authored replacement against that interface, with exact input hashes,
device/reset/resource lifetime handling and an unchanged-original fallback for
unsupported state. A standalone GPU fixture can test shader behavior but cannot
establish correct S7 material binding or actual visual improvement.

On 2026-11-24 the user clarified that the Crossplay chat is archived. Its older
exclusive reservation no longer blocks isolated graphics experiments under the
existing local-test authorization. The inventory tool itself starts no games
and changes no pins. Check actual process ownership and profile isolation before
any separate runtime experiment.

## Bounded original-program observer and material input capture

`native/material_shader_observer.{h,cpp}` is an optional read-only D3D9 component
for the guarded native graphics hook. It accepts a SHA256-pinned scalar catalog,
not program bytes. `tools/material_shaders/catalog.py` generates that catalog
from explicitly selected recipient-owned original resources. The checked
high-quality Water/Terrain/SpeedTree libraries yield 345 entries across three
vendor variants. Many entries alias identical compiled programs: a matched hash
does not uniquely identify a vendor or logical material. Reports retain alias
counts and original VS/PS pair identities rather than inventing uniqueness.

The integrating hook calls `ObserveDraw` after a successful original draw, inside
its internal/TLS scope. `Get*Shader`/`GetFunction` yield bounded program hashes;
cached COM references prevent address reuse from producing a stale identity.
References are released at Present, before Reset and before **every external
device Release**, including serialized same-device cleanup from another thread.
Observe/capture calls remain restricted to their render thread. The component
preserves caller LastError and the complete FPU/MXCSR state. Its scalar reports
contain no original code, pointers, texture pixels or account information.

`tools/material_shaders/semantics.py` reads bounded compiler CTAB metadata.
Actual selected ATI high-quality VS/PS programs yielded 605 binding rows across
115 programs. This catches an important stage distinction: river reflection
and fog vectors belong to the RiverColor vertex shader at c69/c68, while the
OceanColor pixel shader uses a different packed density/specular vector at c102.
Register names in an include alone cannot substitute for the compiled binding.

`tools/material_shaders/capture.py` generates a 952-byte `CaptureConfig` ABI1
and private semantic receipt. Eight exact original VS/PS hash pairs cover river,
ocean color/composite, terrain bump/parallax, leaf cards, fronds and branches.
Configurable first-frame/stride and per-target limits cap capture at 64 samples.
Only selected finite float4 components are retained. Texture capture reports
sampler type, dimensions and format; draw context reports target/viewport sizes,
format, alpha states and sRGB-write state with an explicit validity mask.

The supplied source interpretations distinguish compiler-verified register
locations from meanings established by private include inspection. Packed
components whose meaning is still unqualified are labeled accordingly. A
diffuse multiplier is explicitly not presented as a foliage backlighting knob.
All captures are read-only; native constant/shader writes remain a separate
implementation and qualification step.

Further include/interface inspection resolves the OceanColor c102 vector:
x is fog density, y is depth density, z is specular intensity and w is specular
exponent. Terrain c33.y scales centered high-frequency material detail before
combining it with the base surface; c33.z supplies displacement. These are
material-specific candidate controls. Their numerical limits still require
measured original draw values, and changes must restore the original GPU
constants after each matching draw. A layer pass can write an intermediate
material target, so its context and new-launch behavior must also be checked.

## Recipient-generated leaf backlighting recipe

Private original-source inspection identifies the leaf backlighting factor in
TEXCOORD7.y: the original vertex program computes an angular eye/sun term and
the pixel program uses that interpolant for warm leaf transmission. The other
components carry alpha, specular fade and selection information. This is a
material-specific path, not a full-screen brightness adjustment.

`foliage.py` authors a narrowly scoped vertex-program extension. It identifies
the compiler-declared TEXCOORD7 output, redirects its original calculation to a
verified unused temporary, retains the other components and increases/clamps
only the backlight component. Position, wind calculations, address calculations,
texture inputs and unrelated interpolants are preserved. New local constants
are allocated outside all explicit and complete CTAB-declared float/matrix/array
ranges. Opaque/mismatched relative-array footprints, unsupported control flow,
missing semantics and register/instruction exhaustion are refused. Original
valid HLSL array-index bounds remain the unchanged indexing contract.

The checked three vendor variants generate the same 3,872-byte VS at factor
1.25, forty bytes longer than the original. The system D3D disassembler accepts
SM3, TEXCOORD7 and the authored coefficient. Synthetic semantic checks preserve
position/alpha/specular/selection and verify saturation. These results do not
establish GPU rendering or visible improvement in S7.

`recipes.py` ships authored transform code and compatibility hashes only.
`generate_leaf()` requires the recipient's exact original resource and compiled
LeafCard VS/PS pair. `write_local_cache()` creates content-addressed outputs
under that recipient's `LocalData/MaterialShaders`; existing program bytes are
verified and never overwritten. No original or derived game program is to be
committed or bundled in a public package. Generation returns `active: false`.
The native effect must classify the original pair before a temporary shader
bind, restore the original shader after the draw and record original/applied
observations separately. Actual binding, output quality and state restoration
must pass before an ordinary setting or release can enable this recipe.

The separate `bytecode.py` RGB extension is a guarded tuning/diagnostic primitive
only. It preserves alpha, other MRT outputs and original inputs, but does not
establish completed water, terrain or vegetation improvements by itself.

## First actual bindings and candidate draw-local effects

The parent's owned Ubisoft History run supplied an actual read-only snapshot
with 9,345,217 successful original draws, 2,467,912 known VS/PS pairs and no
query/capture failures. Twenty-four targeted samples covered RiverColor,
terrain bump/parallax, leaf cards, fronds and branches on a 1920x1080 target and
viewport. OceanColor/OceanWater were not captured and remain outside this
candidate's active scope. Vendor aliases still prevent unique vendor attribution.

These are four early-world frames, not a long-term numerical qualification.
River c68/c69 each contain four depth-class coefficients selected by a clamped
0..3 vertex depth index. Their vectors were identical across the four samples;
component min/max describes differences between classes, not a temporal change.
The include's older fog-depth label is consumed by the river pixel program as
fog density, including depth-dependent refraction tint. Later steady and
transition samples are still required before final policy bounds are qualified.

`terrain.py` now provides a material-specific normal-detail recipe for the
original compiled bump/parallax programs. It strengthens the CTAB-proven normal
map at sampler5, preserving the other texture channels. Because the original
program reconstructs Z with an unguarded square root, the authored extension
radially limits decoded XY to 0.99 before re-encoding packed AG channels. The
epsilon-protected reciprocal square root also handles flat normals. Independent
per-channel gain would be unsafe here and is not used. The two recipient-generated
PS programs add nine ALU operations per normal fetch; system disassembly and
synthetic steep/flat-normal checks pass, but actual GPU/visual checks remain.

The new `native/material_shader_effects.{h,cpp}` component is separate from the
currently loaded observer build. It classifies exact original VS/PS pairs before
any temporary shader bind and checks the original target/viewport/alpha/sRGB
context. Candidate river tuning adjusts proven depth-class density/reflection
components independently; terrain contrast/relief changes preserve other fields.
Generated terrain PS and leaf VS are used only for their matching original pairs.
Every draw transaction restores and bitwise reads back original constants and
shader objects, including failed original draws. Reports separate original and
applied values; this is not a full-screen pass or an engine/save/RNG write.

The integrating host must hold its serial device lock and internal/TLS scope,
validate private paths/ancestors and call cleanup before each external Release.
It must inspect `unsafe_state` immediately after Begin/End, retry restoration
inside the same callback and stop the diagnostic renderer if restoration cannot
be proven. It must not describe that state as an original fallback or feed it to
the original-program observer. The component preserves caller LastError and FP
state, has bounded frame references and disables further mutations after a fault.
The x86 CPU fixture covers partial setter failures, exception unwinding, failed
restoration reporting/retry and original-state/reference recovery. This remains
a candidate until the parent's actual GPU, image and pacing tests pass.

The generated-program cache now survives ordinary EndFrame boundaries, bounded
to one leaf VS and two terrain PS objects. Original classification references
still end each frame. Reset before every external device Release, actual Reset,
disable, reconfiguration and Shutdown releases both caches. Reports distinguish
current/peak retained programs from the frame's original `cached_refs`. The CPU
fixture proves eight warm frames perform no additional shader creation, followed
by complete Reset cleanup and lazy recreation. This addresses the actual game's
observed three program creations per active frame; it does not establish a
measured frame-time improvement or resolve the separate game-exit fault.

An actual 4K comparison has matching original River camera/light uniforms for
off/on/off frames. Water changes also occur between the two controls, so their
pixel differences are animation noise as well as any material effect. Distant
ground and foreground leaf regions showed no clear effect. A closer pair had
different eye/VP values and is rejected as an A/B pair. Original P pause can
present a cached backbuffer without fresh world geometry; a configuration
acknowledgement alone therefore cannot prove that pixels show the new effect.

The original terrain programs fade normal detail and parallax displacement
between eye distances 2000 and 3200. Beyond 3200 the normal is flat and the
displacement contribution is zero, even when the matching program executes.
The current normal-strength recipe intentionally preserves that original fade.
Its contrast parameter affects centered detail diffuse colour; a distant mip
near the neutral value can likewise produce little change. These are source
explanations to test, not evidence that the inspected image region uses a
particular texture mip or terrain height. A useful next terrain experiment must
measure this near-detail weight and preserve the separate far-layer fade.

`terrain_distance.py` is that experiment's separate offline candidate: its
bounded default interval is 8000..12800, ending before the far layer starts
fading. It changes exactly two original local DEF components per matched VS,
preserving the Bump program's separate 15000..19200 far visibility interval.
The same source/pair hashes generate identical candidates for ATI/ATIx/NV;
system disassembly accepts both programs. No new instruction or texture read is
added. The parallax candidate also extends its original displacement weight,
so geometry coverage, aliasing, texture mips and pacing still need actual tests.
The current native configuration supports a leaf VS and terrain PS programs;
it cannot load these terrain VS candidates. A future explicit configuration
version and guarded VS/PS transaction are required before runtime use.

`leaf_lobe.py` stages a separate offline candidate. The source-proven angular
factor is a saturated view/sun dot raised to the fourth power. Replacing its
unique, exact-original self-product with a saturated MOV changes only
TEXCOORD7.y to the second-power lobe, without a colour gain, new constant,
temporary or texture read. The recipient's complete source-resource and original
VS/PS hashes are mandatory. System disassembly accepts the generated program;
authored tests verify the other outputs and zero-light response. The production
stager and native program pins do not accept this candidate, and no actual GPU
or visual qualification has been claimed. A view whose dot already clamps to
zero will remain unchanged with either exponent.

`compare.py` can compare an off/on/off sequence with `--repeat-control` and
`--repeat-control-meta`. Each image needs its original native capture receipt.
The diagnostic River PS80 pin must have positive, balanced completed-frame
application/restoration counters in that exact capture frame; a retained global
pin alone is insufficient. All four float32 components, pin epoch and commit
frame must match across the sequence. The helper also checks image hashes,
original camera/light vectors, actual material masks/revisions and fault fields.
It reports the effect difference separately from control/control residual
motion. That residual remains required because frozen shader UV scrolling does
not freeze reflected or refracted scene textures. Source checkpoint, OBSERVE
admission and DLL identity checks belong to the separate runtime qualifier;
this pixel helper does not claim to supply them. No difference metric or valid
receipt sequence automatically qualifies visual quality.

## Verification

The explicit `staging_macro.prepare` candidate preserves the version2/6180-byte
transport and historical `staging_v2.prepare` defaults. It generates new
`terrain-macro-relief-*-v3` VS recipes from the recipient's pinned originals;
it does not overwrite earlier recipe paths or receipts. The native loader
accepts a complete historical set or a complete macro set and rejects mixed
VS variants. Macro uses unit detail/relief factors; the existing bounded1.1
normal PS remains shared. No whole-image colour multiplier is added.

The macro recipe halves the original region-local detail UV frequency using
an existing local0.5 literal and one extra vertex MUL. Bump normal fade changes
to12000..18000 world units; Parallax stays8000..12800 so its expensive ray work
is not extended farther. Original position, other interpolants, far alpha
fade15000..19200, mip selection rules and texture fetch counts survive.
Larger world-space features select a correspondingly finer mip; this does not
invent texture information. The native draw guard requires the observed
region size3200², VS detail512/9/8, DXT5 diffuse/normal512² with10 levels, and
for Parallax the L8 height256² with9 levels. Meaningless components are not
interpreted. Unknown inputs fall back before shader mutation. Query refs and
both original shader stages retain the existing draw-local rollback contract.

Actual read-only Ubi observation recorded eight samples across both exact
original pairs with these inputs. Earlier t30 normal-view images established
no convincing terrain improvement; they did not capture these terrain
registers. Macro is a new unqualified candidate, not a retrospective visual
pass. Its required positive test is visible terrain depth at normal camera
on unobstructed earth/stone, an off/on/off comparison against control motion,
and a slow pan/zoom and region-boundary check for shimmer or swimming.

Run `rtk proxy python -B -m unittest tools.material_shaders.test_terrain_macro
tools.material_shaders.test_staging_macro` for authored recipe/preflight/cache
checks. `native/material_shader_effects_v2_build.ps1` also runs the macro fake
COM fixture: complete-set guards, measured-state refusals, unused components,
texture getter exceptions after owned outputs, rollback, warm cache and
toggle cleanup. CPU/disassembler acceptance does not establish driver or
actual-game visual qualification. Private reports label `terrain_recipe_variant`
separately from the unchanged transport `config_version`.

Actual t41 (Steam, native build38) applied macro recipe3 at normal camera but
did not establish a convincing overall terrain-depth improvement. A small
coarser soil pattern is visible locally. The first off/on/off series had
unequal427/42-frame gaps; temporal control differences dominated it. A second
series used7/7-frame gaps and matching captured camera/sun vectors. Open-field
RGB differences remained comparable to the off/off control. The ON module
report was stale: its first automated counter check reported false and remains
retained. Counters before/after the phase establish applications/restorations,
not per-image role counts. Both image series remain available in the local
terrain gallery. No metric establishes a visual-quality pass.

The test ended through the existing memory guardian (`0xe0537021`, pressure1)
before the initiated menu exit completed. Original binaries, shared profiles
and private options were preserved/restored with no cleanup errors; the new
private save was retained. This was no clean-menu-exit qualification. The
terrain recipe is frozen as visually unqualified; further low-gain variants
are deferred.

Run `rtk proxy python -B -m unittest tools.material_shaders.test_inventory -v` from the
repository root (17 checks at this milestone).
Run `rtk proxy python -B -m unittest tools.material_shaders.test_shaderlib -v` for
eight additional parser, exact-identity and unchanged-program checks.
Run `rtk proxy python -B -m unittest discover -s tools/material_shaders -p 'test_*.py'`
for all 76 inventory, format, CTAB, catalog/capture, output-extension,
recipient staging, comparison and candidate checks. `native/material_shader_observer_build.ps1` builds and
runs an x86 synthetic COM fixture without creating a D3D9 device or GPU work;
it covers reference cleanup, aliases, successful-draw-only observations,
capture bounds, nonfinite values, sampler/context descriptors, foreign-thread
same-device cleanup, caller LastError and MXCSR preservation.
The three additional `test_terrain` checks exercise packed-normal channel
preservation and the original square-root domain. `material_shader_effects_build.ps1`
compiles both the guarded production object and a fixture-only authored-identity
CPU executable; no GPU/game is started by that script.
The synthetic checks cover misleading comment tokens, truncation, unknown
opcodes, token-scan bounds, traversal names, fingerprint/CRC failures, bounded
decompression, absence of unselected payload/full-file reads in metadata-only
mode, explicit scoped payload identity, source/include/reference hints,
private-only reports and exclusive output creation. Passing them does not
qualify S7 shader replacement.

The token format follows Microsoft's [Shader Code Format](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/shader-code-format),
[Instruction Token](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/instruction-token)
and [D3D9 opcode definitions](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d9types/ne-d3d9types-_d3dshader_instruction_opcode_type).
CTAB reflection follows [D3DXSHADER_CONSTANTTABLE](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxshader-constanttable)
and [D3DXSHADER_CONSTANTINFO](https://learn.microsoft.com/en-us/windows/win32/direct3d9/d3dxshader-constantinfo).
