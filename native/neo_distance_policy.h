#pragma once
#include <cmath>
#include <stdint.h>

// This orders eligible handoffs of ONE shipment, never transport requests.
namespace neo_distance {
struct Route {
    float first = 0, remaining = 0;
    uint32_t store = 0;
    bool Valid() const {
        return store && std::isfinite(first) && std::isfinite(remaining)
            && first >= 0 && remaining >= 0;
    }
    double Total() const { return double(first) + double(remaining); }
};
inline bool Before(const Route& candidate, const Route& best) {
    if (!candidate.Valid()) return false;
    if (!best.Valid()) return true;
    if (candidate.Total() != best.Total()) return candidate.Total() < best.Total();
    // A full entity handle is shared by all peers; heap addresses are not.
    return candidate.store < best.store;
}
}
