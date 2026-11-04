#include "resume_pacing.h"
#include <cstdio>
#include <cstdlib>
#include <initializer_list>

namespace {
unsigned int checks = 0;
void Check(bool okay) {
    ++checks;
    if (!okay) { std::fprintf(stderr, "Resume pacing check %u failed\n", checks); std::exit(1); }
}
uint64_t TransferTime(unsigned int cadence, bool catchup) {
    constexpr unsigned int Fragments = 21 * 1024; // Synthetic 21-MiB world, not a game save.
    unsigned int sent = 0;
    uint64_t next = 0;
    ResumeTransferPacer pacer; pacer.Arm(0);
    for (uint64_t now = 0; now < 10000000; now += cadence) {
        unsigned int budget = 0;
        if (catchup) budget = pacer.Take(now);
        else if (now >= next) { budget = 8; next = now + 20; }
        Check(budget <= ResumeTransferPacer::MaximumBatch);
        sent += budget;
        if (sent >= Fragments) return now;
    }
    std::exit(2);
}
}
int main() {
    ResumeTransferPacer baseline(false);
    baseline.Arm(1000);
    Check(baseline.Take(1000) == 8 && baseline.Next() == 1020);
    Check(baseline.Take(1063) == 8 && baseline.Next() == 1083);
    Check(baseline.Take(1063) == 0 && baseline.Take(1082) == 0);
    Check(baseline.Take(100000) == 8 && baseline.Next() == 100020);
    ResumeTransferPacer pacer;
    Check(!pacer.Armed() && pacer.Take(5000) == 0);
    pacer.Arm(1000);
    Check(pacer.Take(1000) == 8 && pacer.Next() == 1020);
    Check(pacer.Take(1019) == 0);
    Check(pacer.Take(1063) == 24 && pacer.Next() == 1080);
    Check(pacer.Take(1063) == 0 && pacer.Take(1079) == 0);
    Check(pacer.Take(1080) == 8 && pacer.Next() == 1100);
    Check(pacer.Take(100000) == 32 && pacer.Next() == 100020);
    Check(pacer.Take(100000) == 0);
    pacer.Reset(); Check(!pacer.Armed() && !pacer.Next() && pacer.Take(200000) == 0);
    pacer.Arm(200000); Check(pacer.Take(200000) == 8);
    for (unsigned int cadence : {1u, 16u, 20u, 33u, 50u, 63u, 80u, 125u, 1000u}) {
        const uint64_t old_time = TransferTime(cadence, false);
        const uint64_t new_time = TransferTime(cadence, true);
        Check(new_time <= old_time);
        if (cadence == 20) Check(new_time == old_time);
        if (cadence == 63) Check(new_time * 3 < old_time);
        std::printf("Synthetic 21-MiB transfer, %u-ms menu updates: old=%llu ms, new=%llu ms\n",
            cadence, static_cast<unsigned long long>(old_time), static_cast<unsigned long long>(new_time));
    }
    std::printf("PASS: %u resume pacing checks; no game or network used.\n", checks);
}
