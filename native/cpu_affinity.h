#pragma once
#include <windows.h>
#include <vector>

struct LocalCpuCore { DWORD_PTR mask; unsigned char efficiency; };
struct LocalCpuGroupMask { DWORD_PTR mask; WORD group; };
bool ValidateWineSingleCpuGroup(bool wine, DWORD error, DWORD_PTR system,
                               const std::vector<LocalCpuGroupMask>& topology);
#ifndef S7_MAX_PHYSICAL_CORES
#define S7_MAX_PHYSICAL_CORES 4
#endif
constexpr unsigned LocalCpuCoreLimit = S7_MAX_PHYSICAL_CORES;
static_assert(LocalCpuCoreLimit == 2 || LocalCpuCoreLimit == 4, "Supported physical core limit required");
DWORD_PTR SelectLocalCpuMask(std::vector<LocalCpuCore> cores, DWORD_PTR allowed, unsigned maximum = 4);
bool ApplyLocalCpuAffinity(unsigned maximum = LocalCpuCoreLimit);
