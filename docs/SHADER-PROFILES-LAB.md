# Interactive native shader profiles

The primary workflow is one game start with the explicit combined `all`
profile. Its in-game tabs retain the choices of every group, so Water, Leaf,
Terrain, Shadow and AO can be judged together. The five older single-family
profiles remain available with their existing isolated contracts. Neither
mode can load arbitrary programs or edit shader files. Executable, source,
path, capability and original-state restoration guards remain required.

| Profile | ID | Available choices | Initial state |
| --- | --- | --- | --- |
| water | 1 | Original / Wasser B / Klareres Ufer / Mehr Glanz | Accepted Water B |
| vegetation | 2 | Original / Blatt A (schwach) / Blatt B (mittel) / Blatt C (stark) | Original |
| terrain | 3 | Original / Makro-Relief (Profil) | Original |
| shadow | 4 | Original / Schattenfilter AN | Original |
| ambient | 5 | Original / AO A (Grenze 1,00) / AO B (Grenze 1,25) | Original |
| all | 6 | Five live group tabs; all existing choices above | Water B; other groups Original |

Combined mode requires additive Init3 (3136 bytes, scope 1), material V5
(24 bytes) and overlay V3 (16 bytes, profile 6). The old initializers and
material layouts are not silently upgraded. Leaf and Terrain preload their
seven fixed programs together; switching either group preserves the other's
choices and GPU cache. Water retains its separate fixed palette. Shadow and
AO use the explicitly admitted combined drawing scope, preserving the older
mutual-exclusion rules outside that scope.

Vegetation uses three actual 1372-byte pixel programs on the existing
4120-byte/version 4 layout. Their fixed wrapped diffuse curves are
saturate(0.8*dot+0.2), saturate((2/3)*dot+1/3), and
saturate((4/7)*dot+3/7). Leaf sides should become progressively brighter and
easier to distinguish; the strongest choice can also flatten directional
contrast. Original VS, wind, alpha, ambient and separate specular stay intact.
Use the castle's foreground tree groups and left crown at normal zoom.

Terrain offers exactly one existing candidate, not three new programs. The
6180-byte/version 2 macro contract requires both detail/relief scales to stay
exactly 1.0 and allows live flags 0 or 2. Its paired VS/PS program set halves
detail UV frequency and keeps near detail farther away: Bump 12000–18000 and
Parallax 8000–12800 world units. The expected effect is larger detail forms
on lit ground, paths and sloped banks. Earlier normal-view comparisons did
not establish a convincing gain. Separate Detail and Relief buttons would
misrepresent the admitted immutable macro set and are unavailable.

Shadow reuses its existing fixed pass filter. Ambient reuses the two existing
12-byte AO profiles; both apply the same bounded 1.25 multiplier, with ceilings
1.0 or 1.25. Profile A rejects original strength above 1.0. A rejected policy
is not an applied effect. Earlier Shadow and AO views did not establish visual
acceptance, and AO B was very subtle.

`tools/graphics_lab/shader_profiles.py` supplies deterministic metadata and
guarded-run keyword data only. `PROFILES` has the five fixed command names;
`profile()`, `choice()` and `setup()` return independent data copies. Water
defaults ON at accepted B; other groups default OFF. The caller must verify
the actual build's selected single-family or combined initializer/overlay ABI
before invoking the recipient stager or native setup. Metadata grants no capability and performs
no filesystem, process or GPU operations.

Requested selection and actual current-frame application remain separate.
The renderer disables unavailable rows. Application status requires a fresh
matching family draw and verified restoration in the current configuration
epoch. Original means substitution is disabled; cached frames must not be
described as freshly changed pixels. User visual acceptance and technical
release qualification are separate from successful CPU or authored GPU tests.


## Verified combined selector milestone, 2026-11-26

The final local entry is `Start-Grafik-Debug.cmd` in
`outputs/Grafik-Debug-20261126`. It launches profile `all` once. Five tabs
change only the visible controls; selected effects remain enabled together.
Choices are automatically stored in the private case's
`shader-selection.private.json` and mirrored to `Letzte-Auswahl.json` beside
the starter. The candidate records its native build identity and keeps final
user acceptance and release qualification false. It does not change release
defaults by itself. The user's accepted combination is the intended future
default after qualification.

The source47/effects-v14 build passed strict compilation and existing/additive
CPU checks: 383 host, 75 UP, 20 SDK, 851 overlay/model, 132 combined-material
COM and retained material regressions. The authored GPU fixture verifies
actual Leaf/Terrain/Water pixels, seven cached material programs, independent
choices, state restoration and Reset. Its final rerun took8.9seconds.

An end-to-end check caught a missing preset block in the native report: the
existing JSON ended with a newline, so testing only its final character
skipped insertion. The corrected producer inserts before the last closing
brace. The actual GPU-generated report now roundtrips through the candidate
recorder, including all five groups and revision. Cleanup also takes a final
snapshot, preserving a late choice when the game closes.

In real Ubisoft History play at3840x2160→1920x1080 and the observed100-ms clock,
the user selected all five groups in ONE process. A retained completed-frame
snapshot proves Water, Leaf, Terrain, Shadow and Ambient simultaneously active,
with no pending choice or unsafe render state. Seven actual material program
hashes were validated; each had been created once. Automatic candidate
persistence was observed while the user changed tabs and variants. See the
[sanitized receipt](receipts/native-combined-shader-smoke-20261126.json).

The session was left under user control. Final save/normal-exit checks and
optical approval are pending, as are the remaining editions, performance,
known exit-crash investigation and final package qualification. Runtime
application evidence does not establish a visible benefit for every variant.
