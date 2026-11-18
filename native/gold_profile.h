#pragma once
#include <array>
#include <cstdio>
#include <cstring>

namespace GoldProfile {
inline constexpr char OriginalMutex[] = "Global\\Settlers7";
struct Names {
    std::array<char, 33> id{};
    std::array<char, 48> suffix{};
    std::array<char, sizeof(OriginalMutex)> mutex{};
};
inline Names FromDigest(const unsigned char (&digest)[32]) {
    Names result{};
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 16; ++i) {
        result.id[i * 2] = hex[digest[i] >> 4];
        result.id[i * 2 + 1] = hex[digest[i] & 15];
    }
    std::snprintf(result.suffix.data(), result.suffix.size(), "\\WePlay\\Gold\\%s", result.id.data());
    // Keep the entire original field, including its terminator, backed by the array.
    std::snprintf(result.mutex.data(), result.mutex.size(), "Local\\G%.8s", result.id.data());
    return result;
}
}
