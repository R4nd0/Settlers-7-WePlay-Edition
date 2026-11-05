#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <cstdio>
#include <cstring>
#include <intrin.h>
uintptr_t seen_left=0,seen_right=0;
uint32_t __cdecl CompareNeoFifoTransfers(uintptr_t a,uintptr_t b) {
    auto error=GetLastError();seen_left=a;seen_right=b;
    __asm {pxor xmm0,xmm0}
    __asm {pxor xmm7,xmm7}
    __asm {fld1}
    _mm_setcsr(_mm_getcsr()^0x2000u);SetLastError(error);return 1;
}
#include "lab_fifo_compare_bridge.inc"
alignas(16) unsigned char original_fx[512]{},expected_fx[512]{},seen_fx[512]{};
uint32_t gpr[7]{},flags=0,expected_flags=0;uintptr_t saved_stack=0,returned_stack=0;
__declspec(naked) void Exercise() {
    __asm {
        pushfd
        pushad
        mov saved_stack,esp
        fxsave original_fx
        fld1
        fldpi
        fxsave expected_fx
        mov eax,0x11111111
        mov ecx,0x22222222
        mov edx,0x33333333
        mov ebx,0x44444444
        mov ebp,0x55555555
        mov esi,0x66666666
        mov edi,0x77777777
        stc
        pushfd
        pop expected_flags
        call CompareNeoFifoTransfersBridge
        mov returned_stack,esp
        mov gpr[0],eax
        mov gpr[4],ecx
        mov gpr[8],edx
        mov gpr[12],ebx
        mov gpr[16],ebp
        mov gpr[20],esi
        mov gpr[24],edi
        pushfd
        pop flags
        fxsave seen_fx
        fxrstor original_fx
        mov esp,saved_stack
        popad
        popfd
        ret
    }
}
int main(){unsigned checks=0,failures=0;auto check=[&](bool b){++checks;if(!b)++failures;};
    for(unsigned n=0;n<128;++n){SetLastError(4444);Exercise();check(GetLastError()==4444);check(gpr[0]==1);for(unsigned i=1;i<7;++i)check(gpr[i]==0x11111111u*(i+1));check(seen_left==0x22222222u&&seen_right==0x33333333u);check(saved_stack==returned_stack);check(flags==expected_flags);check(!memcmp(expected_fx,seen_fx,16));check(!memcmp(expected_fx+24,seen_fx+24,264));}
    std::printf("%s %u FIFO bridge checks (%u failures)\n",failures?"FAIL":"PASS",checks,failures);return failures?1:0;}
