# RC3.28: History-led Gold graphics and options

Gold uses the common launcher's Graphics page and the same validated preference
format, quality presets, display selection, optional 16x filtering, detail and
shadow switches, and authored ReShade presets as Ubisoft and Steam History.
The original game APIs, Gold-specific native guards and per-copy platform
isolation remain authoritative. Original installations are not edited.

Gold retains the pinned x86 DXVK 3.1.1 renderer and strict float emulation needed
by water. Its absolute configuration path survives working-directory changes
and moves. GPU/filter choices now reach the owned child, rather than being
ignored. Inherited graphics overrides cannot replace these choices. Gold does
not offer the unqualified native fallback; History retains its existing renderer
selection. Borderless placement verifies the actual owned executable for all
three prepared editions.

Clearing the filtering override uses DXVK's original `-1` default rather than
leaving Gold's older forced 16x setting active. This value and the display
enumeration default are verified against the pinned
[DXVK 3.1.1 D3D9 option parser](https://github.com/doitsujin/dxvk/blob/v3.1.1/src/d3d9/d3d9_options.cpp).

Gold's ReShade profiles have their own folder and retain separate clock/layout
boundaries. Tone, SMAA and contact shading share History's authored settings.
The actual integrated Gold Maximum trial reproduced displaced duplicate UI
contours in the black profile/menu/load screens, despite the prior separate
pre-UI candidate. Maximum is therefore removed from Gold's normal interface
and rejected by the backend, including at Full HD. The experimental runtime
is not bundled. History's existing Maximum remains available. No global Vulkan
layer, registry setting or Steam/Ubisoft connection is installed by graphics.

The normal Gold interface includes History's Mittsommer weather choice and
camera depth-of-field preference handling. Its environment is generated from
the recipient's original Gold resources; existing shared resource payloads are
retained. Gold's camera turn-counter correction remains. The global chat sound
checkbox and the in-game main chat, contacts and inbox buttons are removed.
Gold's unadmitted multiplayer
entry is removed from the normal single-player menu. Castle Forge appears only
when its required menu background map is enabled. Local profile, campaign,
skirmish, saves, audio, controls and working graphical options remain available.

Repair retains original Interface/Shr archives and generates the new profile
from those verified originals. Existing pre-RC3.28 copies can supply their still
original Shr archive; later copies require the retained original backup. Newer
or unknown bytes are never adopted as originals. Common settings retain the
existing recoverable transaction, update transfer and private profile handling.
The independent experimental crossplay menu builder remains byte-identical.

The [acceptance receipt](receipts/rc328-graphics-parity-20261114.json) records
two actual retained-world Gold loads, working water/HUD/options, persisted
Mittsommer, final menu removal, read-only native detail/shadow values, regular
closure and restoration. It separates the rejected Maximum trial from the
accepted configuration. The bilingual launcher has 37 passing GUI checks;
all 284 Gold Lua scripts compile, and the embedded interpreter generates the
same pinned archives. In particular, static compilation or a visible
menu does not establish performance, multiplayer compatibility or support for
an untested rendering configuration.
