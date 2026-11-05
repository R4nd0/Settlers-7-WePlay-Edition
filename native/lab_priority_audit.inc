// Laboratory observer only; bounded console records, no simulation state writes.
uint32_t lab_priority_sequence = 0;
uint32_t LabPriorityWord(uintptr_t p) { return *reinterpret_cast<const uint32_t*>(p); }
uintptr_t LabPriorityResolve(uintptr_t self, uint32_t id) {
    const auto b = LabPriorityWord(self + 0x1134), e = LabPriorityWord(self + 0x1138);
    const auto i = id & 0xffffu;
    if (b < 0x10000 || e < b || e - b > 0x80000 || (e-b)%8 || i >= (e-b)/8) return 0;
    return LabPriorityWord(b + i*8) == id ? LabPriorityWord(b + i*8 + 4) : 0;
}
void __cdecl ObserveLabPriorityEntry(const uint32_t* frame, uint32_t kind) {
    const DWORD incoming = GetLastError();
    if (!steam_history && initialization == 2 && local_connect_probe
        && GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)) {
#ifdef S7_NEO_FIFO_LAB
        TrackNeoFifoIntent(frame,kind);
#endif
        ++lab_priority_sequence;
        __try {
            const auto self = frame[6], caller = frame[9], first = frame[10], second = frame[11];
            const auto replay = LabPriorityWord(NativeVA(0x13a5200));
            if (LabPriorityWord(self) == NativeVA(0x1148328) && replay >= 0x10000
                && LabPriorityWord(replay) == NativeVA(0x1133f40)
                && lab_lua_console_count < _countof(lab_lua_console_records)) {
                char line[512]{}; int n = 0;
                const auto turn = LabPriorityWord(replay + 0x2c);
                if (!kind) {
                    const auto task = LabPriorityResolve(self, first);
                    const auto rel = second ? LabPriorityResolve(self, second) : 0;
                    n = std::snprintf(line, sizeof(line), "NEO1|priority-task|%u|%u|%08x|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u|%u",
                        turn, lab_priority_sequence, caller - static_cast<uint32_t>(image_base) + 0x400000u,
                        first, second, task ? *reinterpret_cast<const unsigned char*>(task+0x34) : 255u,
                        task ? LabPriorityWord(task+0x2c) : 0u, task ? LabPriorityWord(task+0x1c) : 255u,
                        task ? LabPriorityWord(task+0xc) : 0u, task ? LabPriorityWord(task+0x14) : 0u,
                        task ? LabPriorityWord(task+0x24) : 0u,
                        rel ? LabPriorityWord(rel+0x2c) : 0u, rel ? LabPriorityWord(rel+0x1c) : 255u,
                        rel ? LabPriorityWord(rel+0x14) : 0u);
                } else {
                    const uint32_t player = second & 255u; // native function consumes BL
                    uint32_t values[16]{}; bool valid = player < 25;
                    const uintptr_t vectors[] = { first, self + 8 + player*0xacu };
                    for (unsigned j=0; j<2 && valid; ++j) {
                        const auto b = LabPriorityWord(vectors[j]), e = LabPriorityWord(vectors[j]+4);
                        valid = b >= 0x10000 && e >= b && e-b == 32;
                        if (valid) memcpy(values+j*8, reinterpret_cast<const void*>(b), 32);
                    }
                    n = std::snprintf(line, sizeof(line), "NEO1|priority-group|%u|%u|%08x|%u|%u|%u,%u,%u,%u,%u,%u,%u,%u|%u,%u,%u,%u,%u,%u,%u,%u",
                        turn, lab_priority_sequence, caller - static_cast<uint32_t>(image_base) + 0x400000u, player, valid ? 1u : 0u,
                        values[0],values[1],values[2],values[3],values[4],values[5],values[6],values[7],
                        values[8],values[9],values[10],values[11],values[12],values[13],values[14],values[15]);
                }
                if (n > 0 && static_cast<size_t>(n) < sizeof(line)) {
                    auto& out = lab_lua_console_records[lab_lua_console_count++];
                    memcpy(out.bytes, line, static_cast<size_t>(n)); out.length = static_cast<DWORD>(n);
                }
            }
        } __except(EXCEPTION_EXECUTE_HANDLER) { /* Never alter native error handling. */ }
    }
    SetLastError(incoming);
}
#include "lab_priority_bridge.inc"
