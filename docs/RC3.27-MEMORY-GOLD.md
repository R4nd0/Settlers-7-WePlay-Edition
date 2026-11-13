# RC3.27: memory monitoring and Gold rendering

Standalone remains the default for Gold, Ubisoft History and regular Steam
History. The [RC3.26 isolation and repair contract](RC3.26-STANDALONE-STANDARD.md)
continues to apply. Original installations and game executable fingerprints
remain unchanged.

Update the launcher and update the prepared game copy when requested. Gold's
interface correction and History's copied start script require that preparation;
existing private profiles and saves are retained. The launcher continues to
reject older updates that omit the required platform-isolation contract.

The memory monitor scans the game's address space in one pinned, read-only
native call. This removes thousands of Python thread scheduling points from
each scan. The 50 ms deadline, 20,000-region bound, complete-result requirement,
single timeout retry and pressure thresholds remain unchanged. A failed scan
still requests recovery; its summary now records a bounded failure category
and stage without exception messages, paths or game memory.

Gold explicitly selects its verified DXVK configuration by absolute path at
launch. Gold changes its working directory before graphics initialization,
which previously caused DXVK to miss the configuration and render water black.
The setting is rebuilt from the current copy location, including after a move.
Inherited inline/file configuration cannot override this Gold profile.

The standard Gold interface also includes the existing camera correction:
the cumulative executed-turn counter is no longer added to the camera angle
every frame. Mouse/key rotation and camera damping remain available.

History's local service now has a bounded 60-second readiness window. A fresh
runtime exceeded the previous 15-second window before any game was launched;
an independent service-only retry completed normally in 33.2 seconds overall.
Startup still requires the validated local HTTP response and valid ports.
This window is separate from the unchanged 50 ms memory-scan deadline.

Build the native scanner with `native/build_memory_scan.ps1` into a new folder
under `work/`, then supply its exact pinned DLL through `--memory-scanner` to
the package builder. The installer includes it beside the protected Python
monitor. Changed or missing scanner bytes stop protected startup; there is
no fallback to incomplete or stale measurements.
The build also retains the exact qualified DLL in a hash-named private cache
for source-tree debugger and graphics tools. Installed and portable packages
require their own adjacent DLL and never search that development cache.

Actual isolated Gold and Ubisoft History tests reloaded their retained saved
worlds and closed normally. Gold's water had waves and visible shorelines, and
the camera remained stable across separated observations without input. In each
edition, the old scanner failed all four measurements under a competing Python
thread; the new scanner completed all 20 measurements under the same load.

| Actual game test | Monitor samples | Peak native scan | Monitor failure |
| --- | ---: | ---: | --- |
| Gold | 499 | 29.559 ms | No |
| Ubisoft History | 65 | 24.953 ms | No |

The successful Ubisoft local-service response took 18.8 seconds, exceeding the
old readiness window. Both tests retained kernel isolation and left shared
Documents profiles, executable and adapter bytes unchanged. Their 1,104 combined
process/module/socket observations contained no unexpected platform or
nonloopback connections. These are bounded observations alongside the enforced
per-copy network restrictions.

Validation also includes 202 Python tests, 14 native scanner checks, all 284 Gold
Lua scripts compiled, a bilingual launcher self-test, and actual private
installer installation/upgrades with profiles preserved. The production native
adapters are unchanged. Steam shares the scanner and platform regression checks;
there was no additional Steam world run for this fix. Gold remains single player;
experimental pair drivers are excluded from the main package.

See the [sanitized acceptance receipt](receipts/rc327-memory-gold-20261113.json).
The locally rebuilt installer includes signed update manifests and artifact
verification; published RC3.25 downloads are unchanged.
