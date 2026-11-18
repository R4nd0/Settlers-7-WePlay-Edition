#pragma once
#ifdef S7_LOCAL_PAIR_LAB
#include <atomic>
#include <cstdint>
// Set only after a complete known laboratory executable hash matches.
// Host-order IPv4. Each owned copy can independently bind the same UDP ports.
inline std::atomic<uint32_t> local_pair_bind{0x7f000001u};
inline constexpr bool IsLocalLabSlot(uint32_t address) {
#ifdef S7_LOCAL_THREE_LAB
    return address >= 0x7f000002u && address <= 0x7f000004u;
#else
    return address == 0x7f000002u || address == 0x7f000003u;
#endif
}
#endif
