#pragma once
#include <stdint.h>

// Transport-only ordering. Native scheduler ranks are retained for deliberate
// priority changes, while ordinary reservations use their request's birth turn.
namespace neo_fifo {
enum class Intent : int8_t { Defer = -1, Ordinary = 0, Prefer = 1 };
struct Key {
    Intent intent = Intent::Ordinary;
    int32_t native_rank = 0;
    int32_t created = 0;
    uint32_t request = 0;
    uint32_t reservation = 0;
    bool grouped = false;
    int32_t group_order = 0;
};
inline bool Before(const Key& a, const Key& b) {
    const int band_a=a.grouped || a.intent==Intent::Prefer?0:a.intent==Intent::Defer?2:1;
    const int band_b=b.grouped || b.intent==Intent::Prefer?0:b.intent==Intent::Defer?2:1;
    if(band_a!=band_b)return band_a<band_b;
    if(band_a==0 && a.group_order!=b.group_order)return a.group_order<b.group_order;
    if(a.intent!=b.intent)return static_cast<int>(a.intent)>static_cast<int>(b.intent);
    if (a.intent != Intent::Ordinary && a.native_rank != b.native_rank)
        return a.native_rank < b.native_rank;
    if (a.created != b.created) return a.created < b.created;
    // Equal birth turns remain equivalent: the native stable list sort keeps
    // their existing arrival order, including after a reservation is rebooked.
    return false;
}
// An unchanged order has no explicit preference. A deliberate permutation is
// honored as a whole, so moving several groups cannot reverse its relations.
inline int GroupOrder(unsigned group, const uint32_t (&order)[8], bool deliberate) {
    if (!deliberate || group >= 8) return -1;
    unsigned mask = 0, position = 0;bool changed=false,tail_changed=false;
    for (unsigned i = 0; i < 8; ++i) {
        if (order[i] >= 8 || (mask & (1u << order[i]))) return -1;
        mask |= 1u << order[i];
        if (order[i] == group) position = i;
        if(order[i]!=i){changed=true;if(i>=4)tail_changed=true;}
    }
    if(!changed || (!tail_changed && group>=4))return -1;
    return static_cast<int>(position);
}
struct Mark {
    uint32_t id = 0, created = 0;
    uintptr_t object = 0;
    Intent intent = Intent::Ordinary;
    bool Matches(uint32_t candidate, uint32_t birth, uintptr_t address) const {
        return id == candidate && created == birth && object == address;
    }
};
}
