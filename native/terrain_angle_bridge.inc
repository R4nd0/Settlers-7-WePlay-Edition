#include "terrain_angle.h"

uintptr_t terrain_footprint_constructor = 0;
#ifdef S7_TERRAIN_ANGLE_TESTING
bool terrain_angle_test_clobber = false;
#endif
uint32_t __cdecl ReduceLocalTerrainAngle(uint32_t bits) {
    const DWORD incoming = GetLastError();
#ifdef S7_TERRAIN_ANGLE_TESTING
    if (terrain_angle_test_clobber) {
        __asm { pxor xmm0, xmm0 }
        __asm { pxor xmm2, xmm2 }
        __asm { pxor xmm7, xmm7 }
        __asm { fld1 }
        _mm_setcsr(_mm_getcsr() ^ 0x2000u);
        SetLastError(5678);
    }
#endif
    uint32_t result = bits;
    if (initialization == 2 && local_connect_probe
        && GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)) {
        result = terrain_angle::Reduce(bits);
        static bool reported = false;
        if (result != bits && !reported) {
            reported = true;
            ReportLocalStatus("local-ai-terrain-angle-reduced");
        }
    }
    SetLastError(incoming);
    return result;
}

// The native constructor consumes ECX, XMM2 and two callee-cleaned stack
// arguments. Tail-jump with the original stack and all registers/FP state,
// replacing only the low float of XMM2 when reduction is required.
__declspec(naked) void ConstructLocalTerrainFootprint() {
    __asm {
        pushfd
        push eax
        movd eax, xmm2
        and eax, 0x7fffffff
        cmp eax, 0x5f000000
        jb unchanged
        cmp eax, 0x7f800000
        jae unchanged
        pop eax
        popfd
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push dword ptr [esp + 192]
        call ReduceLocalTerrainAngle
        add esp, 4
        mov dword ptr [esp + 192], eax
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        jmp dword ptr [terrain_footprint_constructor]
    unchanged:
        pop eax
        popfd
        jmp dword ptr [terrain_footprint_constructor]
    }
}
