#include "memory_scan.h"
#include <cstdio>
#include <cstdlib>

unsigned checks = 0;
void Require(bool okay) { ++checks; if (!okay) std::abort(); }
struct Host {
    uint64_t ticks = 0, cost = 0, extent = S7MemoryLimit;
    uint32_t calls = 0;
    unsigned bad = 0;
    bool Clock(uint64_t& now) { now = ticks; return bad != 6; }
    bool Query(uint64_t address, S7MemoryRegion& region, uint32_t& error) {
        ++calls; ticks += cost;
        if (bad == 1) { error = 5; return false; }
        region = {address, extent, calls % 2 != 0};
        if (bad == 2) region.size = 0;
        if (bad == 3) region.base = address + 1;
        if (bad == 4) { region.base = 1; region.size = UINT64_MAX; }
        if (bad == 5) region = {0, address, true};
        return true;
    }
    S7MemoryScanResult Run(uint64_t frequency = 1000000) {
        return S7MemoryScan(frequency, [this](uint64_t& now) { return Clock(now); },
            [this](uint64_t address, S7MemoryRegion& region, uint32_t& error) { return Query(address,region,error); });
    }
};
int main() {
    Host h;
    auto r = h.Run(); Require(r.status == 0 && r.regions == 1 && r.free_bytes == S7MemoryLimit && r.largest_bytes == S7MemoryLimit);
    h = {}; h.extent = S7MemoryLimit / 4;
    r = h.Run(); Require(r.status == 0 && r.regions == 4 && r.free_bytes == S7MemoryLimit / 2 && r.largest_bytes == S7MemoryLimit / 4);
    for (unsigned bad = 1; bad <= 6; ++bad) {
        h = {}; h.bad = bad; if (bad == 5) h.extent = S7MemoryLimit / 2;
        r = h.Run(); Require(r.status != 0 && r.free_bytes == 0 && r.largest_bytes == 0);
        if (bad == 1) Require(r.status == 1 && r.error == 5);
    }
    h = {}; h.extent = 4096;
    r = h.Run(); Require(r.status == 3 && r.regions == 20000 && !r.free_bytes && !r.largest_bytes);
    h = {}; h.cost = 50001;
    r = h.Run(); Require(r.status == 4 && r.regions == 1 && !r.free_bytes && !r.largest_bytes);
    h = {}; h.cost = 50000;
    r = h.Run(); Require(r.status == 0 && r.elapsed_us == 50000);
    h = {}; r = h.Run(0); Require(r.status == 5 && h.calls == 0);
    uint64_t calls = 0;
    r = S7MemoryScan(1000000, [&calls](uint64_t& now) { now = ++calls == 1 ? 10 : 9; return true; },
        [](uint64_t, S7MemoryRegion&, uint32_t&) { return true; });
    Require(r.status == 5 && r.regions == 0);
    std::printf("PASS %u native memory scan bounds/clock/range checks\n", checks);
}
