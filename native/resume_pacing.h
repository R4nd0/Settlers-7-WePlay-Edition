#pragma once
#include <cstdint>

// Keep the existing nominal rate when menu updates arrive less often than the
// send interval. Credit is bounded: a stalled frame never releases a whole save.
class ResumeTransferPacer {
public:
    static constexpr uint64_t IntervalMs = 20;
    static constexpr unsigned int FragmentsPerInterval = 8;
    static constexpr unsigned int MaximumIntervals = 4;
    static constexpr unsigned int MaximumBatch = FragmentsPerInterval * MaximumIntervals;

    explicit ResumeTransferPacer(bool catchup = true) : catchup_(catchup) {}

    void Arm(uint64_t now) { next_ = now; armed_ = true; }
    void Reset() { next_ = 0; armed_ = false; }
    uint64_t Next() const { return next_; }
    bool Armed() const { return armed_; }

    unsigned int Take(uint64_t now) {
        if (!armed_ || now < next_) return 0;
        if (!catchup_) { next_ = now + IntervalMs; return FragmentsPerInterval; }
        const uint64_t elapsed = 1 + (now - next_) / IntervalMs;
        const unsigned int intervals = elapsed > MaximumIntervals
            ? MaximumIntervals : static_cast<unsigned int>(elapsed);
        // Discard credit beyond the cap instead of allowing repeated calls in
        // one frame to spend an arbitrarily old backlog.
        next_ = elapsed > MaximumIntervals ? now + IntervalMs : next_ + elapsed * IntervalMs;
        return intervals * FragmentsPerInterval;
    }
private:
    uint64_t next_ = 0;
    bool armed_ = false;
    bool catchup_;
};
