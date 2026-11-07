# Mittsommer lighting and restrained ReShade tone

Unreleased development, locally verified on 2026-11-07. Published RC3.18
downloads and the user's existing installation are unchanged.

The optional **Mittsommer / Midsummer** environment repeats:

**warm bright twilight → clear daylight → warm bright twilight → daylight**.

One twilight-to-twilight period is 20 minutes of native environment time. The
sun elevation varies smoothly between 14 and 48 degrees. HDR exposure stays
between 2.6 and 3.05; the stock Night environment is about 0.196. Night and rainy
night are not part of this cycle. This is an artistic northern-summer profile,
not an astronomical latitude simulation.

## Use

In a package built with this capability:

1. In the game, open **Options → Gameplay → Environment**
   (**Optionen → Spielelemente → Umwelt**) and select
   **Mittsommer / Midsummer**. This is a local graphics preference and also
   works with native Direct3D9. Existing weather defaults remain unchanged.
2. Optionally enable **Gentle colors and highlights / Sanfte Farben und
   Spitzlichter** in the launcher's Graphics page and save. This uses ReShade
   with **Modern graphics**. First enablement downloads about 4 MB from the
   official ReShade website. Subsequent launches use the verified local copy.

The lighting profile supplies the changing sunlight, native exposure and
shadows. The independent ReShade effect gently lifts shadows, restrains
saturation and softens highlights. It uses color only, without depth access,
extra blur, grain or another ambient-occlusion pass. Like other full-screen
color passes, it also slightly affects the HUD. It does not create HDR output
or new texture detail.

Choose another game environment to restore stock weather. Disable the launcher
tone option to stop adding our ReShade layer on the next launch. No global
Vulkan layer, driver, registry entry or replacement `d3d9.dll` is installed.
Menu and intro weather retain their original behavior. Ordinary campaign
weather cues during gameplay are suppressed while Mittsommer is selected.
The visual timeline is not added to the simulation or NEO save ledger; an
exact phase across loading or environment re-creation is not guaranteed.

## Resource and package integrity

`tools/midsummer/profile.py` generates the environment from the recipient's
own fingerprint-locked `Neutral.xml`. No game XML or third-party runtime DLL
is stored in this repository or bundled in the preparation package. Unrelated
wind, water, foliage and atmospheric curves are retained.

A new `Config/Environment/WePlayMidsummer.xml` resource is appended to the
verified shared BBA. Existing files and their payload locations remain intact.
The addition updates the terminal directory link and uses native CRC32 lookup
with linear probing. An arbitrary free hash slot passes a simple archive
round-trip but fails the game's real resource lookup; the regression suite
specifically checks collision traversal. Missing native resources silently
fall back to Neutral, so the runtime detects that result and reports a missing
profile instead of presenting it as a successful lighting change.

The installer adds the fifth weather choice and its local Lua graphics hook
to the verified interface. It installs the same new game resources on all
peers regardless of the selected environment. Normal content matching still
applies: all peers need the same new package. Choosing the effect does not
rewrite game resources, send simulation commands or consume random numbers.

Build Windows preparation packages with `--midsummer-profile` together with
the current `--neo-logistics`, `--game-directory` and Modern graphics payload.
The flag is optional; legacy preparation packages retain their original path.
The final native adapter remains the previously verified graphics/range build.

The launcher downloads the pinned official ReShade 6.8.0 setup, validates its
SHA256, and extracts only its x86 Vulkan runtime/manifest. It does not execute
the setup program. Runtime files live below `LocalData/ReShade-Midsummer`;
the effect and preset are authored by WePlay Community. Vulkan layer and base
path selection are supplied only to the owned game child. ReShade's normal
addition of UI defaults on exit is accepted without invalidating the next
launch. The original renderer DLL and game-content identity remain intact.

Upstream references: [ReShade download and distribution instructions](https://reshade.me/),
[ReShade FX reference](https://github.com/crosire/reshade-shaders/blob/slim/REFERENCE.md),
[base-path selection](https://github.com/crosire/reshade/blob/v6.8.0/source/dll_main.cpp).

## Observed verification

Private evidence: `work/midsummer-20261107`.

- Actual Grassland Mesa gameplay, fixed far camera, highest native quality,
  1280×720, RX 9070 XT, two physical cores. A private 12-second version of the
  same generated curves was used to inspect repeated daylight/twilight changes
  and moving shadow lengths. The production period remains 1,200,000 ms.
- The native resource loaded as a distinct handle instead of Neutral. The
  production Lua hook selected weather mode 5. ReShade loaded through the
  explicit Vulkan layer and successfully compiled the custom effect (3 ms in
  the foreground test). A second actual game start with the same runtime after
  ReShade's clean-exit configuration update also loaded and compiled it. No
  shader compile error was observed.
- The actual in-game Environment dropdown displayed the fifth choice. Selecting
  it, applying, closing and reopening Options retained Mittsommer. The second
  owned test client exited normally and restored its source files and profile.
- A five-second foreground screen-update-loop sample recorded 301 consecutive
  intervals, median 16.667 ms, p95 17.008 ms, maximum 19.217 ms, none over
  50 ms. This is an operational screen-loop check, not Vulkan presents, GPU
  timing or a claim of unchanged GPU cost.
- A late multiplayer save loaded on two isolated clients and ran turns
  67349–67849. A selected stock settled weather with ReShade; B selected
  Mittsommer without ReShade. Both had the same new archives. Both exited
  normally, both replay gzip streams completed, and all 451 structured replay
  CRC records matched (`work/weavery-20261106/pair-09`). No CRC substitution,
  content-ID exclusion or synchronization bypass was added.
- Python resource, collision, strict-choice, installation, local-layer and
  normal-exit regression checks pass. The Lua 5.1 behavior fixtures separately
  cover activation, stock off-mode, campaign cues, menu scope and a missing
  resource. The Windows launcher build and bilingual UI self-test pass.

The runs restored the isolated source copies and profiles. This does not
establish long-session GPU performance, separate-PC Internet qualification,
Steam gameplay or native Linux qualification for the new effect.
