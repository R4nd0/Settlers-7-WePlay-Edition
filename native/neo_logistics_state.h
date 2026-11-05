#pragma once
#include "neo_fifo_policy.h"
#include <string>
#include <vector>
#include <limits>

namespace neo_logistics {
constexpr size_t MaximumStateBytes = 2 * 1024 * 1024;
struct SavedMark { uint32_t id; int32_t created; neo_fifo::Intent intent; };
struct SavedState {
    bool enabled = false;
    uint32_t turn = 0;
    std::vector<SavedMark> marks;
};
inline std::string Encode(const SavedState& state) {
    if (!state.enabled) return "NEO0\n";
    if (state.marks.size() > 65536) return {};
    std::string result = "NEO1\n" + std::to_string(state.turn) + "\n" + std::to_string(state.marks.size()) + "\n";
    uint32_t previous = 0;
    bool slots[65536]{};
    for (const auto& mark : state.marks) {
        if (!mark.id || mark.id <= previous || mark.created < -1
            || (mark.created>=0 && static_cast<uint32_t>(mark.created)>state.turn) || slots[mark.id&65535u]
            || (mark.intent != neo_fifo::Intent::Prefer && mark.intent != neo_fifo::Intent::Defer)) return {};
        previous = mark.id;slots[mark.id&65535u]=true;
        result += std::to_string(mark.id) + "," + std::to_string(mark.created) + ","
            + (mark.intent == neo_fifo::Intent::Prefer ? "1\n" : "-1\n");
        if (result.size() > MaximumStateBytes) return {};
    }
    return result;
}
inline bool Number(const std::string& text, size_t& pos, char delimiter, int64_t minimum, int64_t maximum, int64_t& result) {
    const size_t begin = pos;
    bool negative = pos < text.size() && text[pos] == '-';
    if (negative) ++pos;
    if (pos >= text.size() || text[pos] < '0' || text[pos] > '9') return false;
    const size_t digits = pos;
    uint64_t value = 0;
    while (pos < text.size() && text[pos] >= '0' && text[pos] <= '9') {
        value = value * 10 + static_cast<unsigned>(text[pos++] - '0');
        if (pos - digits > 10 || value > UINT32_MAX) return false;
    }
    if ((pos - digits > 1 && text[digits] == '0') || (negative && value == 0)
        || pos == begin || pos >= text.size() || text[pos++] != delimiter) return false;
    result = negative ? -static_cast<int64_t>(value) : static_cast<int64_t>(value);
    return result >= minimum && result <= maximum;
}
inline bool Decode(const std::string& text, SavedState& output) {
    output = {};
    if (text == "NEO0\n") return true;
    if (text.size() > MaximumStateBytes || text.compare(0,5,"NEO1\n")) return false;
    SavedState candidate; candidate.enabled = true;
    size_t pos = 5; int64_t turn = 0, count = 0;
    if (!Number(text,pos,'\n',0,UINT32_MAX,turn) || !Number(text,pos,'\n',0,65536,count)) return false;
    candidate.turn = static_cast<uint32_t>(turn);
    uint32_t previous = 0;
    bool slots[65536]{};
    for (int64_t i=0;i<count;++i) {
        int64_t id=0,created=0,intent=0;
        if (!Number(text,pos,',',1,UINT32_MAX,id) || !Number(text,pos,',',-1,INT32_MAX,created)
            || !Number(text,pos,'\n',-1,1,intent) || !intent || id <= previous || slots[id & 65535]) return false;
        if (created >= 0 && static_cast<uint64_t>(created) > candidate.turn) return false;
        previous = static_cast<uint32_t>(id); slots[id & 65535] = true;
        candidate.marks.push_back({static_cast<uint32_t>(id),static_cast<int32_t>(created),static_cast<neo_fifo::Intent>(intent)});
    }
    if (pos != text.size()) return false;
    output = std::move(candidate);
    return true;
}
}
