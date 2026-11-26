# Combined Leaf and Terrain material V5

V5 adds one compact layout without changing versions 1–4 or any optical
program. Six DWORDs encode size 24, version 5, flags 0/2/4/6, render width,
render height and Leaf variant 0/1/2. There are no shader paths or free shader
coefficients in this payload. The admitted native report and an explicit
private-case/recipient layout derive seven fixed recipe/hash filenames and
the sibling material-effects report.

Slots are Leaf A, Bump PS, Parallax PS, Bump Macro VS, Parallax Macro VS,
Leaf B and Leaf C. All seven CPU buffers and read leases are admitted even
when both groups start OFF. The original three Leaf programs and four Macro
programs remain immutable. All three vendor aliases are generated for all
seven programs—21 outputs—before the stager writes any cache artifact.

The first eligible Leaf draw creates its three PS objects; the first eligible
Terrain draw prepares its four VS/PS objects before mutating either stage.
Thereafter an atomic safe-boundary configuration changes only the two family
bits and Leaf selection. It does not discard the other family's programs.
The material cache is bounded at seven objects; Water's separate palette is
additional. Required device Release/Reset/shutdown cleanup remains intact.

Original pair identity, full render context and existing measured Macro
constant/texture guards still apply. Each draw retains original shader
references, restores shaders and constants and exposes an unsafe restoration
failure for fail-closed handling/retry. Terrain still requires unit tuning
factors; it has one Macro choice, not independent Detail/Relief variants.
Internal Stats version 3 preserves prior field offsets and adds seven actual
program hashes/creation counters; Leaf's three per-variant arrays remain.

Verification before real-game evaluation includes 132 authored combined COM
checks, retained V2/Macro/Leaf regressions, actual recipient production hash
admission for all seven programs and all twelve flags/index tuples, plus
all-source preflight tests. The synthetic combined GPU fixture exercises
rapid Leaf/Terrain choices in one process, unchanged Water selection, actual
authored program pixels, warm cache retention, original restoration and Reset.
Writing/compiling that fixture alone does not claim GPU execution or visual
acceptance.

The user-selected combined preset is intended to become the release default
after visual selection and technical qualification. Until those checks are
complete, its model/receipt is an experimental selection, not a qualified
release setting. Existing Water B acceptance does not automatically approve
the other groups or their combination.
