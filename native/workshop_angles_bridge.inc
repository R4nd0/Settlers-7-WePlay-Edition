// CPlanPlaceWorkshop::Search(event=0) initializes these invariant rotations.
// Its constructor and save visitor omit them, so resuming event=1 otherwise
// consumes allocator contents. Initialize them after the original constructor.
uintptr_t local_workshop_constructor = 0;
#ifdef S7_WORKSHOP_ANGLES_TESTING
bool workshop_angles_test_clobber = false;
#endif
void __cdecl InitializeLocalWorkshopAngles(void* object) {
    const DWORD incoming = GetLastError();
#ifdef S7_WORKSHOP_ANGLES_TESTING
    if (workshop_angles_test_clobber) {
        __asm { pxor xmm0, xmm0 }
        __asm { pxor xmm7, xmm7 }
        __asm { fld1 }
        _mm_setcsr(_mm_getcsr() ^ 0x2000u);
        SetLastError(6789);
    }
#endif
    if (initialization == 2 && local_connect_probe
        && GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)
        && object && *static_cast<const uintptr_t*>(object) == NativeVA(0x1159F00)) {
        constexpr uint32_t angles[] = {0,0x41f00000,0x42700000,0x42b40000,
                                      0x42f00000,0x43160000,0x43340000};
        memcpy(static_cast<unsigned char*>(object)+0x60,angles,sizeof(angles));
    }
    SetLastError(incoming);
}

// The factory's original constructor is thiscall, no stack arguments, returning
// the object in EAX. Preserve its actual return registers/flags/FP and LastError
// across our helper; the original allocation, constructor and cleanup still run.
__declspec(naked) void ConstructLocalWorkshopPlan() {
    __asm {
        call dword ptr [local_workshop_constructor]
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [edi + 28]
        call InitializeLocalWorkshopAngles
        add esp, 4
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        ret
    }
}
