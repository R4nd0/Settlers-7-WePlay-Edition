# Water palette in one process

The requested evaluation flow is a native in-game selector for Original,
Water B, Clear bank and Calm gloss, all within one game process. It compares
three fixed recipient-generated River pixel programs. Index 0 is the exact
accepted Water B program; its optics
and bytecode remain unchanged. OFF always uses the original game program.
The two alternatives have not received visual acceptance.

| Index | Name | F0 | Grazing cap | Depth transfer |
| --- | --- | --- | --- | --- |
| 0 | accepted_B | 0.05 | 0.78 | L²(3−2L) |
| 1 | clear_bank | 0.035 | 0.70 | L²(2.4−1.4L) |
| 2 | calm_gloss | 0.075 | 0.58 | L²(3−2L) |

Reflection is saturate((F0 + (cap−F0)(1−cosine)⁵) × original reflectiveness).
L is the original clamped depth-transfer result. All three depth curves are
bounded, monotone and preserve zero/one endpoints. At L=0.5, clear_bank gives
0.425 tint instead of 0.5. Its reduced reflection should make the existing
riverbed and bank transition easier to read. Calm_gloss raises normal-angle
reflection while reducing grazing reflection contrast. Waves and their
animation remain original; “calm” describes reflection intensity.

Only one shader-local optical float4 can differ from B. Clear_bank changes
four floats; calm_gloss changes two. Original River VS, the accepted B
specular program, texture reads, refraction coordinates, flow, alpha, fog and
output instructions are preserved. No extra instruction or texture fetch is
added. No shader binary or original game source is distributed in the repo.

The independent WaterPalette layout is 3096 bytes/version 2: size, version,
render width, render height, enabled and selected index are six DWORDs.
Three UTF16[512] paths begin at 24, 1048 and 2072. Legacy Water1076/version 1
remains supported. Switching configured modes requires shutdown; there is no
silent layout conversion. Within a palette, only enabled and selection may
change. Every path, recipe, content hash and ancestor remains guarded.

The first request, including OFF, validates and leases all three CPU buffers.
The first eligible original River draw creates all three GPU programs before
temporarily binding one. Safe frame-boundary switches reuse them until the
required device Release/Reset or shutdown boundary. Restoration keeps an
owned original shader reference through failures and retries. Per-program
hashes and draw/application/restoration/creation counters provide provenance;
ordinary Release preserves copied frame proof and actual Reset invalidates it.

The selector must queue choices for the owning render thread's safe boundary,
after any temporary River shader has been restored. Original requests OFF;
it must never bind a replacement shader or change the saved original program.
The other choices select only an already admitted, immutable palette member.
No file, shader coefficient or DLL is edited by a live selection.

Requested selection and actual application are distinct states. A pending
choice is not evidence of a changed world frame. Active-program proof must
come from a successful matching River draw with verified shader restoration
in the current configuration epoch. Switching choices or actual device Reset
invalidates previous proof. If the scene is presenting a cached frame, the
selector must not imply that the new program has already affected that image.
Failed restoration blocks switching and diagnostic rendering until recovered.
The overlay renderer must restore all state it changes before returning to
the game's original rendering path.

The intended scene is the existing castle river at normal zoom, including its
left bank and near shore. The primary assessment is live visual selection,
including motion. The short OFF/B/OFF/clear_bank/OFF/calm_gloss/OFF capture
batch remains optional supporting evidence. Matched pose/light and control
noise are required for image comparisons; application counters alone do not
prove benefit. The milestone below records the functional overlay tests;
visual acceptance and full release qualification remain separate.

Verification before real-game evaluation: all three owned vendor aliases
produce the same program for each index, and the system disassembler accepts
all nine outputs. Strict native CPU fixtures pass 4058 legacy and 450 palette
checks. A production-only CPU loader admits all three real recipient hashes
and OFF selections. Python tests exercise emitted equations, source preflight,
capabilities, wire shape and immutable cache reuse. GPU execution, visible
acceptance, motion and release qualification remain separate checks.

## Native live selector milestone, 2026-11-26

`--water-program --water-palette --shader-overlay` enables the diagnostic
in-game panel. Click Original, Wasser B, Klareres Ufer or Mehr Glanz to switch
without restarting the process. Ctrl+Shift+F9 hides/shows the panel while
retaining the selected water program. AKTIV requires current-frame proof;
WARTET does not establish that a replacement has rendered visible water.
The panel uses physical client coordinates before supersampling mouse mapping.
Native frame readbacks precede panel rendering, so optional comparison captures
exclude the diagnostic UI. This is not a release capability or launcher gate.

The source45/effects-v12 build passed 301 host, 75 UP, 20 SDK and 608 renderer
CPU checks. A 4.5-second GPU run verified physical panel/font pixels, unchanged
outside control pixels, all four real fixture-window clicks, actual selected
program pixels, state restoration and Reset cleanup. This is synthetic proof.

In one real Ubisoft History process at 3840x2160→1920x1080 and the observed
100-ms game clock, all four panel choices changed the actual River state.
The three ON phases each proved 120 successful matching applications and
restorations in their recorded completed frame. Shader creations stayed12
across those choices, with all three programs retained. OFF restored the
original path. Hiding retained the selected variant; the original book/menu
under the hidden panel was usable. Visual acceptance of the alternatives
remains with the user, through live play.

The existing memory guard then stopped the owned test at host commit pressure
(minimum1926MiB free commit; peak game private1914MiB). Profiles, options and
game binaries were restored with no cleanup errors. The show-again check and
new-save/normal-exit check were interrupted, not passed. Gold, Steam, performance,
the known exit crash, black streaks and final package qualification remain open.
The retained [smoke receipt](receipts/native-water-overlay-smoke-20261126.json)
records this partial outcome and does not grant release qualification.


A fresh Ubisoft History main-menu follow-up then verified both hide and show
with Ctrl+Shift+F9 and normal original-menu exit code0. Memory remained healthy
(private peak505MiB), all preservation checks passed and cleanup was empty.
No world was loaded in this follow-up; it does not replace the interrupted
world save/exit test or qualify the other editions.

Reproduce the short authored GPU check with:

```powershell
python -B tools/supersampling/effects_gpu.py --build work/BUILD --dxvk work/sgs24/Base/Bin/d3d9.dll --output work/NEW_GPU_CASE --water-palette --shader-overlay
```

The optional `quick_compare` screenshot batch has CPU coverage but has not yet
run against a real game. It is supporting evidence, not the live triage UI.
