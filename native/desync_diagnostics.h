#pragma once
#include <stdint.h>
#include <xmmintrin.h>

// Observation only: never normalize/restore a game's floating-point mode.
// Exception status flags are expected to change during arithmetic and are
// deliberately excluded. No floating-point values, IDs or payloads are read.
struct DesyncFpControl {
    uint16_t x87;
    uint32_t sse;
};
inline DesyncFpControl ReadDesyncFpControl() {
    unsigned short control = 0;
    __asm fnstcw control
    return {static_cast<uint16_t>(control & 0x1f3f), _mm_getcsr() & 0xffc0u};
}

enum class DesyncPhase : unsigned int { Begin, Master, End, Core, Echo, Manager, Owner, Count };
struct DesyncDiagnostics {
    static constexpr uint64_t DurationMs = 20u * 60u * 1000u;
    static constexpr unsigned int MaximumPumps = 120000;
    enum Event : unsigned int {
        Started = 1, Finished = 2, EntryX87Changed = 4, EntrySseChanged = 8,
        EntryNot53Bit = 16, PumpGap = 32
    };
    bool started = false;
    bool finished = false;
    uint64_t started_ms = 0;
    uint64_t previous_ms = 0;
    unsigned int pumps = 0;
    unsigned int reported = 0;
    DesyncFpControl baseline{};
    unsigned int changes[static_cast<unsigned int>(DesyncPhase::Count)]{};
    unsigned int observations[static_cast<unsigned int>(DesyncPhase::Count)]{};

    bool Begin(uint32_t owner, uint32_t caller, uint64_t now, DesyncFpControl entry,
               unsigned int& events) {
        events = 0;
        // Foreign Windows threads must not even read the non-atomic state.
        if (!owner || caller != owner) return false;
        if (finished) return false;
        if (!started) {
            started = true; started_ms = previous_ms = now; baseline = entry;
            events |= Started;
        }
        // Reversed monotonic time is also terminal; never underflow/re-arm.
        if (now < previous_ms || now - started_ms >= DurationMs || pumps >= MaximumPumps) {
            finished = true; events |= Finished; return false;
        }
        unsigned int current = 0;
        if (entry.x87 != baseline.x87) current |= EntryX87Changed;
        if (entry.sse != baseline.sse) current |= EntrySseChanged;
        // Native 4EE940/4EE990 and 53F2E0 select/check _PC_53, raw x87 PC=10b.
        if ((entry.x87 & 0x300) != 0x200) current |= EntryNot53Bit;
        if (pumps && now - previous_ms >= 1000) current |= PumpGap;
        events |= current & ~reported; reported |= current;
        previous_ms = now; ++pumps;
        return true;
    }
    unsigned int Observe(DesyncPhase phase, DesyncFpControl before, DesyncFpControl after) {
        const auto index = static_cast<unsigned int>(phase);
        if (!started || finished || index >= static_cast<unsigned int>(DesyncPhase::Count)) return 0;
        ++observations[index];
        const unsigned int difference = (before.x87 != after.x87 ? 1u : 0u) |
            (before.sse != after.sse ? 2u : 0u);
        const auto fresh = difference & ~changes[index];
        changes[index] |= difference;
        return fresh;
    }
};
