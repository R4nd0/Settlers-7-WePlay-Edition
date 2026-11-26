# Fixed leaf-lighting palette

Config version 4 is an explicit lab option. One game process loads three
recipient-generated Leaf pixel programs and selects them between completed
frames. Original Leaf vertex programs, wind, silhouettes, texture alpha,
ambient lighting and the separate specular path remain unchanged.

The three fixed diffuse curves are A: saturate(0.8·dot + 0.2), B:
saturate(⅔·dot + ⅓), and C: saturate(⁴⁄₇·dot + ³⁄₇). B retains the earlier
wrapped-diffuse program. The comparison scene is the castle's foreground tree
bank at normal zoom. Expected differences are progressively brighter leaf
sides, with possible loss of directional contrast at the strongest setting.
Actual visual acceptance remains pending.

The 4120-byte layout is independent of versions 1–3: size, version, flags,
render width, render height and selection occupy six DWORDs; three fixed
program paths follow at byte offsets 24, 1048 and 2072, with the report path
at 3096. Flags and selection are the only fields allowed to change within a
pinned configuration. Only isolated Leaf intent is accepted.

All three vendor aliases and all three programs are generated and checked
before writing any cache artifact. Programs are 1372 bytes and stored under
separate immutable recipe directories. Unknown programs, reordered paths,
mixed material intent and writes to leased programs are refused. No original
or derived game bytecode is distributed in this repository.

The first eligible Leaf draw creates all three GPU programs. A selection
change performs no file loading or program creation. OFF retains them until
the required device Release/Reset or shutdown boundary. A capture readback can
itself cause such a boundary; that resource epoch is distinct from warm reuse
within an epoch. Original shader references survive substitutions and failed
restoration until the caller completes a verified rollback.

Internal statistics report the actual loaded SHA256 for each program, selected
index, per-program creations, applications, restorations and successful or
failed original draws. This includes authored GPU fixtures, whose hashes must
never be presented as production program hashes.

CPU verification: 112 palette transaction checks, plus the retained 1492 V2
and 107 terrain-macro checks. Python checks cover fixed curve transformation,
all-source preflight, exact wire shape, capabilities and immutable cache reuse.
Real-game comparison uses OFF/A/OFF/B/OFF/C/OFF in one process, with positive
same-frame selected-program applications and matching control captures.
CPU and authored GPU success do not establish a visible improvement.
