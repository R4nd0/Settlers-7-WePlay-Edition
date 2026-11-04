#pragma once
#include <stdint.h>

namespace terrain_angle {
// The terrain footprint constructor takes binary32 degrees. Beyond this
// conservative limit the native x87 trig helpers cannot reduce every argument.
// Keep all ordinary inputs (including their exact bits) on the native path.
inline bool NeedsReduction(uint32_t bits) {
    const uint32_t magnitude = bits & 0x7fffffffu;
    return magnitude >= 0x5f000000u && magnitude < 0x7f800000u;
}
inline uint32_t Reduce(uint32_t bits) {
    if (!NeedsReduction(bits)) return bits;
    // Every float in this range is an exact integer. Integer modulo avoids
    // host-dependent trig, rounding modes and changes to the FP environment.
    uint32_t remainder = ((bits & 0x7fffffu) | 0x800000u) % 360u;
    const uint32_t shift = ((bits >> 23) & 0xffu) - 127u - 23u;
    for (uint32_t i = 0; i < shift; ++i) remainder = (remainder * 2u) % 360u;
    const uint32_t sign = bits & 0x80000000u;
    if (!remainder) return sign;
    uint32_t exponent = 0;
    for (uint32_t value = remainder; value > 1u; value >>= 1) ++exponent;
    return sign | ((127u + exponent) << 23)
        | ((remainder << (23u - exponent)) & 0x7fffffu);
}
}
