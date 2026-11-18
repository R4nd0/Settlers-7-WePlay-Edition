#include "crossplay_gold_workshop_angles.h"

#include <cstdio>

namespace {
using namespace GoldWorkshopAngles;
alignas(16) uint32_t object[36]{};
alignas(16) unsigned char saved_fx[512]{},constructor_fx[512]{},returned_fx[512]{};
uint32_t calls=0,constructor_argument=0,constructor_table=WorkshopVtable;
uintptr_t constructor_this=0,returned_object=0,stack_before=0,stack_after=0;
uint32_t constructor_flags=0,returned_flags=0;
bool world_ready=false;

bool Ready() {
    __asm {
        fldpi
        pxor xmm1,xmm1
    }
    SetLastError(4321);
    return world_ready;
}

__declspec(naked) void MockConstructor() {
    __asm {
        inc calls
        mov constructor_this,ecx
        mov eax,[esp+4]
        mov constructor_argument,eax
        mov eax,constructor_table
        mov [ecx],eax
        mov eax,ecx
        fld1
        pcmpeqd xmm1,xmm1
        fxsave constructor_fx
        stc
        pushfd
        pop constructor_flags
        ret 4
    }
}

__declspec(naked) void Exercise() {
    __asm {
        fxsave saved_fx
        mov stack_before,esp
        push 0xabc123
        lea ecx,object
        call Construct
        mov returned_object,eax
        mov stack_after,esp
        pushfd
        pop returned_flags
        fxsave returned_fx
        fxrstor saved_fx
        ret
    }
}
}

int main() {
    original=reinterpret_cast<uintptr_t>(&MockConstructor);
    const uint32_t rotations[]={0,0x41f00000,0x42700000,0x42b40000,0x42f00000,0x43160000,0x43340000};
    for(unsigned scenario=0;scenario<4;++scenario) {
        owner=GetCurrentThreadId()+(scenario==2);
        ready=&Ready;
        world_ready=scenario!=1;
        constructor_table=WorkshopVtable+(scenario==3 ? 4 : 0);
        for(auto& value:object)value=0x764fed34;
        calls=0;
        SetLastError(1234);
        Exercise();
        if (calls!=1 || constructor_argument!=0xabc123 || constructor_this!=reinterpret_cast<uintptr_t>(object)
            || returned_object!=constructor_this || stack_before!=stack_after || constructor_flags!=returned_flags
            || GetLastError()!=1234 || object[0]!=constructor_table
            || memcmp(constructor_fx,returned_fx,16) || memcmp(constructor_fx+24,returned_fx+24,392)) return 1;
        for(unsigned i=1;i<_countof(object);++i) {
            const auto first=AnglesOffset/sizeof(uint32_t);
            const uint32_t wanted=(scenario==0 && i>=first && i<first+_countof(rotations))
                ? rotations[i-first] : 0x764fed34;
            if(object[i]!=wanted)return 2;
        }
    }
    owner=GetCurrentThreadId();
    Initialize(nullptr);
    std::puts("PASS Gold workshop factory ABI, seven rotations, world/thread/class guards and untouched object fields.");
}
