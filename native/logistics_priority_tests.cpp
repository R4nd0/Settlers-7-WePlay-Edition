// Execute the actual conditional patches in owned synthetic x86 memory.
// No game image, priority object or serialized world is loaded by this test.
#include "offline_bootstrap.cpp"
#include <cstdio>
void ReportLocalStatus(const char*) {}

#ifdef S7_LOCAL_NETWORK_PUMP
__declspec(naked) unsigned int __cdecl CompareAndBranch(const void*, unsigned int, unsigned int) {
    __asm {
        mov ecx, [esp + 4]
        mov eax, [esp + 8]
        cmp eax, [esp + 12]
        jmp ecx
    }
}
#endif

int main() {
#ifndef S7_LOCAL_NETWORK_PUMP
    std::puts("PASS logistics priority correction disabled without network pump");
    return 0;
#else
    int checks = 0, failures = 0;
    auto check = [&](bool value) { ++checks; if (!value) ++failures; };
    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1106000,
        MEM_RESERVE | MEM_COMMIT, PAGE_EXECUTE_READWRITE));
    if (!memory) return 2;
    image_base = reinterpret_cast<uintptr_t>(memory);
    for (bool steam : {false, true}) {
        steam_history = steam;
        for (uintptr_t site : {0x795929u, 0x79593au}) {
            const Patch* found = nullptr;
            for (const auto& patch : ConnectPatches) if (patch.va == site) found = &patch;
            check(found && found->raw_length == 2 && !found->pointer_slot);
            if (!found) continue;
            auto* at = PatchAddress(*found);
            const auto* original = OriginalBytes(*found);
            auto* old_target = at + 2 + original[1];
            auto* next = Native<unsigned char*>(0x795967);
            check(at + 2 + static_cast<const unsigned char*>(found->replacement)[1] == next);
            // Distinct return markers identify fallthrough, old null path and
            // the native next-entry path. Both targets retain ordinary RET.
            const unsigned char marker[] = {0xb8,0,0,0,0,0xc3};
            memcpy(at + 2, marker, sizeof(marker)); at[3] = 11;
            memcpy(old_target, marker, sizeof(marker)); old_target[1] = 22;
            memcpy(next, marker, sizeof(marker)); next[1] = 33;
            const unsigned int cases[][2] = {{0,0},{0,1},{1,0},{185,185},{185,184},{185,186},
                {0x1c00b9,0x1d00b9},{0x1c00b9,0x1c00b9},{UINT32_MAX,0},{0,UINT32_MAX}};
            for (bool corrected : {false, true}) {
                check(WritePatch(*found, !corrected));
                check(FlushInstructionCache(GetCurrentProcess(), memory, 0x1106000) != FALSE);
                for (const auto& values : cases) {
                    const bool rejected = site == 0x795929 ? values[0] >= values[1] : values[0] != values[1];
                    SetLastError(4321);
                    const auto result = CompareAndBranch(at, values[0], values[1]);
                    check(result == (rejected ? (corrected ? 33u : 22u) : 11u) && GetLastError() == 4321);
                }
            }
            check(WritePatch(*found, true));
            check(memcmp(at, original, 2) == 0);
        }
    }
    VirtualFree(memory, 0, MEM_RELEASE);
    std::printf("%s %d logistics priority branch checks\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
#endif
}
