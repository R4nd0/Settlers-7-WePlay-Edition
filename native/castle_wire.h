#pragma once
#include <stdint.h>
#include <string>
#include <vector>
#include <algorithm>
#include <utility>

// Versioned cosmetic data only: no process addresses, profile names or gameplay state.
namespace castle_wire {
constexpr size_t Limit = 2048, MaxParts = 512, MaxAsset = 128;
struct Part { std::string asset; uint32_t parent = 0, slot = 0; };
struct Design { std::string type; std::vector<Part> parts; };
inline bool Asset(const std::string& text) {
    if (text.empty() || text.size() > MaxAsset || text.find("..") != std::string::npos) return false;
    for (unsigned char c : text)
        if (!(c >= 'a' && c <= 'z') && !(c >= 'A' && c <= 'Z') && !(c >= '0' && c <= '9')
            && c != '_' && c != '-' && c != '.') return false;
    return true;
}
inline void Put(std::vector<unsigned char>& out, uint32_t n) {
    for (unsigned int i = 0; i != 4; ++i) out.push_back(static_cast<unsigned char>(n >> (i * 8)));
}
inline bool Valid(const Design& design) {
    if (design.parts.empty()) return design.type.empty();
    if (!Asset(design.type) || design.parts.size() > MaxParts) return false;
    for (size_t i = 0; i != design.parts.size(); ++i) {
        const auto& p = design.parts[i];
        // Parents precede children; UINT_MAX is the native root marker.
        if (!Asset(p.asset) || (p.parent != UINT32_MAX && p.parent >= i) || p.slot > 1024) return false;
    }
    return true;
}
inline bool Encode(const Design& design, std::vector<unsigned char>& out) {
    out.clear();
    if (!Valid(design)) return false;
    std::vector<std::string> assets;
    for (const auto& p : design.parts)
        if (std::find(assets.begin(), assets.end(), p.asset) == assets.end()) assets.push_back(p.asset);
    Put(out, 0x32433753); Put(out, static_cast<uint32_t>(design.parts.size()));
    auto text = [&](const std::string& s) { Put(out, static_cast<uint32_t>(s.size())); out.insert(out.end(), s.begin(), s.end()); };
    text(design.type);
    Put(out, static_cast<uint32_t>(assets.size()));
    for (const auto& asset : assets) text(asset);
    auto small = [&](uint32_t n) { out.push_back(static_cast<unsigned char>(n)); out.push_back(static_cast<unsigned char>(n >> 8)); };
    for (const auto& p : design.parts) {
        small(static_cast<uint32_t>(std::find(assets.begin(), assets.end(), p.asset) - assets.begin()));
        small(p.parent == UINT32_MAX ? 0xffff : p.parent); small(p.slot);
    }
    if (out.size() > Limit) { out.clear(); return false; }
    return true;
}
inline bool Decode(const std::vector<unsigned char>& in, Design& result) {
    result = {}; Design value; size_t at = 0;
    if (in.size() > Limit) return false;
    auto word = [&](uint32_t& n) {
        if (at > in.size() || in.size() - at < 4) return false;
        n = 0; for (unsigned int i = 0; i != 4; ++i) n |= static_cast<uint32_t>(in[at++]) << (i * 8);
        return true;
    };
    auto text = [&](std::string& s) {
        uint32_t n = 0; if (!word(n) || n > MaxAsset || n > in.size() - at) return false;
        s.assign(reinterpret_cast<const char*>(in.data() + at), n); at += n; return true;
    };
    uint32_t magic = 0, count = 0;
    if (!word(magic) || magic != 0x32433753 || !word(count) || count > MaxParts || !text(value.type)) return false;
    uint32_t asset_count = 0;
    if (!word(asset_count) || asset_count > count) return false;
    std::vector<std::string> assets(asset_count);
    for (auto& asset : assets) if (!text(asset) || !Asset(asset)) return false;
    value.parts.resize(count);
    auto small = [&](uint32_t& n) {
        if (in.size() - at < 2) return false;
        n = in[at] | (static_cast<uint32_t>(in[at + 1]) << 8); at += 2; return true;
    };
    for (auto& p : value.parts) {
        uint32_t index = 0;
        if (!small(index) || index >= assets.size() || !small(p.parent) || !small(p.slot)) return false;
        p.asset = assets[index]; if (p.parent == 0xffff) p.parent = UINT32_MAX;
    }
    if (at != in.size() || !Valid(value)) return false;
    result = std::move(value); return true;
}
}
