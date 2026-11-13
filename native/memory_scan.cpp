// One native call keeps Python scheduling outside the bounded read-only loop.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include "memory_scan.h"

extern "C" __declspec(dllexport) DWORD S7ScanAddressSpace(HANDLE process, S7MemoryScanResult* result, DWORD size) {
    if (!result || size != sizeof(S7MemoryScanResult)) return ERROR_INVALID_PARAMETER;
    LARGE_INTEGER frequency{};
    if (!QueryPerformanceFrequency(&frequency) || frequency.QuadPart <= 0) frequency.QuadPart = 0;
    auto clock = [](uint64_t& ticks) {
        LARGE_INTEGER now{};
        if (!QueryPerformanceCounter(&now) || now.QuadPart < 0) return false;
        ticks = static_cast<uint64_t>(now.QuadPart);
        return true;
    };
    auto query = [process](uint64_t address, S7MemoryRegion& region, uint32_t& error) {
        MEMORY_BASIC_INFORMATION info{};
        if (VirtualQueryEx(process, reinterpret_cast<void*>(address), &info, sizeof(info)) != sizeof(info)) {
            error = GetLastError();
            return false;
        }
        region = {reinterpret_cast<uint64_t>(info.BaseAddress), static_cast<uint64_t>(info.RegionSize), info.State == MEM_FREE};
        return true;
    };
    *result = S7MemoryScan(static_cast<uint64_t>(frequency.QuadPart), clock, query);
    return ERROR_SUCCESS;
}
