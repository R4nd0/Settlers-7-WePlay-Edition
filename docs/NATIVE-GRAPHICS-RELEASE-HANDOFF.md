# Accepted graphics selection — release handoff

The user ended graphics experimentation on 2026-11-26 and explicitly approved
**Klareres Ufer** and **Blatt B**. They requested that RC4.00 build the release
including the already accepted supersampling. This supersedes earlier water-B
and multi-effect candidate choices. Do not add further visual variants.

The release builder's fixed input is
`tools/installer/native_graphics_release_preset.py`:

| Feature | Requested default |
| --- | --- |
| Supersampling | Enabled; 200% per axis at 1920×1080 output means 3840×2160 internal |
| Water | Palette index1, `river-palette-clear-bank-v3`, Klareres Ufer |
| Vegetation | Palette index1, `leaf-palette-B-v6`, Blatt B |
| Terrain modification | Disabled; original terrain |
| Shadow-filter experiment | Disabled; original shadows |
| AO-strength experiment | Disabled; original AO |

Both program hashes and byte sizes are pinned in that module. The native
working selection is water enabled/index1, material flags4/Leaf index1, shadow
disabled and ambient disabled. Discard the debug UI from the release defaults;
keep understandable independent off switches for the accepted effects.

Scale is subject to the existing render-size limits: do not apply200% blindly
to output sizes above1920×1080 or bypass the3840×2160 maximum. Preserve correct
HUD/mouse mapping and supported resolution/fallback behavior.

## Implementation and evidence

Implementation milestones are e222d9a and95d1605 on main; d6443f2 records the
guarded-stop outcome. The chosen recipient generators, immutable hashes,
native material/water bindings, and existing supersampling are implemented.
Combined source47/effects-v14 supports initialize3136/version3/scope1,
material24/version5 and water3096/version2. These combined interfaces are an
explicit **diagnostic** scope, not automatic production admission.

The combined GPU pixel/rollback/cache/Reset test passed. Real Ubisoft History
play proved simultaneous independent application and automatic selection
persistence. The user then narrowed the accepted choice to the two effects
above. See `docs/receipts/native-graphics-user-accepted-20261126.json` and the
earlier combined smoke receipt.

The final user-evaluation session exited normally with code0, no forced stop,
healthy memory monitoring and restored shared profiles, private options and
game binaries. Its report and accepted selection are retained locally; the
public acceptance receipt contains no private paths or saved-game content.

## Release builder responsibilities

`native_graphics_runtime.py` still has an empty `QUALIFIED_BUILDS` table and
the current production `prepare_launch` selects the earlier waterB and legacy
material staging. Therefore the release owner must finish production binding
to the approved programs and package them through the existing capability,
edition, renderer and immutable-program checks. Do not silently ship the old
waterB/legacy vegetation while describing the newly accepted pair.

Optical approval is complete. Gold/Steam combined-runtime and final-package
qualification is not established by that approval. Preserve all existing
safety gates; run the applicable release checks rather than inventing a
qualification entry. The previous world-exit crash investigation and
maximized-window fallback remain technical limits to assess. A maximized
client whose physical dimensions differ from the configured output can bypass
filtered presentation and hide the debug panel; restoring the configured
window size was the workaround used during evaluation.

The user assigned release building/publication to the RC4.00 chat. Further
shader experimentation in this graphics chat is finished.
