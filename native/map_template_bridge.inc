// The original ED72E0 copies a native std::string, returns c_str(), then destroys
// that copy before ED7B80 constructs its qString result. Inline names therefore
// point into a dead stack frame; heap names also rely on freed storage.
// Replace only ED7B80's call at ED7B97. Its original qString allocation/copy and
// caller-owned destruction remain intact. No template or simulation is changed.
thread_local char local_template_name_snapshot[1025]{};
#ifdef S7_MAP_TEMPLATE_TESTING
bool map_template_test_clobber = false;
#endif
const char* __cdecl SnapshotLocalTemplateNameData(void* self) {
    const DWORD incoming = GetLastError();
#ifdef S7_MAP_TEMPLATE_TESTING
    if (map_template_test_clobber) {
        __asm { pxor xmm0, xmm0 }
        __asm { pxor xmm7, xmm7 }
        __asm { fld1 }
        _mm_setcsr(_mm_getcsr() ^ 0x2000u);
        SetLastError(6789);
    }
#endif
    const char* result = nullptr;
    local_template_name_snapshot[0] = 0;
    // Check the atomic owner token before consulting ordinary game state.
    if (GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)
        && initialization == 2 && local_connect_probe) {
        __try {
            auto* network = *Native<unsigned char**>(0x1379E14);
            if (self && self == *Native<void**>(0x1379E10) && LocalConnectionReady(network)) {
                auto* handler = *reinterpret_cast<unsigned char**>(network + 0xa4c);
                if (handler && *reinterpret_cast<uintptr_t*>(handler) == NativeVA(0x113FE90)) {
                    auto* implementation = *reinterpret_cast<unsigned char**>(handler + 0x1c);
                    auto* session = implementation ? *reinterpret_cast<unsigned char**>(implementation + 0x10) : nullptr;
                    if (session && *reinterpret_cast<uintptr_t*>(session) == NativeVA(0x1140150)
                        && *reinterpret_cast<uintptr_t*>(session + 0x1ec) == NativeVA(0x1086D3C)) {
                        const auto& name = *reinterpret_cast<const NativeSmallString*>(session + 0x1f8);
                        if (name.length && name.length < sizeof(local_template_name_snapshot)
                            && name.capacity >= 15 && name.capacity >= name.length && name.capacity <= 0x100000) {
                            const auto* data = SmallStringData(name);
                            if (data && data[name.length] == 0 && !memchr(data, 0, name.length)) {
                                memcpy(local_template_name_snapshot, data, name.length + 1);
                                if (network == *Native<unsigned char**>(0x1379E14)
                                    && self == *Native<void**>(0x1379E10)
                                    && handler == *reinterpret_cast<unsigned char**>(network + 0xa4c)
                                    && implementation == *reinterpret_cast<unsigned char**>(handler + 0x1c)
                                    && session == *reinterpret_cast<unsigned char**>(implementation + 0x10))
                                    result = local_template_name_snapshot;
                            }
                        }
                    }
                }
            }
        } __except ((GetExceptionCode() == EXCEPTION_ACCESS_VIOLATION || GetExceptionCode() == EXCEPTION_IN_PAGE_ERROR)
            ? EXCEPTION_EXECUTE_HANDLER : EXCEPTION_CONTINUE_SEARCH) {
            result = nullptr;
        }
    }
    SetLastError(incoming);
    return result;
}

// Original no-argument thiscall, EAX=const char*. Keep the complete incoming
// machine/FP state while providing EAX from bounded adapter-owned TLS storage.
// ED7B80 copies that storage synchronously with the original qString constructor.
__declspec(naked) const char* __fastcall SnapshotLocalTemplateName(void*, void*) {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 24]
        call SnapshotLocalTemplateNameData
        add esp, 4
        mov dword ptr [edi + 28], eax
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        ret
    }
}
