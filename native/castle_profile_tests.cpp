#include "castle_profile.h"
#include <vector>
#include <cstring>
#include <cstdio>

int main() {
    using namespace castle_profile;
    int checks = 0, failures = 0;
    auto check = [&](bool value) { ++checks; if (!value) ++failures; };
    std::vector<unsigned char> memory(0x10000);
    auto put = [&](uint32_t address, uint32_t value) { memcpy(memory.data() + address, &value, 4); };
    auto vector = [&](uint32_t address, uint32_t begin, uint32_t bytes) {
        put(address, begin); put(address + 4, begin + bytes); put(address + 8, begin + bytes);
    };
    const Layout layout{0x1000, 0x1110, 0x2220, 0x3330};
    put(0x1000, 0x2000); put(0x2004, 0x3000); put(0x3000, layout.manager_type);
    put(0x3004, 0); vector(0x3008, 0x4000, 0x1d0);
    put(0x4000, layout.profile_type); put(0x4060, 1); vector(0x40e8, 0x6000, 2 * 0x48);
    for (uint32_t castle : {0x6000u, 0x6048u}) {
        put(castle, layout.castle_type);
        vector(castle + 0x24, 0x8000, 3 * 16);
        vector(castle + 0x30, 0x9000, 3 * 4);
        vector(castle + 0x3c, 0xa000, 3 * 4);
    }
    auto read = [&](uint32_t address, void* destination, size_t size) {
        if (address > memory.size() || size > memory.size() - address) return false;
        memcpy(destination, memory.data() + address, size); return true;
    };
    const auto original = memory;
    check(Selected(layout, read) == 0x6048 && memory == original);
    put(0x4060, 0); check(Selected(layout, read) == 0x6000); memory = original;
    for (uint32_t invalid : {2u, 0xffffffffu}) {
        put(0x4060, invalid); check(Selected(layout, read) == 0); memory = original;
    }
    for (uint32_t field : {0x1000u, 0x2004u, 0x3000u, 0x4000u, 0x6048u}) {
        put(field, 0); check(Selected(layout, read) == 0); memory = original;
    }
    put(0x3004, 1); check(Selected(layout, read) == 0); memory = original;
    put(0x3004, 0xffffffff); check(Selected(layout, read) == 0); memory = original;
    for (uint32_t field : {0x3008u, 0x40e8u, 0x606cu, 0x6078u, 0x6084u}) {
        const uint32_t saved = *reinterpret_cast<const uint32_t*>(memory.data() + field);
        put(field + 4, saved); check(Selected(layout, read) == 0); memory = original;
        put(field + 4, saved - 4); check(Selected(layout, read) == 0); memory = original;
        put(field + 8, saved); check(Selected(layout, read) == 0); memory = original;
        put(field, 0xfffffff0); check(Selected(layout, read) == 0); memory = original;
    }
    put(0x607c, 0x9008); check(Selected(layout, read) == 0); memory = original;
    put(0x6088, 0xa008); check(Selected(layout, read) == 0); memory = original;
    put(0x6074, 0x8000 + 1025 * 16); check(Selected(layout, read) == 0); memory = original;
    check(Selected(layout, [](uint32_t, void*, size_t) { return false; }) == 0);
    for (uint32_t blocked : {0x1000u, 0x2004u, 0x3000u, 0x3004u, 0x3008u, 0x4000u,
                            0x4060u, 0x40e8u, 0x6048u, 0x606cu, 0x6078u, 0x6084u}) {
        auto partial = [&](uint32_t address, void* destination, size_t size) {
            return address != blocked && read(address, destination, size);
        };
        check(Selected(layout, partial) == 0);
    }
    check(memory == original);
    std::printf("%s %d castle profile checks\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
