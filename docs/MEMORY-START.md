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
The existing child-handle/image checks and memory sentinel remain unchanged;
they observe the game's private bytes, free/largest virtual regions and system
pressure. A successful preflight cannot guarantee later allocations will work.

The strict experimental graphics launcher keeps its original 4 GiB budget.
This change does not qualify an experimental renderer or fix the Kartograph.

Regression checks exercise low physical RAM with high commit, the former 4 GiB
commit boundary as a warning, the critical 1 GiB boundary, invalid samples and
the real desktop before-game callback using invented process fixtures.

Microsoft documents these distinctions in
[MEMORYSTATUSEX](https://learn.microsoft.com/en-us/windows/win32/api/sysinfoapi/ns-sysinfoapi-memorystatusex)
and [memory/address-space limits](https://learn.microsoft.com/en-us/windows/win32/memory/memory-limits-for-windows-releases).
The actual production counters use system-wide `GetPerformanceInfo`, rather
than mistaking the 64-bit launcher's virtual address space for the x86 game's.
