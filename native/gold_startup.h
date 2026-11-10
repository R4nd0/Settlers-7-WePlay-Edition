#pragma once
#include <cstdint>

namespace GoldStartup {
// Gold's old Orbit record is independent of both History SDK interfaces.
struct OrbitRecord { std::uint32_t module, unavailable, functions[22]; };
static_assert(sizeof(OrbitRecord) == 96, "Gold Orbit ABI changed");
inline bool Empty(const OrbitRecord* record, std::uint32_t unavailable) {
    if (!record || record->module || record->unavailable != unavailable) return false;
    for (auto function : record->functions) if (function) return false;
    return true;
}
struct AsciiString { void* bytes; std::uint32_t capacity, length, extra; };
// VC90 checked-container prefix precedes this build's small-string union.
struct WideString { std::uint32_t container; unsigned char storage[16]; std::uint32_t length, capacity; };
static_assert(sizeof(AsciiString) == 16 && sizeof(WideString) == 28, "Gold string ABI changed");
inline bool EmptyOutputs(const AsciiString* name, const WideString* display, const AsciiString* ticket) {
    return name && display && ticket && !name->length && !ticket->length
        && !display->length && display->capacity == 7;
}
}
