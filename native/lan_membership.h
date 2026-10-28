#pragma once
#include "lan_address.h"
#include <array>
#include <string>
#include <cstdint>
#include <limits>

namespace lan_membership {
constexpr size_t MaxPeers = 7;
constexpr size_t MaxFileSize = 4096;
constexpr uint64_t MaxLifetimeMs = 30000;
struct Snapshot {
    std::array<uint32_t, MaxPeers> peers{}; // Host byte order; never logged.
    size_t count = 0;
    uint64_t expires = 0;
};

inline bool Decimal(const std::string& text, uint64_t& value) {
    value = 0;
    if (text.empty() || (text.size() > 1 && text[0] == '0')) return false;
    for (char c : text) {
        if (c < '0' || c > '9' || value > ((std::numeric_limits<uint64_t>::max)() - (c - '0')) / 10) return false;
        value = value * 10 + c - '0';
    }
    return true;
}

// Complete, ordered launcher record. No partial updates or unknown keys.
inline bool Parse(const char* bytes, size_t length, uint32_t local, uint32_t coordinator,
                  uint64_t now, Snapshot& output) {
    output = {};
    if (!bytes || !length || length > MaxFileSize) return false;
    for (size_t i = 0; i < length; ++i) {
        const auto c = static_cast<unsigned char>(bytes[i]);
        if (c != '\r' && c != '\n' && (c < 32 || c > 126)) return false;
    }
    Snapshot candidate;
    size_t start = 0, line_number = 0;
    bool coordinator_listed = coordinator == local || (coordinator >> 24) == 127;
    while (start < length) {
        const char* newline = static_cast<const char*>(memchr(bytes + start, '\n', length - start));
        if (!newline) return false;
        std::string line(bytes + start, static_cast<size_t>(newline - bytes) - start);
        start = static_cast<size_t>(newline - bytes) + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        if (line_number == 0) { if (line != "format=s7-lan-peers-v1") return false; }
        else if (line_number == 1) {
            uint32_t address = 0;
            if (line.compare(0, 6, "local=") != 0 || !ParsePrivateLanAddress(line.c_str() + 6, address) || address != local) return false;
        } else if (line_number == 2) {
            if (line.compare(0, 16, "expires_tick_ms=") != 0 || !Decimal(line.substr(16), candidate.expires)
                || candidate.expires <= now || candidate.expires - now > MaxLifetimeMs) return false;
        } else {
            if (candidate.count == MaxPeers) return false;
            const std::string prefix = "peer_" + std::to_string(candidate.count + 1) + "=";
            uint32_t peer = 0;
            if (line.compare(0, prefix.size(), prefix) != 0 || !ParsePrivateLanAddress(line.c_str() + prefix.size(), peer)
                || peer == local) return false;
            for (size_t i = 0; i < candidate.count; ++i) if (candidate.peers[i] == peer) return false;
            candidate.peers[candidate.count++] = peer;
            if (peer == coordinator) coordinator_listed = true;
        }
        ++line_number;
    }
    // Reserve one of eight total addresses for a remote permanent coordinator.
    if (line_number < 3 || candidate.count + (coordinator_listed ? 0 : 1) > MaxPeers) return false;
    output = candidate;
    return true;
}

inline bool Contains(const Snapshot& snapshot, uint32_t address, uint64_t now) {
    if (snapshot.expires <= now) return false;
    for (size_t i = 0; i < snapshot.count; ++i) if (snapshot.peers[i] == address) return true;
    return false;
}
} // namespace lan_membership

// Platform API names are UTF-8. Bound bytes, reject invalid scalar encodings,
// C0/C1 controls and Unicode line/paragraph separators; do not alter identity.
inline bool ValidLocalDisplayName(const char* text, size_t& byte_count) {
    byte_count = 0;
    if (!text) return false;
    while (byte_count <= 24 && text[byte_count]) ++byte_count;
    if (!byte_count || byte_count > 24) return false;
    size_t index = 0;
    while (index < byte_count) {
        unsigned char first = static_cast<unsigned char>(text[index++]);
        uint32_t scalar = first;
        size_t continuation = 0;
        uint32_t minimum = 0;
        if (first < 0x80) {}
        else if (first >= 0xc2 && first <= 0xdf) { scalar = first & 0x1f; continuation = 1; minimum = 0x80; }
        else if (first >= 0xe0 && first <= 0xef) { scalar = first & 0x0f; continuation = 2; minimum = 0x800; }
        else if (first >= 0xf0 && first <= 0xf4) { scalar = first & 7; continuation = 3; minimum = 0x10000; }
        else return false;
        if (continuation > byte_count - index) return false;
        while (continuation--) {
            unsigned char next = static_cast<unsigned char>(text[index++]);
            if ((next & 0xc0) != 0x80) return false;
            scalar = (scalar << 6) | (next & 0x3f);
        }
        if (scalar < minimum || scalar > 0x10ffff || (scalar >= 0xd800 && scalar <= 0xdfff)
            || scalar < 32 || (scalar >= 0x7f && scalar <= 0x9f) || scalar == 0x2028 || scalar == 0x2029) return false;
    }
    return true;
}
