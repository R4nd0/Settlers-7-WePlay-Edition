#pragma once
#include <cstdint>

struct S7MemoryScanResult {
    uint32_t size, version, status, error, regions, elapsed_us;
    uint64_t free_bytes, largest_bytes;
};
static_assert(sizeof(S7MemoryScanResult) == 40, "Memory scanner ABI");
struct S7MemoryRegion { uint64_t base, size; bool free; };
constexpr uint64_t S7MemoryLimit = uint64_t{1} << 32;

template<class Clock, class Query>
S7MemoryScanResult S7MemoryScan(uint64_t frequency, Clock clock, Query query) {
    S7MemoryScanResult result{sizeof(S7MemoryScanResult), 1, 1, 0, 0, 0, 0, 0};
    uint64_t start = 0, now = 0;
    if (!frequency || !clock(start)) { result.status = 5; return result; }
    uint64_t address = 0, free = 0, largest = 0;
    auto expired = [&]() {
        if (!clock(now) || now < start) { result.status = 5; return true; }
        const auto ticks = now - start;
        result.elapsed_us = ticks > frequency ? 1000000 :
            static_cast<uint32_t>(static_cast<double>(ticks) / frequency * 1000000);
        if (ticks > frequency / 20) { result.status = 4; return true; }
        return false;
    };
    while (address < S7MemoryLimit) {
        if (expired()) return result;
        if (result.regions >= 20000) { result.status = 3; return result; }
        S7MemoryRegion region{};
        if (!query(address, region, result.error)) return result;
        ++result.regions;
        if (region.base > address || !region.size || region.size > UINT64_MAX - region.base ||
                region.base + region.size <= address) { result.status = 2; return result; }
        const auto end = region.base + region.size < S7MemoryLimit ? region.base + region.size : S7MemoryLimit;
        if (region.free) {
            free += end - address;
            if (end - address > largest) largest = end - address;
        }
        address = end;
    }
    if (expired()) return result;
    result.status = 0;
    result.free_bytes = free;
    result.largest_bytes = largest;
    return result;
}
