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
  `FILE` header and HLSL text markers. The bounded scan found no raw SM2/3 token
  envelopes in those selected resources. This does not decode their container.
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
path. Container serialization, the actually selected vendor variant, compiled
programs and live texture/constant inputs still require runtime observation.
No material binding or shader replacement is claimed by this milestone.

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

Actual game experiments remain subject to the Crossplay chat's reservation of
native adapters and test copies. This work starts no games and changes no pins.

## Verification

Run `rtk proxy python -B -m unittest tools.material_shaders.test_inventory -v` from the
repository root (17 checks at this milestone).
The synthetic checks cover misleading comment tokens, truncation, unknown
opcodes, token-scan bounds, traversal names, fingerprint/CRC failures, bounded
decompression, absence of unselected payload/full-file reads in metadata-only
mode, explicit scoped payload identity, source/include/reference hints,
private-only reports and exclusive output creation. Passing them does not
qualify S7 shader replacement.

The token format follows Microsoft's [Shader Code Format](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/shader-code-format),
[Instruction Token](https://learn.microsoft.com/en-us/windows-hardware/drivers/display/instruction-token)
and [D3D9 opcode definitions](https://learn.microsoft.com/en-us/windows-hardware/drivers/ddi/d3d9types/ne-d3d9types-_d3dshader_instruction_opcode_type).
