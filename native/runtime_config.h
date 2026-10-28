#pragma once
#include "lan_membership.h"
#include <map>
// Kept separate from file I/O so malformed launcher records have native tests.
inline bool ParseLocalRuntimeConfiguration(const char* bytes, size_t length, std::map<std::string, std::string>& fields) {
    fields.clear();
    if (!bytes || !length || length >= 2048) return false;
    const std::string text(bytes, length);
    size_t start = 0;
    while (start < text.size()) {
        size_t end = text.find('\n', start);
        if (end == std::string::npos) return false;
        std::string line = text.substr(start, end - start); start = end + 1;
        if (!line.empty() && line.back() == '\r') line.pop_back();
        size_t equals = line.find('=');
        if (equals == std::string::npos || !equals || equals + 1 == line.size()) return false;
        const std::string key = line.substr(0, equals), value = line.substr(equals + 1);
        for (unsigned char c : key) if (c < 32 || c > 126) return false;
        if (key == "display_name") {
            size_t count = 0;
            if (!ValidLocalDisplayName(value.c_str(), count) || count != value.size()) return false;
        } else for (unsigned char c : value) if (c < 32 || c > 126) return false;
        if (!fields.emplace(key, value).second) return false;
    }
    const char* required[] = {"format", "address", "udp_port", "public_key", "profile_id", "display_name", "ticket"};
    for (const char* key : required) if (!fields.count(key)) return false;
    if (fields.at("format") != "s7-local-runtime-v1") return false;
    if (fields.count("connect_probe") && fields.at("connect_probe") != "0" && fields.at("connect_probe") != "1") return false;
    const bool group = fields.count("lan_group") != 0;
    const bool local = fields.count("lan_local") != 0, peer = fields.count("lan_peer") != 0;
    if (group) {
        uint32_t local_ip = 0, server_ip = 0;
        if (fields.at("lan_group") != "1" || !local || peer
            || !ParsePrivateLanAddress(fields.at("lan_local").c_str(), local_ip)
            || (fields.at("address") != "127.0.0.1" && !ParsePrivateLanAddress(fields.at("address").c_str(), server_ip))) return false;
    } else if (local || peer) {
        if (!local || !peer || !ValidLanPair(fields.at("lan_local").c_str(), fields.at("lan_peer").c_str())
            || (fields.at("address") != fields.at("lan_local") && fields.at("address") != fields.at("lan_peer"))) return false;
    } else if (fields.at("address") != "127.0.0.1") return false;
    if (local && (!fields.count("connect_probe") || fields.at("connect_probe") != "1")) return false;
    return fields.size() == 7 + fields.count("connect_probe") + (group || local ? 2 : 0);
}
// Accept fixed developer-authored labels only; never pass runtime arguments.
void ReportLocalStatus(const char* fixed_label);
