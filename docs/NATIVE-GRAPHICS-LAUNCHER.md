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

The existing river-water option now prepares the accepted balanced B recipe
explicitly. There is no second A/B user setting. All three pinned High vendor
resources must generate the same 3840-byte B program in recipient LocalData;
the fixed production loader accepts only B's recipe/hash path and SHA256. It
queues `WePlayNativeGraphicsConfigureWater` with the separate 1076-byte transport
after native initialization and the material configuration. When only water is
selected, a 4132-byte V1 material bootstrap with flags zero satisfies the startup
contract. Water is removed from legacy material staging, so River VS68/69
scaling cannot overlap the new PS. Terrain/leaf preparation remains independent.
Native Water intent and the material-role mask are different fields.

This wiring remains behind the unchanged empty `QUALIFIED_BUILDS` admission
table. The static Steam and sampled Ubi lab evidence does not qualify a production
DLL or installed package. No payload is admitted or option enabled by this
implementation. Preparation/identity failure returns no initializer; a fixed
native command refusal follows the owned pre-entry failure path. Production's
export allowlist excludes observer, comparison and capture operations.

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
Water-specific CPU checks cover explicit B selection, zero legacy flags, combined
terrain/water ordering, malformed/A/unknown payload refusal, denied qualification,
recipient preparation failure and imports from the exact packaged recipe whitelist
without repository modules. These are authored fixture checks, not a production
loader game run.
Actual production-loader lifecycle, all-edition installed-package tests, 4K
memory/performance and visible material/shadow comparisons remain separate
requirements. See [qualification](GRAPHICS-QUALIFICATION.md) and the
[native integration contract](NATIVE-GRAPHICS-INTEGRATION.md).

## Local launcher startup checks

Use `dotnet publish`, rather than the executable from `dotnet build`, when
checking the portable EXE. The SDK applies `AppHostDotNetSearch=AppRelative` and
`AppHostRelativeDotNet=DesktopRuntime` to the published apphost only. A normal
build EXE still searches the global runtime and can display a misleading .NET
installation dialog even when DesktopRuntime is present beside it. This is
[documented SDK behavior](https://learn.microsoft.com/en-us/dotnet/core/project-sdk/msbuild-props#apphostdotnetsearch).

Retain the project's exact 10.0.15 runtime pin. Place the verified redistributable
DesktopRuntime beside the published EXE and run that EXE's existing `--self-test`
with inherited `DOTNET_ROOT*` overrides removed. Host tracing can verify the
actual local hostfxr/framework paths; keep traces private under work/. The local
native-graphics launcher check resolved that app-relative runtime and started
the UI without requiring a global .NET10 installation. A developer DLL-only
build can instead be run through the explicitly selected bundled dotnet.exe.
