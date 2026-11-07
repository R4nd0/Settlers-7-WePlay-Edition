# Maximum-profile shader sources

These are shader sources and SMAA lookup textures, not game assets or binary
add-ons. Sources and licenses are installed with the enabled profile. Maximum
also loads the separately built, hash-pinned WePlay atmosphere clock bridge;
arbitrary external add-ons are refused.

* [DH ReShade shaders](https://github.com/AlucardDH/dh-reshade-shaders/tree/0783b8fde77df48412d79070be7de4bc3743b898),
  AlucardDH, commit `0783b8fde77df48412d79070be7de4bc3743b898`:
  `dh_uber_rt.fx` 0.20.6 and `dh_uber_motion.fx` 0.2.0, GPL-2.0
  (`DH-LICENSE.txt`). WePlay changes dated 2026-11-07: corrected include casing;
  the RT effect falls back to the input when
  depth is unavailable; tone-map bounds are passed explicitly to remove an
  upstream read/write feedback hazard. Motion processing is otherwise unchanged. Complete
  modified sources are included. Source line endings/trailing spaces are
  normalized. GPU noise does not use the game's random state.
* [ReShade common headers](https://github.com/crosire/reshade-shaders/tree/fd0022170615ce0d8162d219bff07232fa6dd84f),
  commit `fd0022170615ce0d8162d219bff07232fa6dd84f`:
  `ReShade.fxh` and `ReShadeUI.fxh`, CC0-1.0, SPDX notices retained.
* [SweetFX SMAA](https://github.com/CeeJayDK/SweetFX/tree/93ddf39b357f5da534ed6d34ba4ec8cc7dcfa361),
  commit `93ddf39b357f5da534ed6d34ba4ec8cc7dcfa361`:
  `SMAA.fx`, `SMAA.fxh`, `AreaTex.png` and `SearchTex.png`, MIT
  (`SweetFX-LICENSE.txt` and the notice in `SMAA.fxh`). Shader logic is unmodified;
  line endings/trailing spaces are normalized.

`WePlay-World.fxh` is a separate WePlay Community MIT-licensed helper.
`WePlayAtmosphere.addon32` is built from `native/atmosphere_addon.cpp` and
`native/atmosphere_clock.h` (MIT) with the official [ReShade 6.8.0 SDK](https://github.com/crosire/reshade/tree/18deaa52de0c425a78b329e9cb3c497281cd00ec/include)
(BSD-3-Clause OR MIT header notices; BSD-3-Clause distribution license retained
in `Atmosphere-LICENSE.txt`). The bridge reads the guarded sun timeline and
supplies three shader uniforms. It makes no game calls or game-memory writes.
The upstream effects retain their own licenses; the project's top-level license
does not replace them. Screen-space GI requires visible depth/color geometry;
it is not hardware ray tracing and cannot reconstruct off-screen light sources.
