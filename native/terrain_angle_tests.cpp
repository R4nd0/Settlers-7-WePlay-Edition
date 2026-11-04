#define S7_TERRAIN_ANGLE_TESTING
#include "offline_bootstrap.cpp"
#include "terrain_angle.h"
#include <cmath>
#include <cstdio>
void ReportLocalStatus(const char*) { SetLastError(12345); }

#ifdef S7_LOCAL_NETWORK_PUMP
namespace {
alignas(16) unsigned char terrain_original_fx[512]{}, terrain_expected_fx[512]{}, terrain_seen_fx[512]{};
alignas(16) uint32_t terrain_xmm[32]{};
uint32_t terrain_gpr[9]{}, terrain_arguments[2]{}, terrain_flags = 0;
uintptr_t terrain_saved_stack = 0, terrain_returned_stack = 0;
uint32_t terrain_canary = 0, terrain_result = 0, terrain_expected_flags = 0;
__declspec(naked) void FakeTerrainConstructor() {
    __asm {
        pushfd
        pushad
        fxsave terrain_seen_fx
        mov eax, [esp + 28]
        mov terrain_gpr[0], eax
        mov eax, [esp + 24]
        mov terrain_gpr[4], eax
        mov eax, [esp + 20]
        mov terrain_gpr[8], eax
        mov eax, [esp + 16]
        mov terrain_gpr[12], eax
        mov eax, [esp + 8]
        mov terrain_gpr[16], eax
        mov eax, [esp + 4]
        mov terrain_gpr[20], eax
        mov eax, [esp]
        mov terrain_gpr[24], eax
        mov eax, [esp + 32]
        mov terrain_flags, eax
        mov eax, [esp + 40]
        mov terrain_arguments[0], eax
        mov eax, [esp + 44]
        mov terrain_arguments[4], eax
        popad
        popfd
        mov eax, ecx
        ret 8
    }
}
// Save the test runner's complete state, seed the custom native ABI, and
// capture it at the original constructor's entry. No C++ call may intervene.
__declspec(naked) void ExerciseTerrainBridge() {
    __asm {
        pushfd
        pushad
        mov terrain_saved_stack, esp
        fxsave terrain_original_fx
        movaps xmm0, terrain_xmm[0]
        movaps xmm1, terrain_xmm[16]
        movaps xmm2, terrain_xmm[32]
        movaps xmm3, terrain_xmm[48]
        movaps xmm4, terrain_xmm[64]
        movaps xmm5, terrain_xmm[80]
        movaps xmm6, terrain_xmm[96]
        movaps xmm7, terrain_xmm[112]
        fld1
        fldpi
        fxsave terrain_expected_fx
        push 0xa55aa55a
        push 0x88888888
        push 0x99999999
        mov eax, 0x11111111
        mov ecx, 0x22222222
        mov edx, 0x33333333
        mov ebx, 0x44444444
        mov ebp, 0x55555555
        mov esi, 0x66666666
        mov edi, 0x77777777
        stc
        pushfd
        pop terrain_expected_flags
        call ConstructLocalTerrainFootprint
        mov terrain_result, eax
        mov terrain_returned_stack, esp
        mov eax, [esp]
        mov terrain_canary, eax
        fxrstor terrain_original_fx
        mov esp, terrain_saved_stack
        popad
        popfd
        ret
    }
}
__declspec(naked) float __cdecl NativeCosine(float) {
    __asm { fld dword ptr [esp + 4] }
    __asm { fcos }
    __asm { ret }
}
__declspec(naked) float __cdecl NativeSine(float) {
    __asm { fld dword ptr [esp + 4] }
    __asm { fsin }
    __asm { ret }
}
}
#endif
namespace {
float FromBits(uint32_t bits) { float value = 0; memcpy(&value, &bits, 4); return value; }
uint32_t ToBits(float value) { uint32_t bits = 0; memcpy(&bits, &value, 4); return bits; }
}

int main() {
    int checks = 0, failures = 0;
    auto check = [&](bool okay, int line) {
        ++checks; if (!okay) { ++failures; std::printf("Failed terrain check %d at %d\n", checks, line); }
    };
#define check(x) check((x), __LINE__)
    // Independent CRT oracle over exponents, signs and mantissas, including
    // the exact finite angle recovered from the stalled native frame.
    const uint32_t examples[] = {0,0x80000000u,1,0x80000001u,0x3f800000u,0x43b40000u,
        0x5effffffu,0x5f000000u,0x6e1cbbdau,0x7f7fffffu,0x7f800000u,0xff800000u,0x7fc12345u};
    for (uint32_t bits : examples) {
        const auto reduced = terrain_angle::Reduce(bits);
        if (terrain_angle::NeedsReduction(bits))
            check(FromBits(reduced) == static_cast<float>(std::fmod(static_cast<double>(FromBits(bits)), 360.0)));
        else check(reduced == bits);
    }
    check(terrain_angle::Reduce(0x6e1cbbdau) == ToBits(24.0f));
    uint32_t random = 0x735731u;
    for (unsigned int exponent = 190; exponent < 255; ++exponent) {
        for (unsigned int n = 0; n < 128; ++n) {
            random = random * 1664525u + 1013904223u;
            for (uint32_t sign : {0u,0x80000000u}) {
                const uint32_t bits = sign | (exponent << 23) | (random & 0x7fffffu);
                const auto reduced = terrain_angle::Reduce(bits);
                check(FromBits(reduced) == static_cast<float>(std::fmod(static_cast<double>(FromBits(bits)), 360.0)));
                check((reduced & 0x80000000u) == sign && std::fabs(FromBits(reduced)) < 360.0f);
            }
        }
    }
#ifdef S7_LOCAL_NETWORK_PUMP
    // Reproduce the native failure without starting its unbounded raster:
    // x87 leaves this out-of-range argument unchanged, producing giant bounds.
    const float bad = FromBits(0x6e1cbbdau);
    const float radians = (3.1415927410125732f * bad) / 180.0f;
    check(NativeCosine(radians) == radians && NativeSine(radians) == radians);
    check(std::fabs(1000.0f * NativeCosine(radians)) > 1.0e20f);
    const float safeRadians = (3.1415927410125732f * FromBits(terrain_angle::Reduce(0x6e1cbbdau))) / 180.0f;
    check(std::fabs(NativeCosine(safeRadians)) <= 1.0f && std::fabs(NativeSine(safeRadians)) <= 1.0f);
    check(std::fabs(1000.0f * NativeCosine(safeRadians)) <= 1000.0f);

    auto* memory = static_cast<unsigned char*>(VirtualAlloc(nullptr, 0x1106000,
        MEM_COMMIT | MEM_RESERVE, PAGE_EXECUTE_READWRITE));
    if (!memory) return 2;
    image_base = reinterpret_cast<uintptr_t>(memory);
    initialization = 2; local_connect_probe = true; bootstrap_thread = GetCurrentThreadId();
    terrain_angle_test_clobber = true;
    for (unsigned int i = 0; i < 32; ++i) terrain_xmm[i] = 0x3f000000u + i * 0x1234u;
    for (bool steam : {false,true}) {
        steam_history = steam;
        terrain_footprint_constructor = NativeVA(0x894EA0);
        auto* target = reinterpret_cast<unsigned char*>(terrain_footprint_constructor);
        target[0] = 0xe9;
        const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&FakeTerrainConstructor))
            - static_cast<uint32_t>(terrain_footprint_constructor + 5);
        memcpy(target + 1, &relative, 4);
        check(FlushInstructionCache(GetCurrentProcess(), target, 5) != FALSE);
        for (uint32_t bits : {0u,0x80000000u,0x3f800000u,0x43b40000u,0x5effffffu,
            0x5f000000u,0x6e1cbbdau,0xee1cbbdau,0x7f7fffffu,0x7f800000u,0x7fc12345u}) {
            for (int guard = 0; guard < 4; ++guard) {
                initialization = guard == 1 ? 1 : 2;
                local_connect_probe = guard != 2;
                bootstrap_thread = guard == 3 ? 0 : GetCurrentThreadId();
                terrain_xmm[8] = bits;
                SetLastError(4444); ExerciseTerrainBridge();
                check(GetLastError() == 4444 && terrain_result == 0x22222222u);
                check(terrain_returned_stack == terrain_saved_stack - 4 && terrain_canary == 0xa55aa55au);
                check(terrain_arguments[0] == 0x99999999u && terrain_arguments[1] == 0x88888888u);
                for (unsigned int i = 0; i < 7; ++i) check(terrain_gpr[i] == 0x11111111u * (i + 1));
                check(terrain_flags == terrain_expected_flags);
                const uint32_t expected = guard ? bits : terrain_angle::Reduce(bits);
                memcpy(terrain_expected_fx + 192, &expected, 4);
                check(memcmp(terrain_expected_fx, terrain_seen_fx, 16) == 0);
                check(memcmp(terrain_expected_fx + 24, terrain_seen_fx + 24, 264) == 0);
            }
        }
    }
    VirtualFree(memory, 0, MEM_RELEASE);
#endif
    std::printf("%s %d terrain angle checks\n", failures ? "FAIL" : "PASS", checks);
    return failures ? 1 : 0;
}
