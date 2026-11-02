#pragma once
#include <stdint.h>

namespace lobby_teams {
struct Layout { uint32_t manager_global, manager_type, castle_type; };
struct Player { uint32_t configuration = 0, id = 0; uint8_t previous_team = 0; };

// Only the preparation buffer belongs to lobby updates. The current buffer can
// contain a running game or a loaded replay and must never be selected here.
template<class Read> bool Find(const Layout& layout, uint32_t address, Read read, Player& result) {
    result = {};
    uint32_t manager = 0, type = 0, configuration = 0, players = 0, readable = 0;
    uint8_t mode = 0, slots = 0;
    if (!read(layout.manager_global, &manager, 4) || !manager || manager > UINT32_MAX - 0x190
        || !read(manager, &type, 4) || type != layout.manager_type
        || !read(manager + 4, &mode, 1) || mode > 1
        || !read(manager + ((mode - 1) & 1) * 0xc4 + 0x98, &configuration, 4)
        || !configuration || configuration > UINT32_MAX - 8
        || !read(configuration + 4, &players, 4) || !players || players > UINT32_MAX - 0x8dc0
        || !read(configuration, &readable, 4) || readable != players
        || address < players || (address - players) % 0x5a8) return false;
    const uint32_t id = (address - players) / 0x5a8;
    if (!id || id > 8 || !read(players + 0x8db8, &slots, 1) || !slots || slots > 8 || id > slots
        || !read(address + 0x55c, &type, 4) || type != layout.castle_type
        || !read(address + 0x64, &type, 4) || (type != 1 && type != 2)
        || !read(address + 0x68, &result.previous_team, 1) || result.previous_team > 8) return false;
    // Native SetTeamAffiliation walks the registry to find the new teammates.
    uint32_t registry = 0, registry_next = 0, seen_players = 0, registered = 0;
    if (!read(players + 0x8dbc, &registry, 4) || !registry || registry > UINT32_MAX - 12
        || !read(registry, &registry_next, 4)) return false;
    while (registry_next != registry) {
        uint8_t player = 0;
        if (!registry_next || registry_next > UINT32_MAX - 12 || seen_players++ >= 25
            || !read(registry_next + 8, &player, 1) || player >= 25 || (registered & (1u << player))) return false;
        registered |= 1u << player;
        if (!read(registry_next, &registry_next, 4)) return false;
    }
    if (!(registered & (1u << id))) return false;
    // Validate every list the native reciprocal update can walk. Neutral Type8
    // entries with Team0 are excluded from both removal and new-team lookup.
    // Their legacy lists can contain repeated IDs and are never rewritten.
    for (uint32_t player = 0; player < 25; ++player) {
        const uint32_t entry = players + player * 0x5a8;
        uint32_t kind = 0;
        uint8_t team = 0;
        if (!read(entry + 0x64, &kind, 4) || !read(entry + 0x68, &team, 1)) return false;
        if (kind == 8) { if (team) return false; continue; }
        if (kind != 1 && kind != 2 && kind != 4) return false;
        uint32_t head = 0, count = 0, node = 0, previous = 0;
        if (!read(entry + 0x554, &head, 4) || !head || head > UINT32_MAX - 12
            || !read(entry + 0x558, &count, 4) || count > 25 || !read(head, &node, 4)) return false;
        previous = head;
        uint32_t seen = 0;
        uint32_t ids = 0;
        while (node != head) {
            uint32_t back = 0, next = 0;
            uint8_t ally = 0;
            if (!node || node > UINT32_MAX - 12 || seen++ >= count
                || !read(node + 4, &back, 4) || back != previous
                || !read(node + 8, &ally, 1) || ally >= 25 || (ids & (1u << ally))
                || !read(node, &next, 4)) return false;
            ids |= 1u << ally;
            previous = node;
            node = next;
        }
        uint32_t tail = 0;
        if (seen != count || !read(head + 4, &tail, 4) || tail != previous) return false;
    }
    result.configuration = configuration;
    result.id = id;
    return true;
}
}
