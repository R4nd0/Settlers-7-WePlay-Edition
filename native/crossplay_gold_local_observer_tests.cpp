#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_gold_local_observer.h"
#include <cstdio>
namespace CrossplayProduction {const Launch* CurrentLaunch(){return nullptr;}}
using CrossplayNativeObserver::GoldLocalObserver;
using CrossplayNativeObserver::GoldLocalBridge;
namespace {
uint32_t manager[8]{},main_object[8]{},event_object[8]{},session[8]{};
unsigned callbacks=0,locks=0,forwarded=0;bool throw_callback=false,reenter=false;
DWORD lock_error=0;uint32_t entry_eax=0,entry_ecx=0,entry_edx=0,entry_flags=0;bool correct_lock=false,correct_arguments=false;
uint32_t lock_eax=0,lock_ecx=0,lock_edx=0,lock_flags=0;
bool direction=false,fp=false;__declspec(align(16)) unsigned char saved_fp[512]{};
void __cdecl LockOriginal(void* lock) {
    unsigned short control=0;uint32_t mxcsr=0,xmm=0;double value=0;
    __asm {fnstcw control
        stmxcsr mxcsr
        fst qword ptr [value]
        movd xmm,xmm2}
    fp=control==0x27f && mxcsr==0x3f80 && value==7.25 && xmm==0x3fa00000;
    ++locks;correct_lock=lock==manager+7;lock_error=GetLastError();SetLastError(0x5555);
}
__declspec(naked) void LockThunk() {
    __asm {
        mov lock_eax,eax
        mov lock_ecx,ecx
        mov lock_edx,edx
        pushfd
        pop lock_flags
        cld
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
    __asm {fninit
        pxor xmm2,xmm2}
    _mm_setcsr(0x5f80);
    if(throw_callback)throw 7;
}
void Reset() {
    callbacks=locks=forwarded=0;throw_callback=reenter=direction=fp=false;correct_lock=correct_arguments=false;lock_error=0;
    main_object[0]=0xf55600;main_object[4]=reinterpret_cast<uint32_t>(manager);main_object[5]=reinterpret_cast<uint32_t>(session);
    manager[0]=0xf566cc;manager[6]=reinterpret_cast<uint32_t>(manager+7);
    auto& state=GoldLocalObserver::Current();state={};state.callback=&Callback;state.owner=GetCurrentThreadId();
    state.manager=reinterpret_cast<uintptr_t>(manager);state.main=reinterpret_cast<uintptr_t>(main_object);state.session=reinterpret_cast<uintptr_t>(session);state.installed=true;
    *reinterpret_cast<uintptr_t*>(0x10f83dc)=reinterpret_cast<uintptr_t>(main_object);
}
bool Run(uint32_t mode,unsigned expected_callbacks,bool expected_failure=false) {
    uintptr_t before=0,after=1;uint32_t result=0,second=0,flags=0,result_bx=0,result_si=0,result_di=0;
    void* m=manager;const void* e=event_object;SetLastError(0x7777);
    const unsigned short control=0x27f;const uint32_t mxcsr=0x3f80,xmm=0x3fa00000;
    const double value=7.25;const uint32_t incoming_flags=direction?0x646:0x246;
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
        push 0x33333333
        push 0x22222222
        push 0x11111111
        push mode
        push e
        mov ecx,m
        mov edx,0x1234abcd
        push incoming_flags
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
        fxrstor saved_fp
    }
    return before==after && result==0x13572468 && second==0x24681357 && (flags&1) && result_bx==0x11112222 && result_si==0x33334444 && result_di==0x55556666
        && callbacks==expected_callbacks && locks==1 && forwarded==1 && correct_lock && correct_arguments && fp && lock_error==0x7777 && GetLastError()==0x4444
        && entry_eax==0x11223344 && entry_ecx==0x22334455 && entry_edx==0x33445566 && (entry_flags&1)
        && lock_eax==reinterpret_cast<uintptr_t>(manager+7) && lock_ecx==reinterpret_cast<uintptr_t>(manager)
        && lock_edx==0x1234abcd && (lock_flags&0xcd5)==(direction?0x444u:0x44u)
        && GoldLocalObserver::Current().failed==expected_failure && !GoldLocalObserver::Current().inside;
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayGoldLocalMethodsOracle(uint32_t edition) {
    return edition==1 && GoldLocalObserver::OriginalMethods()?1u:0u;
}
namespace {void IgnoreOutgoing(void*,const void*,uint32_t,uint32_t){}}
// Offline-only: the verifier supplies the fingerprinted original mapped image.
// This exercises composition against actual bytes without executing game code.
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayGoldLocalCompositeCase(uint32_t variant){
    if(variant>8)return 0;
    namespace O=CrossplayProduction::OutgoingObserver;
    O::profile=O::Pin(Crossplay::Edition::Gold);O::owner=GetCurrentThreadId();O::manager=0x1234;
    O::callback=&IgnoreOutgoing;O::installed=true;O::inside=O::failed=false;
    for(unsigned i=0;i<3;++i){
        const auto at=O::profile.sites[i];O::replacements[i]=O::Bridge(O::profile,i);
        const int32_t relative=static_cast<int32_t>(O::replacements[i]-at-5);
        memcpy(reinterpret_cast<void*>(at+1),&relative,4);
    }
    if(variant==1)*reinterpret_cast<unsigned char*>(O::profile.sites[0]+1)^=1;
    if(variant==2)*reinterpret_cast<unsigned char*>(O::profile.dispatcher)^=1;
    if(variant==3)++O::owner;
    if(variant==4)O::manager=0;
    if(variant==5)O::callback=nullptr;
    if(variant==6)O::failed=true;
    if(variant==7)O::inside=true;
    if(variant==8)++O::replacements[0];
    return GoldLocalObserver::OriginalMethods()==(variant==0)?1u:0u;
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayLocalABICase(uint32_t mode,uint32_t variant) {
    if(mode>2 || variant>9)return 0;
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
    if(variant==9)direction=true;
    return Run(mode,count,failure)?1u:0u;
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayLocalReleaseCase(uint32_t variant){
    if(variant>5)return 0;Reset();auto* at=reinterpret_cast<unsigned char*>(0xa4e4cf);
    unsigned char patch[]={0xe9,0,0,0,0,0x90,0x90,0x90,0x90,0x90};
    const int32_t relative=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&GoldLocalBridge)-(0xa4e4cf+5));
    memcpy(patch+1,&relative,4);memcpy(at,patch,10);auto& state=GoldLocalObserver::Current();
    if(variant==1)state.inside=true;
    if(variant==2)++state.owner;
    if(variant==3)at[1]^=1;
    if(variant==4)at[9]=0xcc;
    if(variant==5)state.installed=false;
    unsigned char before[10]{};memcpy(before,at,10);SetLastError(0x7777);
    const bool released=GoldLocalObserver::Release();
    const unsigned char original[]={0x8b,0x47,0x18,0x50,0xff,0x15,0x4c,0xf0,0xeb,0x00};
    return GetLastError()==0x7777 && (variant?(!released && !memcmp(at,before,10))
        :(released && !state.installed && !memcmp(at,original,10)))?1u:0u;
}
int main() {
    for(uintptr_t at:{0xa40000u,0xeb0000u,0x10f0000u}) {
        if(!VirtualAlloc(reinterpret_cast<void*>(at),0x10000,MEM_RESERVE|MEM_COMMIT,at==0xa40000?PAGE_EXECUTE_READWRITE:PAGE_READWRITE)) {
            const DWORD error=GetLastError();MEMORY_BASIC_INFORMATION region{};VirtualQuery(reinterpret_cast<void*>(at),&region,sizeof(region));
            std::printf("SKIP Windows fixture: modeled address %p occupied (%lu); module=%p base=%p allocation=%p size=%zx state=%lx type=%lx protect=%lx. Use offline fixture.\n",reinterpret_cast<void*>(at),error,GetModuleHandleW(nullptr),region.BaseAddress,region.AllocationBase,region.RegionSize,region.State,region.Type,region.Protect);return 77;
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
    Reset();direction=true;check(Run(0,1));
    check(!WePlayGoldLocalMethodsOracle(0) && !WePlayGoldLocalMethodsOracle(1) && !WePlayGoldLocalMethodsOracle(2));
    for(uint32_t variant=0;variant<=5;++variant)check(WePlayLocalReleaseCase(variant)==1);
    std::printf("PASS %u local-branch ABI, once-only forwarding, scope, reentrancy, exception and LastError checks; original lock/queue continuation modeled.\n",checks);
}
