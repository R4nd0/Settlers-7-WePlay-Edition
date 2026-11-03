// Private laboratory observation of original build-strategy callbacks. The
// naked shims restore all machine state and tail-call the unchanged callbacks.
struct LabResumeAiRecord {
    uint32_t turn, callback, event, player, building, state, done, remaining;
    uint32_t candidates, candidate_hash, last[5], tasks, chosen[5];
};
LabResumeAiRecord lab_resume_ai_records[16384]{};
size_t lab_resume_ai_count = 0;
bool lab_resume_ai_written = false;
ULONGLONG lab_resume_ai_finish_after = 0;
uintptr_t lab_resume_ai_original[6]{};
#ifdef S7_RNG_TRACE_TESTING
bool lab_resume_ai_test_clobber = false;
#endif
void __cdecl ObserveLabResumeAi(unsigned char* strategy, uint32_t event, uint32_t callback) {
    const DWORD incoming = GetLastError();
#ifdef S7_RNG_TRACE_TESTING
    if (lab_resume_ai_test_clobber) {
        __asm { pxor xmm0, xmm0 }
        __asm { fld1 }
    }
#endif
    if (!steam_history && initialization == 2 && local_connect_probe && event <= 2 && callback < 7
        && GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)
        && strategy && *reinterpret_cast<uintptr_t*>(strategy) == NativeVA(callback < 3 ? 0x1159874 : 0x1159f00)) {
        auto* replay = *Native<unsigned char**>(0x13A5200);
        if (replay && *reinterpret_cast<uintptr_t*>(replay) == NativeVA(0x1133F40)) {
            const uint32_t turn = *reinterpret_cast<uint32_t*>(replay + 0x2c);
            if (turn >= 2000 && turn <= 2300 && lab_resume_ai_count < _countof(lab_resume_ai_records)) {
                const bool workshop = callback >= 3;
                const auto begin = *reinterpret_cast<uintptr_t*>(strategy + (workshop ? 0xec : 0x84));
                const auto end = *reinterpret_cast<uintptr_t*>(strategy + (workshop ? 0xf0 : 0x88));
                // Original mapped vector is 20-byte candidate records. Never
                // persist heap addresses or follow an unbounded container.
                if (end >= begin && (end - begin) % 20 == 0 && (end - begin) / 20 <= 256) {
                    auto& row = lab_resume_ai_records[lab_resume_ai_count++];
                    row.turn = turn; row.callback = callback; row.event = event;
                    row.player = strategy[0x10];
                    row.building = workshop ? 12 : *reinterpret_cast<uint32_t*>(strategy + 0x60);
                    row.state = *reinterpret_cast<uint32_t*>(strategy + 0x5c);
                    row.done = strategy[workshop ? 0x98 : 0x78];
                    row.remaining = *reinterpret_cast<uint32_t*>(strategy + (workshop ? 0xa4 : 0x74));
                    row.tasks = *reinterpret_cast<uint32_t*>(strategy + 8);
                    row.candidates = static_cast<uint32_t>((end - begin) / 20);
                    uint32_t hash = 2166136261u;
                    for (uintptr_t at = begin; at < end; ++at) hash = (hash ^ *reinterpret_cast<unsigned char*>(at)) * 16777619u;
                    row.candidate_hash = hash;
                    if (row.candidates) memcpy(row.last, reinterpret_cast<const void*>(end - 20), sizeof(row.last));
                    if (workshop) memcpy(row.chosen, strategy + 0xa8, sizeof(row.chosen));
                    lab_resume_ai_finish_after = GetTickCount64() + 2000;
                }
            }
        }
    }
    SetLastError(incoming);
}
__declspec(naked) void ObserveLabResumeAiSearch() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push 0
        push dword ptr [edi + 40]
        push dword ptr [edi + 24]
        call ObserveLabResumeAi
        add esp, 12
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        jmp dword ptr [lab_resume_ai_original]
    }
}
__declspec(naked) void ObserveLabResumeAiBuild() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push 1
        push dword ptr [edi + 40]
        push dword ptr [edi + 24]
        call ObserveLabResumeAi
        add esp, 12
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        jmp dword ptr [lab_resume_ai_original + 4]
    }
}
__declspec(naked) void ObserveLabResumeAiPlacement() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push 2
        push dword ptr [edi + 40]
        push dword ptr [edi + 24]
        call ObserveLabResumeAi
        add esp, 12
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        jmp dword ptr [lab_resume_ai_original + 8]
    }
}
__declspec(naked) void ObserveLabResumeWorkshopSearch() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push 3
        push dword ptr [edi + 40]
        push dword ptr [edi + 24]
        call ObserveLabResumeAi
        add esp, 12
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        jmp dword ptr [lab_resume_ai_original + 12]
    }
}
__declspec(naked) void ObserveLabResumeWorkshopBuild() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push 4
        push dword ptr [edi + 40]
        push dword ptr [edi + 24]
        call ObserveLabResumeAi
        add esp, 12
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        jmp dword ptr [lab_resume_ai_original + 16]
    }
}
__declspec(naked) void ObserveLabResumeWorkshopPlacement() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push 5
        push dword ptr [edi + 40]
        push dword ptr [edi + 24]
        call ObserveLabResumeAi
        add esp, 12
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        jmp dword ptr [lab_resume_ai_original + 20]
    }
}
bool __fastcall ObserveLabResumeWorkshopSite(unsigned char* strategy, void*, const void* point) {
    const bool result = Native<bool (__thiscall*)(void*, const void*)>(0x9BB4D0)(strategy, point);
    alignas(16) unsigned char fp[512];
    _fxsave(fp);
    ObserveLabResumeAi(strategy, result ? 1 : 0, 6);
    _fxrstor(fp);
    return result;
}
void FlushLabResumeAiTrace() {
    if (lab_resume_ai_written || !lab_resume_ai_count || steam_history) return;
    const DWORD incoming = GetLastError();
    auto* replay = *Native<unsigned char**>(0x13A5200);
    if (!replay || *reinterpret_cast<uintptr_t*>(replay) != NativeVA(0x1133F40)
        || *reinterpret_cast<uint32_t*>(replay + 0x2c) < 2257) { SetLastError(incoming); return; }
    if (GetTickCount64() < lab_resume_ai_finish_after) { SetLastError(incoming); return; }
    HMODULE module = nullptr; wchar_t path[MAX_PATH]{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ObserveLabResumeAi), &module)) {
        const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
        const std::wstring suffix = L"\\Base\\Bin\\uplay_r1_loader.dll";
        if (length && length < MAX_PATH && length > suffix.size()
            && _wcsicmp(path + length - suffix.size(), suffix.c_str()) == 0) {
            const auto output = std::wstring(path, length - suffix.size()) + L"\\LocalData\\lab-ai-build.private.csv";
            HANDLE file = CreateFileW(output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                bool okay = true;
                for (size_t i = 0; okay && i < lab_resume_ai_count; ++i) {
                    const auto& v = lab_resume_ai_records[i]; char line[256]{};
                    const int size = _snprintf_s(line, sizeof(line), _TRUNCATE,
                        "%u,%u,%u,%u,%u,%u,%u,%u,%u,%x,%x,%x,%x,%x,%x,%u,%x,%x,%x,%x,%x\r\n",
                        v.turn,v.callback,v.event,v.player,v.building,v.state,v.done,v.remaining,
                        v.candidates,v.candidate_hash,v.last[0],v.last[1],v.last[2],v.last[3],v.last[4],v.tasks,
                        v.chosen[0],v.chosen[1],v.chosen[2],v.chosen[3],v.chosen[4]);
                    DWORD written = 0;
                    okay = size > 0 && WriteFile(file, line, static_cast<DWORD>(size), &written, nullptr)
                        && written == static_cast<DWORD>(size);
                }
                CloseHandle(file); lab_resume_ai_written = true;
                ReportLocalStatus(okay ? "lab-mp-resume-ai-trace-written" : "lab-mp-resume-ai-trace-write-failed");
            }
        }
    }
    SetLastError(incoming);
}
