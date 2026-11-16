#define S7_PRODUCTION_OBSERVER_TESTING
#include "crossplay_production_observer.h"
#include <cstdio>
#include <cstdlib>
namespace {
namespace O=CrossplayProduction::SessionObserver;
unsigned checks=0,calls=0;bool abi=false;unsigned desired_df=0;
uint32_t self[16]{},event[40]{},seen_self=0,seen_event=0;
uint32_t native_self=0,native_event=0,native_flags=0,returned_flags=0,result[6]{},before_sp=0,after_sp=0;
uintptr_t target=0;
alignas(16) unsigned char original_fp[512]{},native_fp[512]{},returned_fp[512]{};
void Check(bool value){++checks;if(!value){printf("FAIL production observer check %u\n",checks);ExitProcess(1);}}
void Capture(void* object,const unsigned char* message){
    ++calls;seen_self=reinterpret_cast<uintptr_t>(object);seen_event=reinterpret_cast<uintptr_t>(message);
    if(abi){SetLastError(0xdeadbeef);__asm{fninit
        fldpi
        pxor xmm0,xmm0
        pxor xmm1,xmm1
        pxor xmm7,xmm7
    }}
}
void Setup(){
    memset(self,0,sizeof(self));memset(event,0,sizeof(event));calls=0;abi=false;
    O::profile={0x11111111,0,0x22222222,0x9c,0x98,true};self[0]=0x11111111;event[0]=0x22222222;
    reinterpret_cast<unsigned short*>(event)[3]=3;reinterpret_cast<unsigned char*>(event)[0x98]=1;
    O::owner=GetCurrentThreadId();O::installed=true;O::capture=&Capture;O::test_authorized=true;
}
__declspec(naked) void Original(){__asm{
    mov [native_self],ecx
    mov eax,dword ptr [esp+4]
    mov [native_event],eax
    fninit
    fld1
    pcmpeqd xmm0,xmm0
    pcmpeqd xmm1,xmm1
    pcmpeqd xmm7,xmm7
    fxsave [native_fp]
    cmp desired_df,0
    je clear_df
    std
    jmp flags_ready
clear_df:
    cld
flags_ready:
    xor eax,eax
    cmp eax,eax
    pushfd
    pop dword ptr [native_flags]
    mov eax,0xfaceb00c
    mov ecx,0x11223344
    mov edx,0x55667788
    ret 4
}}
__declspec(naked) void Invoke(){__asm{
    push ebp
    mov ebp,esp
    push ebx
    push esi
    push edi
    fxsave [original_fp]
    mov [before_sp],esp
    mov ebx,0x33333333
    mov esi,0x66666666
    mov edi,0x77777777
    lea ecx,[self]
    push offset event
    call dword ptr [target]
    pushfd
    pop dword ptr [returned_flags]
    mov [after_sp],esp
    mov [result],eax
    mov [result+4],ecx
    mov [result+8],edx
    mov [result+12],ebx
    mov [result+16],esi
    mov [result+20],edi
    fxsave [returned_fp]
    fxrstor [original_fp]
    cld
    pop edi
    pop esi
    pop ebx
    mov esp,ebp
    pop ebp
    ret
}}
}
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionObserverGuardOracle(uint32_t edition){return O::Methods(static_cast<Crossplay::Edition>(edition))?1u:0u;}
int main(){
    Setup();O::Capture(self,reinterpret_cast<const unsigned char*>(event));Check(calls==1);
    for(unsigned which=0;which<8;++which){
        Setup();
        if(which==0)O::installed=false;if(which==1)++O::owner;if(which==2)O::test_authorized=false;
        if(which==3)O::capture=nullptr;if(which==4)self[0]^=1;if(which==5)event[0]^=1;
        if(which==6)reinterpret_cast<unsigned short*>(event)[3]=2;
        if(which==7)reinterpret_cast<unsigned char*>(event)[0x98]=0;
        SetLastError(1909);O::Capture(self,reinterpret_cast<const unsigned char*>(event));Check(!calls&&GetLastError()==1909);
    }
    Setup();O::Capture(nullptr,reinterpret_cast<const unsigned char*>(event));O::Capture(self,nullptr);Check(!calls);
    for(unsigned df=0;df<2;++df){
        Setup();abi=true;desired_df=df;O::original_dispatch=reinterpret_cast<uintptr_t>(&Original);target=reinterpret_cast<uintptr_t>(&O::Dispatch);
        SetLastError(0xabcdef12);Invoke();
        Check(calls==1&&native_self==reinterpret_cast<uintptr_t>(self)&&native_event==reinterpret_cast<uintptr_t>(event));
        Check(seen_self==native_self&&seen_event==native_event&&before_sp==after_sp);
        Check(result[0]==0xfaceb00c&&result[1]==0x11223344&&result[2]==0x55667788
            &&result[3]==0x33333333&&result[4]==0x66666666&&result[5]==0x77777777);
        Check(native_flags==returned_flags&&GetLastError()==0xabcdef12&&memcmp(native_fp,returned_fp,288)==0);
    }
    uintptr_t table[2]{};Setup();O::profile.table=reinterpret_cast<uintptr_t>(table);
    O::original_dispatch=reinterpret_cast<uintptr_t>(&Original);table[1]=reinterpret_cast<uintptr_t>(&O::Dispatch);
    table[1]^=1;Check(!O::Release()&&O::installed);table[1]^=1;
    Check(O::Release()&&table[1]==reinterpret_cast<uintptr_t>(&Original)&&!O::installed&&!O::capture);
    Check(!O::Release());
    printf("PASS %u production session observer checks: owned local Confirmed only; original result/stack/registers/flags/LastError/x87/SSE/DF preserved; foreign-hook release excluded.\n",checks);
}
