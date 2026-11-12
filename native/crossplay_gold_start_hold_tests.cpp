#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_gold_start_hold.h"
#include <cstdio>
using CrossplayNativeStart::GoldStartHold;
using CrossplayNativeStart::GoldStartBridge;
namespace {
uint32_t fixture_manager[8]{},main_object[8]{},event_object[8]{},fixture_session[8]{};
unsigned callbacks=0;uint32_t marker=UINT32_MAX;bool reject=false,throws=false,reenter=false;
void* event_argument=event_object;uint32_t mode_argument=0,argument_a=0;
bool __stdcall Capture(void* m,const void* e) {
    ++callbacks;SetLastError(0x3333);
    if(m!=fixture_manager || e!=event_object)return false;
    if(reenter)GoldStartHold::Send(m,reinterpret_cast<void*>(0xa4e430),e,0,0,0,0);
    if(throws)throw 3;
    return !reject;
}
__declspec(naked) void Continuation() {
    __asm {
        mov eax,dword ptr [esp+0x20]
        mov marker,eax
        add esp,0x60
        pop edi
        pop esi
        pop ebx
        pop ebp
        ret
    }
}
__declspec(naked) void CallSite() {
    __asm {
        push ebp
        mov ebp,esp
        push ebx
        push esi
        push edi
        sub esp,0x60
        mov ecx,offset fixture_manager
        mov edx,0xa4e430
        mov ebx,0x12345678
        push 0
        push 0
        push argument_a
        push mode_argument
        push event_argument
        jmp GoldStartBridge
    }
}
void Reset() {
    callbacks=0;marker=UINT32_MAX;reject=throws=reenter=false;mode_argument=argument_a=0;event_argument=event_object;
    main_object[0]=0xf55600;main_object[4]=reinterpret_cast<uint32_t>(fixture_manager);main_object[5]=reinterpret_cast<uint32_t>(fixture_session);
    fixture_manager[0]=0xf566cc;
    event_object[0]=0xf555e4;event_object[1]=0x00070007;event_object[2]=0x11223344;event_object[3]=1;
    event_object[4]=event_object[5]=event_object[6]=event_object[7]=0;
    auto& s=GoldStartHold::Current();s={};s.host=0x11223344;s.callback=&Capture;
    s.roots.main=reinterpret_cast<uintptr_t>(main_object);s.roots.manager=reinterpret_cast<uintptr_t>(fixture_manager);
    s.roots.session=reinterpret_cast<uintptr_t>(fixture_session);s.roots.owner=GetCurrentThreadId();s.roots.installed=true;
    *reinterpret_cast<uintptr_t*>(0x10f83dc)=reinterpret_cast<uintptr_t>(main_object);
}
bool Run(unsigned count,bool captured,bool failed) {
    uintptr_t before=0,after=1;uint32_t out_bx=0,out_si=0,out_di=0;SetLastError(0x7777);
    __asm {
        push ebx
        push esi
        push edi
        mov before,esp
        mov ebx,0x11112222
        mov esi,0x33334444
        mov edi,0x55556666
        call CallSite
        mov after,esp
        mov out_bx,ebx
        mov out_si,esi
        mov out_di,edi
        pop edi
        pop esi
        pop ebx
    }
    const auto& s=GoldStartHold::Current();
    return before==after && out_bx==0x11112222 && out_si==0x33334444 && out_di==0x55556666 && marker==0x12345678
        && callbacks==count && s.captured==captured && s.failed==failed && GetLastError()==0x7777;
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayGoldStartHoldMethodsOracle(uint32_t edition) {
    return edition==1 && GoldStartHold::Methods()?1u:0u;
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayGoldStartHoldABICase(uint32_t variant) {
    if(variant>13)return 0;
    unsigned char jump[5]={0xe9};const int32_t relative=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&Continuation)-(0xa35f6a+5));
    memcpy(jump+1,&relative,4);memcpy(reinterpret_cast<void*>(0xa35f6a),jump,5);
    Reset();unsigned count=1;bool captured=true,failed=false;
    if(variant==1){mode_argument=1;count=0;captured=false;failed=true;}
    if(variant==2){argument_a=1;count=0;captured=false;failed=true;}
    if(variant==3){++GoldStartHold::Current().roots.owner;count=0;captured=false;failed=true;}
    if(variant==4){++main_object[5];count=0;captured=false;failed=true;}
    if(variant==5){event_object[0]=0;count=0;captured=false;failed=true;}
    if(variant==6){event_object[1]=0x00070008;count=0;captured=false;failed=true;}
    if(variant==7){event_object[2]++;count=0;captured=false;failed=true;}
    if(variant==8){event_object[3]=0;count=0;captured=false;failed=true;}
    if(variant==9){reject=true;captured=false;failed=true;}
    if(variant==10){GoldStartHold::Current().roots.installed=false;count=0;captured=false;failed=true;}
    if(variant==11){GoldStartHold::Current().callback=nullptr;count=0;captured=false;failed=true;}
    if(variant==12){GoldStartHold::Current().attempted=true;count=0;captured=false;failed=true;}
    if(variant==13){reenter=true;captured=false;failed=true;}
    return Run(count,captured,failed)?1u:0u;
}
int main(){std::puts("Offline fixed-address ABI fixture only; use the CPU verifier.");return 2;}
