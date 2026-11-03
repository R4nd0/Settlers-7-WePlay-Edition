// Own bounded scalar-only laboratory diagnostics. No RNG value is changed here.
struct LabResumeRngRecord { uint32_t turn, count, site, seed, caller, category, task_return, plan_return, plan_type, player, building; };
LabResumeRngRecord lab_resume_rng_records[4096]{};
size_t lab_resume_rng_count = 0;
bool lab_resume_rng_written = false;
ULONGLONG lab_resume_rng_finish_after = 0;
#ifdef S7_RNG_TRACE_TESTING
bool lab_resume_rng_test_clobber = false;
#endif
void __cdecl ObserveLabResumeRng(uintptr_t return_address, uintptr_t source, uint32_t memory_operand,
    uintptr_t parent_return, uintptr_t parent_esi, uintptr_t parent_ebp) {
    const DWORD incoming = GetLastError();
#ifdef S7_RNG_TRACE_TESTING
    if (lab_resume_rng_test_clobber) {
        __asm { pxor xmm0, xmm0 }
        __asm { pxor xmm5, xmm5 }
        __asm { fld1 }
    }
#endif
    if (!steam_history && initialization == 2 && local_connect_probe
        && GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)) {
        auto* logic = *Native<unsigned char**>(0x13A5220);
        auto* replay = *Native<unsigned char**>(0x13A5200);
        if (logic && replay && *reinterpret_cast<uintptr_t*>(logic) == NativeVA(0x1141534)
            && *reinterpret_cast<uintptr_t*>(replay) == NativeVA(0x1133F40)) {
            const uint32_t turn = *reinterpret_cast<uint32_t*>(replay + 0x2c);
            auto* random = *reinterpret_cast<uint32_t**>(logic + 0x10);
            if (turn >= 2245 && turn <= 2300 && random
                && (memory_operand ? source == reinterpret_cast<uintptr_t>(random) : source == random[0])
                && lab_resume_rng_count < _countof(lab_resume_rng_records)) {
                const auto site = static_cast<uint32_t>(return_address - 5 - image_base + PreferredBase);
                uint32_t caller = 0, selected_category = 0, task_return = 0, plan_return = 0, plan_type = 0, player = 0, building = 0;
                // Only 6C1F20 has no stack frame/pushes before its 6C1F49 RNG call.
                // Other sites' stack words are deliberately never interpreted as callers.
                if (site == 0x6c1f49) {
                    const uint32_t known[] = {0x530227,0x6c7797,0x6d23de,0x73d639,0x73d65f,
                        0x7438cf,0x7bb746,0x7bb7ac,0x7c1807,0x7e0f36,0x7e3a5d,0x7e3a81,0x9f6336,0x9f76b4};
                    for (uint32_t address : known) {
                        if (parent_return == NativeVA(address)) {
                            caller = address; break;
                        }
                    }
                    // ESI is the task owner at this independently mapped caller.
                    // The ECX value at the RNG instruction is a vector address,
                    // not the original category. Never log/compare that pointer.
                    if (caller == 0x9f6336 && parent_esi
                        && *reinterpret_cast<const uintptr_t*>(parent_esi) == NativeVA(0x115c004)) {
                        selected_category = *reinterpret_cast<const uint32_t*>(parent_esi + 0xc);
                        player = *reinterpret_cast<const unsigned char*>(parent_esi + 4);
                        building = *reinterpret_cast<const uint32_t*>(parent_esi + 8);
                        ULONG_PTR low = 0, high = 0;
                        GetCurrentThreadStackLimits(&low, &high);
                        if (parent_ebp >= low + 32 && parent_ebp <= high - 20 && !(parent_ebp & 3)) {
                            const auto direct = *reinterpret_cast<const uintptr_t*>(parent_ebp + 4);
                            if (direct >= image_base && direct < image_base + 0xc40000)
                                task_return = static_cast<uint32_t>(direct - image_base + PreferredBase);
                            // Only this verified frame means saved ESI is the
                            // generic task runner's current plan, not another
                            // arbitrary register/heap pointer.
                            if (direct == NativeVA(0x9f018e)) {
                                const auto parent = *reinterpret_cast<const uintptr_t*>(parent_ebp + 16);
                                if (parent >= image_base && parent < image_base + 0xc40000)
                                    plan_return = static_cast<uint32_t>(parent - image_base + PreferredBase);
                                const auto plan = *reinterpret_cast<const uintptr_t*>((parent_ebp & ~uintptr_t{7}) - 20);
                                MEMORY_BASIC_INFORMATION info{};
                                if (plan && VirtualQuery(reinterpret_cast<const void*>(plan), &info, sizeof(info)) == sizeof(info)
                                    && info.State == MEM_COMMIT && !(info.Protect & (PAGE_NOACCESS | PAGE_GUARD))
                                    && plan >= reinterpret_cast<uintptr_t>(info.BaseAddress)
                                    && plan + 4 >= plan && plan + 4 <= reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize) {
                                    const auto table = *reinterpret_cast<const uintptr_t*>(plan);
                                    if (table >= NativeVA(0x1090000) && table < NativeVA(0x1160000))
                                        plan_type = static_cast<uint32_t>(table - image_base + PreferredBase);
                                }
                            }
                        }
                    }
                }
                lab_resume_rng_records[lab_resume_rng_count++] = {turn, random[1],site,random[0],caller,selected_category,
                    task_return,plan_return,plan_type,player,building};
                lab_resume_rng_finish_after = GetTickCount64() + 2000;
            }
        }
    }
    SetLastError(incoming);
}
void FlushLabResumeRngTrace() {
    if (lab_resume_rng_written || !lab_resume_rng_count || steam_history) return;
    auto* replay = *Native<unsigned char**>(0x13A5200);
    if (!replay || *reinterpret_cast<uintptr_t*>(replay) != NativeVA(0x1133F40)
        || *reinterpret_cast<uint32_t*>(replay + 0x2c) < 2257) return;
    const DWORD incoming = GetLastError();
    const auto now = GetTickCount64();
    if (now < lab_resume_rng_finish_after) { SetLastError(incoming); return; }
    HMODULE module = nullptr; wchar_t path[MAX_PATH]{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ObserveLabResumeRng), &module)) {
        const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
        const std::wstring suffix = L"\\Base\\Bin\\uplay_r1_loader.dll";
        if (length && length < MAX_PATH && length > suffix.size()
            && _wcsicmp(path + length - suffix.size(), suffix.c_str()) == 0) {
            const auto output = std::wstring(path, length - suffix.size()) + L"\\LocalData\\lab-rng-trace.private.csv";
            HANDLE file = CreateFileW(output.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
                FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                bool okay = true;
                for (size_t i = 0; okay && i < lab_resume_rng_count; ++i) {
                    const auto& row = lab_resume_rng_records[i]; char line[128]{};
                    const int size = _snprintf_s(line, sizeof(line), _TRUNCATE, "%u,%u,%x,%u,%x,%u,%x,%x,%x,%u,%u\r\n",
                        row.turn,row.count,row.site,row.seed,row.caller,row.category,row.task_return,row.plan_return,
                        row.plan_type,row.player,row.building);
                    DWORD written = 0;
                    okay = size > 0 && WriteFile(file, line, static_cast<DWORD>(size), &written, nullptr)
                        && written == static_cast<DWORD>(size);
                }
                CloseHandle(file); lab_resume_rng_written = true;
                ReportLocalStatus(okay ? "lab-mp-resume-rng-trace-written" : "lab-mp-resume-rng-trace-write-failed");
            }
        }
    }
    SetLastError(incoming);
}

__declspec(naked) void LabResumeRng01() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 24]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul eax, dword ptr [ecx], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRng02() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 20]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul eax, dword ptr [edx], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRng06() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 4]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul eax, dword ptr [esi], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRng07() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 0]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul eax, dword ptr [edi], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRng08() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 28]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul ecx, dword ptr [eax], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRng0A() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 20]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul ecx, dword ptr [edx], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRng0B() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 16]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul ecx, dword ptr [ebx], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRng0E() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 4]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul ecx, dword ptr [esi], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRng0F() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 0]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul ecx, dword ptr [edi], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRng10() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 28]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul edx, dword ptr [eax], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRng13() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 16]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul edx, dword ptr [ebx], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRng39() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 1
        push dword ptr [edi + 24]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul edi, dword ptr [ecx], 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRngC0() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 0
        push dword ptr [edi + 28]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul eax, eax, 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRngC1() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 0
        push dword ptr [edi + 24]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul eax, ecx, 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRngC2() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 0
        push dword ptr [edi + 20]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul eax, edx, 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRngC8() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 0
        push dword ptr [edi + 28]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul ecx, eax, 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRngC9() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 0
        push dword ptr [edi + 24]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul ecx, ecx, 0x6c078965
        ret
    }
}

__declspec(naked) void LabResumeRngD0() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 8]
        push dword ptr [edi + 4]
        push dword ptr [edi + 40]
        push 0
        push dword ptr [edi + 28]
        push dword ptr [edi + 36]
        call ObserveLabResumeRng
        add esp, 24
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        imul edx, eax, 0x6c078965
        ret
    }
}

#ifdef S7_RNG_TRACE_TESTING
__declspec(naked) void __cdecl TestLabResumeRng01(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea ecx, [esp + 40]
        stc
        call LabResumeRng01
        pushfd
        push eax
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRng02(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea edx, [esp + 40]
        stc
        call LabResumeRng02
        pushfd
        push eax
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRng06(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea esi, [esp + 40]
        stc
        call LabResumeRng06
        pushfd
        push eax
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRng07(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea edi, [esp + 40]
        stc
        call LabResumeRng07
        pushfd
        push eax
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRng08(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea eax, [esp + 40]
        stc
        call LabResumeRng08
        pushfd
        push ecx
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRng0A(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea edx, [esp + 40]
        stc
        call LabResumeRng0A
        pushfd
        push ecx
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRng0B(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea ebx, [esp + 40]
        stc
        call LabResumeRng0B
        pushfd
        push ecx
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRng0E(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea esi, [esp + 40]
        stc
        call LabResumeRng0E
        pushfd
        push ecx
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRng0F(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea edi, [esp + 40]
        stc
        call LabResumeRng0F
        pushfd
        push ecx
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRng10(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea eax, [esp + 40]
        stc
        call LabResumeRng10
        pushfd
        push edx
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRng13(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea ebx, [esp + 40]
        stc
        call LabResumeRng13
        pushfd
        push edx
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRng39(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        lea ecx, [esp + 40]
        stc
        call LabResumeRng39
        pushfd
        push edi
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRngC0(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        mov eax, dword ptr [esp + 40]
        stc
        call LabResumeRngC0
        pushfd
        push eax
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRngC1(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        mov ecx, dword ptr [esp + 40]
        stc
        call LabResumeRngC1
        pushfd
        push eax
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRngC2(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        mov edx, dword ptr [esp + 40]
        stc
        call LabResumeRngC2
        pushfd
        push eax
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRngC8(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        mov eax, dword ptr [esp + 40]
        stc
        call LabResumeRngC8
        pushfd
        push ecx
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRngC9(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        mov ecx, dword ptr [esp + 40]
        stc
        call LabResumeRngC9
        pushfd
        push ecx
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

__declspec(naked) void __cdecl TestLabResumeRngD0(uint32_t, uint32_t*, uint32_t*) {
    __asm {
        pushfd
        pushad
        mov eax, dword ptr [esp + 40]
        stc
        call LabResumeRngD0
        pushfd
        push edx
        mov eax, dword ptr [esp + 52]
        mov edx, dword ptr [esp]
        mov dword ptr [eax], edx
        mov eax, dword ptr [esp + 56]
        mov edx, dword ptr [esp + 4]
        mov dword ptr [eax], edx
        add esp, 8
        popad
        popfd
        ret
    }
}

#endif