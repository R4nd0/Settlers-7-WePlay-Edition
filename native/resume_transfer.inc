// Guarded multiplayer checkpoint transfer; no simulation or checksum changes.
#include "resume_pacing.h"
struct ResumeFragment {
    void* manager;
    uint32_t source, checksum, total, target, mode, reliable, extra;
    std::vector<unsigned char> bytes;
};
std::deque<ResumeFragment> resume_fragments;
size_t resume_fragment_bytes = 0;
#ifdef S7_RESUME_CATCHUP
constexpr bool ResumeCatchupEnabled = true;
#else
constexpr bool ResumeCatchupEnabled = false;
#endif
ResumeTransferPacer resume_pacer(ResumeCatchupEnabled);
bool resume_readiness_held = false;
// A peer's Ready click cannot make an owned, still-queued world transferable.
// Restrict the existing Lua readiness getter until the real sender drains;
// never set the native ready bit, acknowledge data or alter a world checksum.
int __cdecl ReadResumeReadyState(void* state) {
    const int result = Native<int (__cdecl*)(void*)>(0x84BEB0)(state);
    const DWORD incoming = GetLastError();
    if (result == 1 && initialization == 2 && local_connect_probe
        && GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)
        && !resume_fragments.empty()) {
        const double slot = Native<double (__cdecl*)(void*, int)>(0x452C90)(state, 1);
        if (slot >= 1 && slot <= 4 && slot == static_cast<uint32_t>(slot)) {
            // Lua collects the top result and discards the rest of this C frame.
            Native<void (__cdecl*)(void*, int)>(0x44C6E0)(state, 0);
            if (!resume_readiness_held) {
                resume_readiness_held = true;
                ReportLocalStatus("local-mp-resume-start-held-for-transfer");
            }
        }
    }
    SetLastError(incoming);
    return result;
}
// A native create/join request begins a new ownership interval even when the
// engine reuses its manager allocation. Never carry owned fragments across it.
void CancelResumeFragments() {
    const DWORD incoming = GetLastError();
    if (GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)) {
        const bool pending = !resume_fragments.empty();
        resume_fragments.clear(); resume_fragment_bytes = 0; resume_pacer.Reset();
        resume_readiness_held = false;
        if (pending) ReportLocalStatus("local-mp-resume-transfer-cancelled");
    }
    SetLastError(incoming);
}
using ResumeSend = void (__thiscall*)(void*, const void*, uint32_t, uint32_t, uint32_t, uint32_t);
void __fastcall QueueResumeFragment(void* self, void*, const void* value,
    uint32_t mode, uint32_t reliable, uint32_t extra, uint32_t target) {
    const DWORD incoming = GetLastError();
    auto* event = static_cast<const unsigned char*>(value);
    if (initialization != 2 || !local_connect_probe
        || GetCurrentThreadId() != bootstrap_thread.load(std::memory_order_acquire)
        || !self || !event || *reinterpret_cast<const uintptr_t*>(event) != NativeVA(0x114DE08)
        || *static_cast<uintptr_t*>(self) != NativeVA(0x11404F0)) {
        SetLastError(incoming);
        Native<ResumeSend>(0xEDB300)(self, value, mode, reliable, extra, target);
        return;
    }
    const uint32_t length = *reinterpret_cast<const uint32_t*>(event + 0x18);
    const auto* data = *reinterpret_cast<const unsigned char* const*>(event + 0x10);
    if (length > 1024 || (length && !data) || resume_fragments.size() >= 65536
        || length > 64 * 1024 * 1024 - resume_fragment_bytes) Stop(NativeGameObjectMismatch);
    ResumeFragment fragment{self,
        *reinterpret_cast<const uint32_t*>(event + 8),
        *reinterpret_cast<const uint32_t*>(event + 0x14),
        *reinterpret_cast<const uint32_t*>(event + 0x1c), target, mode, reliable, extra, {}};
    if (length) fragment.bytes.assign(data, data + length);
    if (resume_fragments.empty() && (ResumeCatchupEnabled || !resume_pacer.Armed()))
        resume_pacer.Arm(GetTickCount64());
    resume_fragments.push_back(std::move(fragment));
    resume_fragment_bytes += length;
    SetLastError(incoming);
}
void DrainResumeFragmentsAt(ULONGLONG now) {
    if (resume_fragments.empty() || initialization != 2 || !local_connect_probe
        || GetCurrentThreadId() != bootstrap_thread.load(std::memory_order_acquire)) return;
    const DWORD incoming = GetLastError();
    auto* main = *Native<unsigned char**>(0x13A5210);
    void* manager = main && *reinterpret_cast<uintptr_t*>(main) == NativeVA(0x113F2C0)
        ? *reinterpret_cast<void**>(main + 0x10) : nullptr;
    if (!manager || manager != resume_fragments.front().manager) {
        resume_fragments.clear(); resume_fragment_bytes = 0; resume_pacer.Reset();
        resume_readiness_held = false;
        SetLastError(incoming); return;
    }
    const unsigned int budget = resume_pacer.Take(now);
    if (!budget) { SetLastError(incoming); return; }
    using Construct = void* (__thiscall*)(void*, uint32_t, uint32_t, const void*, uint32_t, uint32_t, uint32_t);
    using Destroy = void (__thiscall*)(void*);
    for (unsigned int count = 0; count < budget && !resume_fragments.empty(); ++count) {
        const auto& fragment = resume_fragments.front();
        if (fragment.manager != manager) Stop(NativeGameObjectMismatch);
        alignas(4) unsigned char event[0x20]{};
        Native<Construct>(0x84A1D0)(event, 0, fragment.source, fragment.bytes.data(),
            static_cast<uint32_t>(fragment.bytes.size()), fragment.checksum, fragment.total);
        Native<ResumeSend>(0xEDB300)(manager, event, fragment.mode, fragment.reliable, fragment.extra, fragment.target);
        Native<Destroy>(0x84A270)(event);
        resume_fragment_bytes -= fragment.bytes.size();
        resume_fragments.pop_front();
    }
    if (resume_fragments.empty()) {
        if (ResumeCatchupEnabled) resume_pacer.Reset();
        resume_readiness_held = false;
        ReportLocalStatus("local-mp-resume-send-queue-drained");
    }
    SetLastError(incoming);
}
void DrainResumeFragments() {
    const DWORD incoming = GetLastError();
    const auto now = GetTickCount64();
    SetLastError(incoming);
    DrainResumeFragmentsAt(now);
}
void __fastcall DeserializeResumeEvent(void* self, void*, const void* data, uint32_t length) {
    const DWORD incoming = GetLastError();
    if (GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)
        && initialization == 2 && local_connect_probe) {
        static std::atomic<unsigned int> reported{0};
        const unsigned int bit = !length ? 1u : !data ? 2u : length < 4 ? 4u : 8u;
        if (!(reported.fetch_or(bit) & bit)) {
            ReportLocalStatus(!length ? "local-mp-resume-event-empty"
                : !data ? "local-mp-resume-event-null-data"
                : length < 4 ? "local-mp-resume-event-short" : "local-mp-resume-event-has-header");
        }
    }
    SetLastError(incoming);
    if (!length) {
        static std::atomic<bool> reported{false};
        if (!reported.exchange(true)) ReportLocalStatus("local-mp-resume-empty-event-rejected");
        SetLastError(incoming);
        return;
    }
    Native<void (__thiscall*)(void*, const void*, uint32_t)>(0xEC9FD0)(self, data, length);
}
