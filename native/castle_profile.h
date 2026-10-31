#pragma once
#include <stdint.h>
#include <stddef.h>

// Read-only selection from the fingerprinted History Edition profile layout.
// Never interpret a network peer's pointers or invent a design for that peer.
namespace castle_profile {
struct Layout { uint32_t slot, manager_type, profile_type, castle_type; };
struct Vector { uint32_t begin, end, capacity; };
inline bool Count(Vector value, uint32_t stride, uint32_t maximum, uint32_t& count) {
    count = 0;
    if (!value.begin || value.begin % 4 || value.end < value.begin || value.capacity < value.end
        || (value.end - value.begin) % stride || (value.capacity - value.begin) % stride
        || (value.capacity - value.begin) / stride > maximum) return false;
    count = (value.end - value.begin) / stride;
    return count != 0;
}
template<typename Read> uint32_t Selected(const Layout& layout, Read read) {
    auto word = [&](uint32_t at, uint32_t& value) {
        return at && !(at % 4) && at <= UINT32_MAX - 4 && read(at, &value, sizeof(value));
    };
    uint32_t root = 0, manager = 0, kind = 0, selected = 0;
    if (!word(layout.slot, root) || root > UINT32_MAX - 4 || !word(root + 4, manager)
        || manager > UINT32_MAX - 0x14 || !word(manager, kind) || kind != layout.manager_type
        || !word(manager + 4, selected)) return 0;
    Vector profiles{}; uint32_t count = 0;
    if (!read(manager + 8, &profiles, sizeof(profiles)) || !Count(profiles, 0x1d0, 128, count)
        || selected >= count) return 0;
    const auto profile = profiles.begin + selected * 0x1d0;
    if (profile > UINT32_MAX - 0xf4 || !word(profile, kind) || kind != layout.profile_type
        || !word(profile + 0x60, selected)) return 0;
    Vector castles{};
    if (!read(profile + 0xe8, &castles, sizeof(castles)) || !Count(castles, 0x48, 256, count)
        || selected >= count) return 0; // Includes the native -1/no-castle choice.
    const auto castle = castles.begin + selected * 0x48;
    if (castle > UINT32_MAX - 0x48 || !word(castle, kind) || kind != layout.castle_type) return 0;
    Vector parts{}, parents{}, slots{}; uint32_t part_count = 0, parent_count = 0, slot_count = 0;
    if (!read(castle + 0x24, &parts, sizeof(parts)) || !Count(parts, 16, 1024, part_count)
        || !read(castle + 0x30, &parents, sizeof(parents)) || !Count(parents, 4, 1024, parent_count)
        || !read(castle + 0x3c, &slots, sizeof(slots)) || !Count(slots, 4, 1024, slot_count)
        || part_count != parent_count || part_count != slot_count) return 0;
    return castle;
}
}
