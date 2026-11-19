#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define S7_HISTORY_START_HOLD_TESTING
#include "crossplay_history_start_hold.h"
#include <cstdio>
#include <cstdlib>

namespace CrossplayProduction {const Launch* CurrentLaunch(){return nullptr;}}
using CrossplayNativeStart::HistoryStartHold;
using CrossplayNativeStart::HistoryStartBridge;
using CrossplayNativeStart::HistoryStartEntry;
using CrossplayNativeStart::history_start_resume;
namespace {
unsigned checks=0,probes=0;bool arguments=false,fp=false,direction=false;
bool enter_from_producer=false;
uintptr_t bridge_target=0;
uintptr_t mock_dispatcher[]={0,0,0,0,0,0xeda9f0};
uint32_t seen_ax=0,seen_cx=0,seen_dx=0,seen_flags=0;unsigned char event[0x20]{};
__declspec(align(16)) unsigned char saved_fp[512]{};
void Check(bool ok){++checks;if(!ok){printf("FAIL History start hold check %u\n",checks);std::exit(1);}}
void __cdecl Probe(void* manager,void* original,const void* value,uint32_t mode,uint32_t a,uint32_t b,uint32_t c){
    ++probes;arguments=manager==reinterpret_cast<void*>(0x12345678) && original==reinterpret_cast<void*>(0xeda9f0)
        && value==event && mode==2 && a==3 && b==4 && c==5;
    SetLastError(999);__asm {fninit
        pxor xmm2,xmm2}
    _mm_setcsr(0x5f80);
}
void __cdecl Inspect(){
    unsigned short control=0;uint32_t mxcsr=0,xmm=0;double value=0;
    __asm {fnstcw control
        stmxcsr mxcsr
        fst qword ptr [value]
        movd xmm,xmm2}
    fp=control==0x27f && mxcsr==0x3f80 && value==7.25 && xmm==0x3fa00000;
}
// Reuse the existing Gold capture-bridge ABI/FP/DF fixture pattern. The authored
// continuation avoids executing any original game address in the Windows test.
__declspec(naked) void CallBridge(){__asm{
    push 5
    push 4
    push 3
    push 2
    push offset event
    cmp enter_from_producer,0
    jne producer
    push offset continued
    mov edx,0xeda9f0
    mov eax,0xabcddcba
    jmp common
producer:
    mov history_start_resume,offset continued
    mov eax,offset mock_dispatcher
common:
    mov ecx,0x12345678
    cmp direction,0
    je forward
    push 0x646
    jmp selected
forward:
    push 0x246
selected:
    popfd
    jmp dword ptr [bridge_target]
continued:
    mov seen_ax,eax
    mov seen_cx,ecx
    mov seen_dx,edx
    pushfd
    pop seen_flags
    cld
    pushad
    call Inspect
    popad
    ret
}}
void Run(){
    uintptr_t before=0,after=1;uint32_t seen_bx=0,seen_si=0,seen_di=0;
    const unsigned short control=0x27f;const uint32_t mxcsr=0x3f80,xmm=0x3fa00000;const double value=7.25;
    bridge_target=reinterpret_cast<uintptr_t>(enter_from_producer?&HistoryStartEntry:&HistoryStartBridge);
    HistoryStartHold::Current()={};SetLastError(1973);probes=0;arguments=fp=false;
    __asm {
        fxsave saved_fp
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
        call CallBridge
        mov after,esp
        mov seen_bx,ebx
        mov seen_si,esi
        mov seen_di,edi
        pop edi
        pop esi
        pop ebx
        fxrstor saved_fp
    }
    Check(before==after);Check(seen_bx==0x11112222 && seen_si==0x33334444 && seen_di==0x55556666);
    const auto expected_ax=enter_from_producer?reinterpret_cast<uintptr_t>(mock_dispatcher):0xabcddcba;
    Check(seen_ax==expected_ax && seen_cx==0x12345678 && seen_dx==0xeda9f0);
    Check((seen_flags&0xcd5)==(direction?0x444u:0x44u));Check(fp);Check(arguments && probes==1);
    const auto& state=HistoryStartHold::Current();Check(GetLastError()==1973);
    Check(state.failed && state.failure==1 && !state.attempted && !state.captured);
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionHistoryStartHoldGuardOracle(uint32_t edition){
    return (edition==2 || edition==3) && HistoryStartHold::Methods(static_cast<Crossplay::Edition>(edition))?1u:0u;
}
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionHistoryStartEntryOracle(uint32_t edition){
    return (edition==2 || edition==3)?static_cast<uint32_t>(reinterpret_cast<uintptr_t>(&CrossplayNativeStart::HistoryStartEntry)):0u;
}
// Offline only, on modeled original pages: require original bytes first, then
// check exact ownership normalization and restore those same bytes afterward.
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionHistoryStartHeldGuardOracle(uint32_t edition){
    const auto profile=static_cast<Crossplay::Edition>(edition);
    if((edition!=2 && edition!=3) || !HistoryStartHold::Methods(profile))return 0;
    const auto site=HistoryStartHold::Pin(profile).site;
    auto* at=reinterpret_cast<unsigned char*>(site);unsigned char before[5]{};memcpy(before,at,5);
    const int32_t delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&CrossplayNativeStart::HistoryStartEntry)-(site+5));
    at[0]=0xe9;memcpy(at+1,&delta,4);HistoryStartHold::Current().edition=profile;HistoryStartHold::Current().installed=true;
    const bool accepted=HistoryStartHold::Methods(profile);memcpy(at,before,5);HistoryStartHold::Current()={};return accepted?1u:0u;
}
int main(){
    HistoryStartHold::probe=&Probe;Run();direction=true;Run();
    enter_from_producer=true;Run();direction=false;Run();
    history_start_resume=0;HistoryStartHold::probe=nullptr;
    Check(!HistoryStartHold::Release());Check(GetLastError()==1973);
    CrossplayNative::ParticipantEvent binding;CrossplayNative::LobbyAdmission admission;
    Check(!HistoryStartHold::InstallBound(binding,admission,"",nullptr));
    printf("PASS %u History start hold ABI/FP/DF/stack/closed-scope checks; authored only, no original start or world qualified.\n",checks);return 0;
}
