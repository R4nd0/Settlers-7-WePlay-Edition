#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#define S7_GOLD_SDK_READY_HOLD_TESTING
#include "crossplay_gold_sdk_ready_hold.h"
#include <cstdio>
#include <cstdlib>

namespace CrossplayProduction {const Launch* CurrentLaunch(){return nullptr;}}
using CrossplayNativeReady::GoldSdkReadyHold;
using CrossplayNativeReady::GoldSdkReadyBridge;
namespace {
unsigned checks=0,probes=0;bool arguments=false,fp=false,direction=false;
uint32_t seen_ax=0,seen_cx=0,seen_dx=0,seen_flags=0;unsigned char event[24]{};
__declspec(align(16)) unsigned char saved_fp[512]{};
void Check(bool ok){++checks;if(!ok){printf("FAIL SDK Ready hold check %u\n",checks);std::exit(1);}}
void __cdecl Probe(void* manager,void* original,const void* value,uint32_t mode,uint32_t a,uint32_t b,uint32_t c,uintptr_t continuation){
    ++probes;arguments=manager==reinterpret_cast<void*>(0x12345678) && original==reinterpret_cast<void*>(0xa4e430)
        && value==event && mode==2 && a==3 && b==4 && c==5 && continuation!=0;
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
// Same authored ABI/FP/DF fixture pattern as the existing GoldStartHold tests.
// The synthetic return address avoids executing any original game address.
__declspec(naked) void CallBridge(){__asm{
    push 5
    push 4
    push 3
    push 2
    push offset event
    push offset continued
    mov ecx,0x12345678
    mov edx,0xa4e430
    mov eax,0xabcddcba
    cmp direction,0
    je forward
    push 0x646
    jmp selected
forward:
    push 0x246
selected:
    popfd
    jmp GoldSdkReadyBridge
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
    GoldSdkReadyHold::Current()={};SetLastError(1965);probes=0;arguments=fp=false;
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
    Check(seen_ax==0xabcddcba && seen_cx==0x12345678 && seen_dx==0xa4e430);
    Check((seen_flags&0xcd5)==(direction?0x444u:0x44u));Check(fp);Check(arguments && probes==1);
    Check(GetLastError()==1965);Check(GoldSdkReadyHold::Current().failed && !GoldSdkReadyHold::Current().captured
        && GoldSdkReadyHold::Current().failure==1);
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionGoldSdkReadyHoldGuardOracle(uint32_t edition){
    return edition==1 && GoldSdkReadyHold::Methods()?1u:0u;
}
// Offline only: original game pages are mapped by the bounded guard emulator.
// Validate before patching, then exercise exact owned-patch normalization too.
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionGoldSdkReadyHeldGuardOracle(uint32_t edition){
    if(edition!=1 || !GoldSdkReadyHold::Methods())return 0;
    unsigned char before[2][5]{};auto& state=GoldSdkReadyHold::Current();
    for(unsigned i=0;i<2;++i){
        auto* at=reinterpret_cast<unsigned char*>(GoldSdkReadyHold::Site(i));memcpy(before[i],at,5);
        const int32_t delta=static_cast<int32_t>(GoldSdkReadyHold::Entry(i)-(GoldSdkReadyHold::Site(i)+5));
        at[0]=0xe9;memcpy(at+1,&delta,4);
    }
    state.roots.installed=true;state.patched=3;const bool accepted=GoldSdkReadyHold::Methods();
    for(unsigned i=0;i<2;++i)memcpy(reinterpret_cast<void*>(GoldSdkReadyHold::Site(i)),before[i],5);
    state={};return accepted?1u:0u;
}
int main(){
    GoldSdkReadyHold::probe=&Probe;Run();direction=true;Run();GoldSdkReadyHold::probe=nullptr;
    Check(!GoldSdkReadyHold::Release());Check(GetLastError()==1965);
    unsigned char notification[0x18]{};
    CrossplayChat::Put(notification,0xf562a8);CrossplayChat::Put(notification+4,CrossplayReady::Type);
    CrossplayChat::Put(notification+8,22);notification[12]=1;CrossplayChat::Put(notification+0x10,1);
    for(uintptr_t continuation:{0xaa819du,0xaa8277u}){
        for(unsigned bit=0;bit<=2;++bit){
            notification[0x14]=static_cast<unsigned char>(bit);
            Check(GoldSdkReadyHold::NotificationFailure(notification,22,continuation)==(bit<=1?0u:10u));
        }
        notification[0x14]=0;Check(GoldSdkReadyHold::NotificationFailure(notification,23,continuation)==8);
        CrossplayChat::Put(notification+0x10,2);Check(GoldSdkReadyHold::NotificationFailure(notification,22,continuation)==9);
        CrossplayChat::Put(notification+0x10,1);
    }
    Check(GoldSdkReadyHold::NotificationFailure(notification,22,0xaa819e)==5);
    Check(GoldSdkReadyHold::NotificationFailure(nullptr,22,0xaa819d)==8);
    printf("PASS %u SDK Ready hold ABI/FP/DF/stack/closed-scope checks; authored only, no native Ready qualified.\n",checks);return 0;
}
