#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <intrin.h>
uintptr_t exercise_bridge=0; unsigned int observer_calls=0; uint32_t observer_frame[12]{};
void __cdecl ObserveLabPriorityEntry(const uint32_t* frame, uint32_t kind) {
    const DWORD error=GetLastError(); ++observer_calls; memcpy(observer_frame,frame,48);
    observer_frame[8]=kind;
    __asm { pxor xmm0,xmm0 }
    __asm { pxor xmm7,xmm7 }
    __asm { fld1 }
    _mm_setcsr(_mm_getcsr() ^ 0x2000u); SetLastError(error);
}
#include "lab_priority_bridge.inc"
namespace {
alignas(16) unsigned char audit_original_fx[512]{}, audit_expected_fx[512]{}, audit_seen_fx[512]{};
alignas(16) uint32_t audit_xmm[32]{};
uint32_t audit_gpr[9]{}, audit_arguments[2]{}, audit_flags = 0;
uintptr_t audit_saved_stack = 0, audit_returned_stack = 0;
uint32_t audit_canary = 0, audit_result = 0, audit_expected_flags = 0;
__declspec(naked) void FakeOriginal() {
    __asm {
        lea esp, [esp + 4]
        pop ebp
        pushfd
        pushad
        fxsave audit_seen_fx
        mov eax, [esp + 28]
        mov audit_gpr[0], eax
        mov eax, [esp + 24]
        mov audit_gpr[4], eax
        mov eax, [esp + 20]
        mov audit_gpr[8], eax
        mov eax, [esp + 16]
        mov audit_gpr[12], eax
        mov eax, [esp + 8]
        mov audit_gpr[16], eax
        mov eax, [esp + 4]
        mov audit_gpr[20], eax
        mov eax, [esp]
        mov audit_gpr[24], eax
        mov eax, [esp + 32]
        mov audit_flags, eax
        mov eax, [esp + 40]
        mov audit_arguments[0], eax
        mov eax, [esp + 44]
        mov audit_arguments[4], eax
        popad
        popfd
        mov eax, ecx
        ret 8
    }
}
// Save the test runner's complete state, seed the custom native ABI, and
// capture it at the original constructor's entry. No C++ call may intervene.
__declspec(naked) void ExerciseBridge() {
    __asm {
        pushfd
        pushad
        mov audit_saved_stack, esp
        fxsave audit_original_fx
        movaps xmm0, audit_xmm[0]
        movaps xmm1, audit_xmm[16]
        movaps xmm2, audit_xmm[32]
        movaps xmm3, audit_xmm[48]
        movaps xmm4, audit_xmm[64]
        movaps xmm5, audit_xmm[80]
        movaps xmm6, audit_xmm[96]
        movaps xmm7, audit_xmm[112]
        fld1
        fldpi
        fxsave audit_expected_fx
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
        pop audit_expected_flags
        call dword ptr [exercise_bridge]
        mov audit_result, eax
        mov audit_returned_stack, esp
        mov eax, [esp]
        mov audit_canary, eax
        fxrstor audit_original_fx
        mov esp, audit_saved_stack
        popad
        popfd
        ret
    }
}
}
int main() {
    lab_priority_task_resume=reinterpret_cast<uintptr_t>(&FakeOriginal);
    lab_priority_group_resume=reinterpret_cast<uintptr_t>(&FakeOriginal);
    for(unsigned i=0;i<32;++i) audit_xmm[i]=0x3f000000u+i*0x1234u;
    unsigned checks=0, failures=0;
    auto check=[&](bool b){++checks;if(!b)++failures;};
    for(unsigned i=0;i<128;++i) {
        exercise_bridge = i%2 ? reinterpret_cast<uintptr_t>(&LabPriorityGroupBridge) : reinterpret_cast<uintptr_t>(&LabPriorityTaskBridge); SetLastError(4444); ExerciseBridge();
        check(observer_calls==i+1); check(observer_frame[8]==i%2); check(GetLastError()==4444);
        check(audit_result==0x22222222u);
        check(audit_returned_stack==audit_saved_stack-4 && audit_canary==0xa55aa55au);
        check(audit_arguments[0]==0x99999999u && audit_arguments[1]==0x88888888u);
        check(observer_frame[6]==0x22222222u && observer_frame[10]==0x99999999u && observer_frame[11]==0x88888888u);
        for(unsigned j=0;j<7;++j)check(audit_gpr[j]==0x11111111u*(j+1));
        check(audit_flags==audit_expected_flags);
        check(memcmp(audit_expected_fx,audit_seen_fx,16)==0);
        check(memcmp(audit_expected_fx+24,audit_seen_fx+24,264)==0);
    }
    std::printf("%s %u priority observer ABI checks (%u failures)\n",failures?"FAIL":"PASS",checks,failures);
    return failures?1:0;
}
