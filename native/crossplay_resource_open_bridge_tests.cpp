#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_resource_open_bridge.h"
#include <stdio.h>
#include <string.h>

namespace {
alignas(16) unsigned char original_entry_fx[512]{},caller_fx[512]{};
uintptr_t observed_this=0,observed_path=0;
uint32_t observed_mode=0,original_calls=0,observer_calls=0;
DWORD original_error=0;
uint32_t changing_mxcsr=0x3f80;
void __stdcall Observer(void* object,const void* path,uint32_t mode) {
    ++observer_calls;
    observed_this=reinterpret_cast<uintptr_t>(object);
    observed_path=reinterpret_cast<uintptr_t>(path);observed_mode=mode;
    SetLastError(0xbad);
    __asm {
        fninit
        fldz
        pxor xmm0,xmm0
        pcmpeqd xmm1,xmm1
        ldmxcsr changing_mxcsr
    }
}
void __stdcall FaultObserver(void* object,const void* path,uint32_t mode) {
    Observer(object,path,mode);
    RaiseException(0xe0537002,0,0,nullptr);
}
void __cdecl OriginalRecord(void* object,const void* path,uint32_t mode) {
    original_error=GetLastError();++original_calls;
    if(object!=reinterpret_cast<void*>(0x12345000) || path!=reinterpret_cast<void*>(0x23456000)
        || mode!=0x10113)original_calls+=100;
    SetLastError(0x4444);
}
__declspec(naked) void Original() {
    __asm {
        fxsave original_entry_fx
        push dword ptr [esp+8]
        push dword ptr [esp+8]
        push ecx
        call OriginalRecord
        add esp,12
        mov eax,0x13572468
        mov edx,0x24681357
        stc
        ret 8
    }
}
bool One(uint32_t observation) {
    using namespace CrossplayResourceOpenBridge::Detail;
    const bool observe=observation!=0,fault=observation==2;
    original=reinterpret_cast<uintptr_t>(&Original);callback=fault?FaultObserver:observe?Observer:nullptr;callback_failed=0;
    original_calls=observer_calls=0;observed_this=observed_path=0;observed_mode=0;original_error=0;
    uintptr_t before=0,after=0;uint32_t first=0,second=0,flags=0,result_bx=0,result_si=0,result_di=0;
    SetLastError(0x7777);
    __asm {
        push ebx
        push esi
        push edi
        fxsave caller_fx
        mov before,esp
        mov ebx,0x11112222
        mov esi,0x33334444
        mov edi,0x55556666
        mov ecx,0x12345000
        push 0x10113
        push 0x23456000
        call Forward
        mov first,eax
        mov second,edx
        pushfd
        pop eax
        mov flags,eax
        mov after,esp
        mov result_bx,ebx
        mov result_si,esi
        mov result_di,edi
        pop edi
        pop esi
        pop ebx
    }
    // FXSAVE stores instruction/data pointers too; compare the actual register
    // payload, x87 control/status/tag and MXCSR, not those diagnostic addresses.
    const bool fp=!memcmp(caller_fx,original_entry_fx,6)
        && !memcmp(caller_fx+24,original_entry_fx+24,8)
        && !memcmp(caller_fx+32,original_entry_fx+32,256);
    return before==after && first==0x13572468 && second==0x24681357 && (flags&1)
        && result_bx==0x11112222 && result_si==0x33334444 && result_di==0x55556666 && fp
        && original_calls==1 && original_error==0x7777 && GetLastError()==0x4444 && (callback_failed!=0)==fault
        && observer_calls==(observe?1u:0u) && (!observe || (observed_this==0x12345000 && observed_path==0x23456000 && observed_mode==0x10113));
}
}
extern "C" __declspec(dllexport) void __cdecl WePlayResourceBridgeBind(uintptr_t target,uint32_t observe) {
    CrossplayResourceOpenBridge::Detail::original=target;
    CrossplayResourceOpenBridge::Detail::callback=observe?Observer:nullptr;
    CrossplayResourceOpenBridge::Detail::callback_failed=0;
    observer_calls=0;observed_this=observed_path=0;observed_mode=0;
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayResourceBridgeObserved(
    uintptr_t object,uintptr_t path,uint32_t mode,uint32_t count) {
    return !CrossplayResourceOpenBridge::Detail::callback_failed && observer_calls==count
        && (!count || (observed_this==object && observed_path==path && observed_mode==mode))?1u:0u;
}
using CrossplayResourceOpenBridge::Detail::Forward;
extern "C" __declspec(dllexport) __declspec(naked) void WePlayResourceOpenForward() {
    __asm {jmp Forward}
}
int main() {
    // Restore the test program's incoming FP state after deliberately hostile
    // observer callbacks; the assertions inspect state at original entry.
    alignas(16) unsigned char saved[512]{};
    __asm {fxsave saved}
    const bool okay=One(0) && One(1) && One(2);
    __asm {fxrstor saved}
    if(!okay)return 1;
    puts("PASS native thiscall path/mode, ret8, result/flags/error/registers/FP preservation, including a real observer exception.");
    return 0;
}
