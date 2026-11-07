# RC3.19 graphics measurements

2026-11-07: Ryzen 7 9800X3D, Radeon RX 9070 XT, 1920x1080, DXVK 3.1.1,
two physical cores, Normal priority, VSync and the existing 60-FPS cap.
Three fresh, serialized clients load the same late save and matching near,
medium and far camera positions. Simulation is held at the camera checkpoint;
rendering and particles keep updating; the native light clock is held too. Wait seven
seconds after each camera change, then collect 15 seconds per position.

Pinned Intel PresentMon 2.6.0 collects only the owned PID with input tracking
disabled. All nine captures exit normally, start/stop markers exist, and no
events are lost or overflowed. Its generic privilege warning is retained;
the captures contain the correct process and valid frame/GPU measurements.

| Profile | Near GPU busy median | Medium | Far | Dedicated GPU memory |
| --- | ---: | ---: | ---: | ---: |
| Previous WePlay, 4096 shadows | 4.192 ms | 4.042 ms | 4.174 ms | 1789.3 MiB |
| New effects, 4096 shadows | 4.266 ms | 4.144 ms | 4.242 ms | 1800.6–1805.2 MiB |
| New effects, 8192 Ultra | 5.010 ms | 4.903 ms | 4.822 ms | 2055.6–2060.0 MiB |

New effects versus previous WePlay differ by 0.068–0.102 ms in these medians;
one pass cannot distinguish every small difference from ordinary run variation.
Ultra versus the recommended candidate adds 0.580–0.759 ms and roughly
250–260 MiB. Live guarded reads confirm 4096 versus 8192 maps and the expected
Ultra cascade distribution. Both alternatives retain the longer range.

All profiles produce approximately 60 frames/s at the cap. Their producer-frame
p95 intervals span 19.164–21.506 ms, with no observed interval above 50 ms.
PresentMon FrameTime describes production intervals; DisplayedTime/DisplayLatency
are kept separately. These numbers do not prove an equivalent displayed rate.
GPU busy is actual measured busy time, not GPUTime including queue waits.

Windows GPU Process Memory counters sample the target PID three times per
camera. The baseline was sampled once after the far capture, so its repeated
memory value is a run-level reference. Dedicated allocation is not a VRAM
budget or asset-only count. Shared allocation is about 375.7 MiB in these runs.
Test copies and profiles are restored after each run.

The recommended profile therefore keeps 4096 shadows and enables the new
effects, longer detail and range. Ultra remains independently available.
This nine-capture comparison predates the subsequent full-azimuth sun orbit;
it does not measure every possible sun direction or light phase.
This is a single-machine recommendation; the cap hides uncapped headroom.
No claim is made for other cards, resolutions, or sustained GPU-bound gameplay.

Sanitized machine-readable results:
[graphics-rc319-20261107.json](verification/graphics-rc319-20261107.json).
Raw PID-scoped CSVs and diagnostics remain private under work/visual-quality-20261107.

## Earlier cinematic prototype

A later isolated near-camera comparison holds the same sun direction and
checkpoint to compare the actual expanded shader. Recommended versus Maximum:
GPU busy medians **4.439 versus 4.711 ms**, dedicated allocations **1799.95
versus 1805.20 MiB**. Both 15-second captures are complete with no lost events;
899/897 frames produce approximately 60 frames/s, p95 19.680/21.664 ms,
maximum 33.031/32.946 ms, no interval above 50 ms. This is a matched short
comparison on this PC, not a universal or uncapped performance claim.

The moving sun was checked separately at active simulation speed with production
curve values and a private 12-second duration. The fixed screenshot fixture
also holds the native light clock and must not be used as a moving-light test.
A 36-second live-city capture alongside two other owned clients has 1951 frames,
54.233 producer Hz, p95 43.440 ms, maximum 175.544 ms and 63 intervals over
50 ms. That contended test does not isolate the cost of the orbit or shaders;
it is retained rather than presented as a clean 60-FPS gameplay guarantee.

## Expanded Maximum: GI and atmosphere

The earlier four-pass prototype above was superseded by DH screen-space GI/AO,
motion estimation, SMAA, haze and heat refraction. In the same 1080p near-camera
fixture, the expanded profile records a **7.3075 ms** median GPU busy time and
**2123.09 MiB** dedicated GPU allocation. Its 15-second capture has 897 frames,
p95 23.091 ms, maximum 41.293 ms, no interval over 50 ms and no lost events.
The 4.439 ms recommended-profile reference remains a separate earlier run.
These measurements precede the correction from broad HUD edge strips to
compact HUD footprints; they do not measure every camera or active gameplay.
The shader observer confirms all seven techniques active, valid dynamic depth
and an advancing GPU timer. The upstream tone-map read/write hazard was fixed
before this capture, and all effects compile without warnings.

After removing both edge strips and rectangular HUD restoration, the final
full-frame profile records **7.2244 ms** median GPU busy time and **2123.09 MiB**
dedicated allocation. Its complete 15-second capture contains 899 frames,
p95 23.802 ms, maximum 55.346 ms, **one interval over 50 ms**, no overflow or
lost events. This remains a capped, paused-simulation screenshot fixture;
it does not establish sustained gameplay throughput or perfect frame pacing.
The full frame, including UI, receives light/GI/haze; only heat refraction
uses compact HUD exclusions. No private diagnostic add-on is distributed.

The subsequent local multiplayer reload with the expanded GI/atmosphere chain
completes turns 67349--67849 in 238.3 seconds. Both clients exit normally;
both replay gzip streams complete and **451/451** structured CRCs match, with
zero mismatches. The final removal of HUD restoration is a local render-only
change after that pair. This is loopback on one PC, not two-PC qualification.
