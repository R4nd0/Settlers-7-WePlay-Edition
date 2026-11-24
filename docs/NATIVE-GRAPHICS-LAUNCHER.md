# Native graphics launcher integration

The launcher has separate, disabled-by-default settings for internal render
scale, river water, terrain, vegetation and stable shadows. The current source
does not publish a qualified native payload: the admission table in
`native_graphics_runtime.py` remains empty until actual game and package checks
pass. UI fixtures supply explicit synthetic capabilities and do not grant them
to installations.

Scale applies to each dimension: 1.5x costs 2.25x pixels; 2x costs 4x pixels.
The panel shows the exact internal dimensions and requires an explicit output
display/mode/resolution. It rejects fractional dimensions and sizes beyond
3840 x 2160. The backend independently checks these conditions, each original
edition's allowed features and scales, the exact DLL/manifest hashes and the
pinned DXVK renderer. A saved checkbox or a compiled-feature bit cannot grant
qualification. Unsupported choices fail explicitly.

The native path excludes the existing ReShade effect options. Material recipes
require High shader quality, with High water quality for rivers. Original
resources are read from the verified recipient edition's own archive; derived
programs are generated only under its LocalData. The package carries authored
parsers/recipes and the native DLL, never original or generated game shaders.
Ocean water is not covered by the current river option.

`platform_standalone.launch` composes native graphics with its existing owned
pre-entry callback. The fixed x86 worker uses the original executable's kernel
imports, exact entry/identity checks, a DLL read lease, bounded separate RW data
and RX code allocations, and a fixed export allowlist. It does not offer a
general PID/export interface. An initialization refusal follows the existing
pre-entry failure path; it never resumes a partially admitted startup.

Optional package building uses `--native-graphics <qualified-distribution>`.
The builder and installer require the original-edition qualification matrix,
verified payload and public recipe module whitelist. Gold and History receive
the same trusted launcher modules in their fresh portable copies. Existing
settings transactions preserve the full native selection for save/discard and
graphics recovery. Disabled legacy settings do not inspect or load the payload.

Development validation includes the bilingual GUI self-test, exact request and
dirty-state round trips, per-scale availability, renderer/effect conflicts,
backend sizing/quality checks, callback composition and payload drift rejection.
Actual production-loader lifecycle, all-edition installed-package tests, 4K
memory/performance and visible material/shadow comparisons remain separate
requirements. See [qualification](GRAPHICS-QUALIFICATION.md) and the
[native integration contract](NATIVE-GRAPHICS-INTEGRATION.md).
