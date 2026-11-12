#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_gold_local_observer.h"
#include <cstdio>
using CrossplayNativeObserver::GoldLocalObserver;
using CrossplayNativeObserver::GoldLocalBridge;
namespace {
uint32_t manager[8]{},main_object[8]{},event_object[8]{},session[8]{};
unsigned callbacks=0,locks=0,forwarded=0;bool throw_callback=false,reenter=false;
DWORD lock_error=0;uint32_t entry_eax=0,entry_ecx=0,entry_edx=0,entry_flags=0;bool correct_lock=false,correct_arguments=false;
uint32_t lock_eax=0,lock_ecx=0,lock_edx=0,lock_flags=0;
void __cdecl LockOriginal(void* lock) {++locks;correct_lock=lock==manager+7;lock_error=GetLastError();SetLastError(0x5555);}
__declspec(naked) void LockThunk() {
    __asm {
        mov lock_eax,eax
        mov lock_ecx,ecx
        mov lock_edx,edx
        pushfd
        pop lock_flags
        push dword ptr [esp+4]
        call LockOriginal
        add esp,4
        mov eax,0x11223344
        mov ecx,0x22334455
        mov edx,0x33445566
        stc
        ret 4
    }
}
void __cdecl CompleteOriginal(void* m,const void* e,uint32_t mode,uint32_t a,uint32_t b,uint32_t c) {
    ++forwarded;correct_arguments=m==manager && e==event_object && mode<=2 && a==0x11111111 && b==0x22222222 && c==0x33333333;
    SetLastError(0x4444);
}
__declspec(naked) void FinishOriginal() {
    __asm {
        mov entry_eax,eax
        mov entry_ecx,ecx
        mov entry_edx,edx
        pushfd
        pop entry_flags
        push dword ptr [ebp+0x18]
        push dword ptr [ebp+0x14]
        push dword ptr [ebp+0x10]
        push dword ptr [ebp+0xc]
        push esi
        push edi
        call CompleteOriginal
        add esp,24
        mov eax,0x13572468
        mov edx,0x24681357
        stc
        pop edi
        pop esi
        pop ebp
        ret 20
    }
}
__declspec(naked) void SendFixture() {
    __asm {
        push ebp
        mov ebp,esp
        push esi
        push edi
        mov edi,ecx
        mov esi,dword ptr [ebp+8]
        jmp GoldLocalBridge
    }
}
void __stdcall Callback(void* m,const void* e,uint32_t mode) {
    if(m==manager && e==event_object && mode==0)++callbacks;
    SetLastError(0x3333);
    if(reenter)GoldLocalObserver::Observe(m,e,mode);
    if(throw_callback)throw 7;
}
void Reset() {
    callbacks=locks=forwarded=0;throw_callback=reenter=false;correct_lock=correct_arguments=false;lock_error=0;
    main_object[0]=0xf55600;main_object[4]=reinterpret_cast<uint32_t>(manager);main_object[5]=reinterpret_cast<uint32_t>(session);
    manager[0]=0xf566cc;manager[6]=reinterpret_cast<uint32_t>(manager+7);
    auto& state=GoldLocalObserver::Current();state={};state.callback=&Callback;state.owner=GetCurrentThreadId();
    state.manager=reinterpret_cast<uintptr_t>(manager);state.main=reinterpret_cast<uintptr_t>(main_object);state.session=reinterpret_cast<uintptr_t>(session);state.installed=true;
    *reinterpret_cast<uintptr_t*>(0x10f83dc)=reinterpret_cast<uintptr_t>(main_object);
}
bool Run(uint32_t mode,unsigned expected_callbacks,bool expected_failure=false) {
    uintptr_t before=0,after=1;uint32_t result=0,second=0,flags=0,result_bx=0,result_si=0,result_di=0;
    void* m=manager;const void* e=event_object;SetLastError(0x7777);
    __asm {
        push ebx
        push esi
        push edi
        mov before,esp
        mov ebx,0x11112222
        mov esi,0x33334444
        mov edi,0x55556666
        push 0x33333333
        push 0x22222222
        push 0x11111111
        push mode
        push e
        mov ecx,m
        mov edx,0x1234abcd
        push 0x246
        popfd
        call SendFixture
        mov result,eax
        mov second,edx
        pushfd
        pop flags
        mov after,esp
        mov result_bx,ebx
        mov result_si,esi
        mov result_di,edi
        pop edi
        pop esi
        pop ebx
    }
    return before==after && result==0x13572468 && second==0x24681357 && (flags&1) && result_bx==0x11112222 && result_si==0x33334444 && result_di==0x55556666
        && callbacks==expected_callbacks && locks==1 && forwarded==1 && correct_lock && correct_arguments && lock_error==0x7777 && GetLastError()==0x4444
        && entry_eax==0x11223344 && entry_ecx==0x22334455 && entry_edx==0x33445566 && (entry_flags&1)
        && lock_eax==reinterpret_cast<uintptr_t>(manager+7) && lock_ecx==reinterpret_cast<uintptr_t>(manager)
        && lock_edx==0x1234abcd && (lock_flags&0x8d5)==0x44
        && GoldLocalObserver::Current().failed==expected_failure && !GoldLocalObserver::Current().inside;
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayGoldLocalMethodsOracle(uint32_t edition) {
    return edition==1 && GoldLocalObserver::OriginalMethods()?1u:0u;
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayLocalABICase(uint32_t mode,uint32_t variant) {
    if(mode>2 || variant>8)return 0;
    *reinterpret_cast<uintptr_t*>(0xebf04c)=reinterpret_cast<uintptr_t>(&LockThunk);
    unsigned char jump[5]={0xe9};const int32_t distance=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&FinishOriginal)-(0xa4e4d9+5));
    memcpy(jump+1,&distance,4);memcpy(reinterpret_cast<void*>(0xa4e4d9),jump,5);
    Reset();unsigned count=mode==0?1:0;bool failure=false;
    if(variant==1)reenter=true;
    if(variant==2){++main_object[5];count=0;failure=true;}
    if(variant==3){++GoldLocalObserver::Current().owner;count=0;}
    if(variant==4){GoldLocalObserver::Current().failed=true;count=0;failure=true;}
    if(variant==5){GoldLocalObserver::Current().installed=false;count=0;}
    if(variant==6){GoldLocalObserver::Current().callback=nullptr;count=0;}
    if(variant==7){GoldLocalObserver::Current().manager+=4;count=0;}
    if(variant==8){main_object[0]=0;count=0;failure=true;}
    return Run(mode,count,failure)?1u:0u;
}
int main() {
    for(uintptr_t at:{0xa40000u,0xeb0000u,0x10f0000u}) {
        if(!VirtualAlloc(reinterpret_cast<void*>(at),0x10000,MEM_RESERVE|MEM_COMMIT,at==0xa40000?PAGE_EXECUTE_READWRITE:PAGE_READWRITE)) {
            const DWORD error=GetLastError();MEMORY_BASIC_INFORMATION region{};VirtualQuery(reinterpret_cast<void*>(at),&region,sizeof(region));
            std::printf("Fixture address %p allocation failed: %lu module=%p base=%p allocation=%p size=%zx state=%lx type=%lx protect=%lx\n",reinterpret_cast<void*>(at),error,GetModuleHandleW(nullptr),region.BaseAddress,region.AllocationBase,region.RegionSize,region.State,region.Type,region.Protect);return 2;
        }
    }
    *reinterpret_cast<uintptr_t*>(0xebf04c)=reinterpret_cast<uintptr_t>(&LockThunk);
    unsigned char jump[5]={0xe9};const int32_t distance=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&FinishOriginal)-(0xa4e4d9+5));memcpy(jump+1,&distance,4);memcpy(reinterpret_cast<void*>(0xa4e4d9),jump,5);
    unsigned checks=0;auto check=[&](bool okay){++checks;if(!okay){std::printf("FAIL local observer ABI %u\n",checks);exit(3);}};
    Reset();check(Run(0,1));Reset();check(Run(2,0));Reset();check(Run(1,0));
    Reset();reenter=true;check(Run(0,1));Reset();throw_callback=true;check(Run(0,1,true));
    Reset();++main_object[5];check(Run(0,0,true));Reset();++GoldLocalObserver::Current().owner;check(Run(0,0));
    Reset();GoldLocalObserver::Current().failed=true;check(Run(0,0,true));
    Reset();GoldLocalObserver::Current().installed=false;check(Run(0,0));
    Reset();GoldLocalObserver::Current().callback=nullptr;check(Run(0,0));
    Reset();GoldLocalObserver::Current().manager+=4;check(Run(0,0));
    Reset();main_object[0]=0;check(Run(0,0,true));
    check(!WePlayGoldLocalMethodsOracle(0) && !WePlayGoldLocalMethodsOracle(1) && !WePlayGoldLocalMethodsOracle(2));
    std::printf("PASS %u local-branch ABI, once-only forwarding, scope, reentrancy, exception and LastError checks; original lock/queue continuation modeled.\n",checks);
}
