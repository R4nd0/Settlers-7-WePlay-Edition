# Windows memory and game startup

The normal WePlay desktop starter does not require 4 GiB of unused physical RAM.
Windows allocates memory when the game asks for it and can trim working sets
or use the page file. Free physical pages, system commit headroom and the
32-bit game's own virtual address space are different measurements.

The previous desktop path reused the isolated graphics experiment's strict
4 GiB physical-RAM and commit budget. This could reject a normal game start
despite ample Windows commit headroom. The desktop now uses a separate policy:

- Low physical availability is advisory, not a startup quota.
- Less than 4 GiB system commit headroom produces a warning, not a 4 GiB minimum.
- At or below 1 GiB system commit headroom, startup stops at the same critical
  threshold already used by the running memory sentinel. This is a precaution,
  not an official minimum requirement or proof that Windows cannot allocate.
- Invalid/unavailable measurements do not silently pass.
- The check runs again immediately before the owned game is spawned.

No memory is preallocated to satisfy a check. WePlay does not change the page
file, close other applications, trim their working sets or kill a running match.
The existing child-handle/image checks and pressure thresholds remain unchanged;
the monitor observes the game's private bytes, free/largest virtual regions and system
pressure. A successful preflight cannot guarantee later allocations will work.

The strict experimental graphics launcher keeps its original 4 GiB budget.
This change does not qualify an experimental renderer or fix the Kartograph.

An address-space scan that exceeds its 50 ms deadline is discarded and restarted
once from address zero. Each attempt retains the 20,000-region limit and all
query/range checks. A second timeout or any other error still fails monitoring
and requests recovery; incomplete or old measurements are never substituted.
The final query must also finish within the deadline. Memory-pressure thresholds,
the ten-second scan interval and the startup budgets are unchanged.

The earlier single-retry correction addressed transient scan timeouts, which are
distinct from measured memory pressure. An isolated DXVK lobby diagnostic identified a scan-budget failure;
the following run recorded no monitoring failure, but did encounter later physical
memory pressure and a separate Gold connection error. Neither run establishes
stable crossplay or proved the cause of that scheduling delay.

RC3.27 moves the complete address scan into one fingerprint-bound native call.
The previous loop released and reacquired Python's GIL on every Windows query.
Under a competing Python thread, the old monitor exhausted both attempts in
four of four samples against an owned Gold world; the new monitor completed
twenty of twenty. The native loop keeps the same 50 ms wall-clock deadline,
20,000-region maximum, range/query checks and rejection of a slow final query.
It uses the original validated child HANDLE and returns only aggregates.
The library remains locked against replacement while loaded. Installed packages
require their own adjacent pinned DLL; source debugger tools can use the exact
qualified build in the repository's private hash-named cache.

Twenty-one Python memory-guard tests and fourteen native bound/clock/range checks
pass. Failures retain recovery behavior and now record a bounded category/stage,
without storing exception messages, paths or game memory. See the
[RC3.27 package and acceptance notes](RC3.27-MEMORY-GOLD.md).

Regression checks exercise low physical RAM with high commit, the former 4 GiB
commit boundary as a warning, the critical 1 GiB boundary, invalid samples and
the real desktop before-game callback using invented process fixtures.

Microsoft documents these distinctions in
[MEMORYSTATUSEX](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex)
and [memory/address-space limits](https://learn.microsoft.com/en-us/windows/win32/memory/memory-limits-for-windows-releases).
The actual production counters use system-wide `GetPerformanceInfo`, rather
than mistaking the 64-bit launcher's virtual address space for the x86 game's.
Python documents the GIL behavior of foreign calls in
[ctypes](https://docs.python.org/3.14/library/ctypes.html#ctypes.CDLL).
