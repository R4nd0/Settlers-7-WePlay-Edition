#define WIN32_LEAN_AND_MEAN
#define S7_PRODUCTION_OUTGOING_TESTING
#include <windows.h>
#include <intrin.h>
#include <cstdio>
#include <stdexcept>
#include "crossplay_outgoing_observer.h"
namespace CrossplayProduction {const Launch* CurrentLaunch(){return nullptr;}}
namespace {
namespace O=CrossplayProduction::OutgoingObserver;
unsigned checks=0,recorded=0,forwarded=0;
void* expected_manager=reinterpret_cast<void*>(0x12345678);
void* expected_event=reinterpret_cast<void*>(0x23456789);
bool broadcast=false,arguments=false,fp=false,throw_callback=false,reenter=false;
DWORD original_error=0;
uint32_t original_flags=0;bool direction=false;
__declspec(align(16)) unsigned char saved[512]{};
void Require(bool value){++checks;if(!value)throw std::runtime_error("outgoing observer fixture");}
void __cdecl Original(void* manager,uint32_t target,const void* event){
    original_error=GetLastError();++forwarded;
    arguments=manager==expected_manager && event==expected_event && target==(broadcast?UINT32_MAX:0x3456789au);
    unsigned short control=0;uint32_t mxcsr=0,xmm=0;double value=0;
    __asm {
        fnstcw control
        stmxcsr mxcsr
        fst qword ptr [value]
        movd xmm,xmm2
    }
    fp=control==0x27f && mxcsr==0x3f80 && value==7.25 && xmm==0x3fa00000;
    SetLastError(0x4444);
}
__declspec(naked) void GoldTarget(){__asm{
    pushfd
    pop original_flags
    cld
    push dword ptr [esp+12]
    push dword ptr [esp+12]
    push dword ptr [esp+12]
    call Original
    add esp,12
    mov eax,0x13572468
    mov edx,0x24681357
    stc
    ret 12
}}
__declspec(naked) void GoldBroadcast(){__asm{
    pushfd
    pop original_flags
    cld
    push dword ptr [esp+8]
    push 0xffffffff
    push dword ptr [esp+12]
    call Original
    add esp,12
    mov eax,0x13572468
    mov edx,0x24681357
    stc
    ret 8
}}
__declspec(naked) void HistoryTarget(){__asm{
    pushfd
    pop original_flags
    cld
    push dword ptr [esp+8]
    push dword ptr [esp+8]
    push ecx
    call Original
    add esp,12
    mov eax,0x13572468
    mov edx,0x24681357
    stc
    ret 8
}}
__declspec(naked) void HistoryBroadcast(){__asm{
    pushfd
    pop original_flags
    cld
    push dword ptr [esp+4]
    push 0xffffffff
    push ecx
    call Original
    add esp,12
    mov eax,0x13572468
    mov edx,0x24681357
    stc
    ret 4
}}
void Observe(void* manager,const void* event,uint32_t mode,uint32_t target){
    Require(manager==expected_manager && event==expected_event && mode==(broadcast?2u:1u)
        && target==(broadcast?UINT32_MAX:0x3456789au));++recorded;
    if(reenter)O::Record(manager,event,mode,target);
    __asm {
        fninit
        pxor xmm2,xmm2
    }
    _mm_setcsr(0x5f80);SetLastError(0x3333);
    if(throw_callback)throw std::runtime_error("expected callback exception");
}
void Run(bool history,bool wide,unsigned expected){
    broadcast=wide;recorded=forwarded=0;arguments=fp=false;original_error=0;
    O::target_method=reinterpret_cast<uintptr_t>(history?&HistoryTarget:&GoldTarget);
    O::broadcast_method=reinterpret_cast<uintptr_t>(history?&HistoryBroadcast:&GoldBroadcast);
    O::Profile bridge_profile;bridge_profile.gold=!history;
    const auto bridge=O::Bridge(bridge_profile,wide?2u:0u);
    uintptr_t before=0,after=1;uint32_t first=0,second=0,flags=0,result_bx=0,result_si=0,result_di=0;
    const unsigned short control=0x27f;const uint32_t mxcsr=0x3f80,xmm=0x3fa00000;const double value=7.25;
    SetLastError(0x7777);
    __asm{
        fxsave saved
        fninit
        fldcw control
        ldmxcsr mxcsr
        fld qword ptr [value]
        movd xmm2,xmm
        push ebx
        push esi
        push edi
        mov before,esp
        mov ebx,0x11112222
        mov esi,0x33334444
        mov edi,0x55556666
        mov ecx,expected_manager
        push expected_event
        cmp wide,0
        jne no_target
        push 0x3456789a
    no_target:
        cmp history,0
        jne no_manager
        push expected_manager
    no_manager:
        cmp direction,0
        je forward_direction
        std
    forward_direction:
        call bridge
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
        fxrstor saved
    }
    const DWORD outgoing=GetLastError();
    Require(before==after);Require(first==0x13572468 && second==0x24681357 && (flags&1));
    Require(result_bx==0x11112222 && result_si==0x33334444 && result_di==0x55556666);
    Require(recorded==expected && forwarded==1 && arguments);Require(fp);
    Require(original_error==0x7777 && outgoing==0x4444);
    Require(bool(original_flags&0x400)==direction);
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayOutgoingGuard(uint32_t edition){
    return O::Methods(static_cast<Crossplay::Edition>(edition))?1u:0u;
}
int main(){try{
    O::installed=true;O::test_authorized=true;O::owner=GetCurrentThreadId();
    O::manager=reinterpret_cast<uintptr_t>(expected_manager);O::callback=&Observe;
    for(const auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}){
        const auto pin=O::Pin(edition);Require(pin.target && pin.broadcast && pin.sites[0]!=pin.sites[1]);
        for(bool wide:{false,true}){
            const bool history=edition!=Crossplay::Edition::Gold;
            Run(history,wide,1);direction=true;Run(history,wide,1);direction=false;
            reenter=true;Run(history,wide,1);reenter=false;
            O::test_authorized=false;Run(history,wide,0);O::test_authorized=true;
            ++O::owner;Run(history,wide,0);--O::owner;
            ++O::manager;Run(history,wide,0);--O::manager;
            O::failed=true;Run(history,wide,0);O::failed=false;
            throw_callback=true;Run(history,wide,1);Require(O::failed && !O::inside);
            throw_callback=false;O::failed=false;
        }
    }
    auto* site=static_cast<unsigned char*>(VirtualAlloc(nullptr,4096,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));Require(site!=nullptr);
    const auto at=reinterpret_cast<uintptr_t>(site);const auto first=at+128,second=at+256;
    site[0]=0xe8;const int32_t delta=123;memcpy(site+1,&delta,4);
    Require(O::Matches(at,first));Require(!O::Exchange(at,second,at+512));
    Require(O::Exchange(at,first,second));Require(O::Matches(at,second));
    Require(O::Exchange(at,second,first));site[0]=0xe9;Require(!O::Matches(at,first));
    Require(VirtualFree(site,0,MEM_RELEASE)!=FALSE);
    std::printf("PASS %u outgoing ABI/FP/ownership/reentrancy/exception/callsite checks; synthetic native methods only.\n",checks);return 0;
}catch(const std::exception& e){std::printf("FAIL check %u: %s\n",checks,e.what());return 1;}}
