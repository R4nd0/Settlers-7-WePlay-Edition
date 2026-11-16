#define WIN32_LEAN_AND_MEAN
#define S7_PRODUCTION_UI_TESTING
#include <windows.h>
#include "crossplay_production_ui.h"
#include <cstdio>
namespace {
namespace U=CrossplayProduction::UiConsole;
unsigned checks=0,pushes=0,desired_df=0;
uint32_t lua[8]{},argument[2]{},before_sp=0,after_sp=0,input_flags=0,original_flags=0,after_flags=0,result[6]{};
alignas(16) unsigned char text[128]{},original_fp[512]{},before_fp[512]{},after_fp[512]{};
uintptr_t target=0;
void Check(bool okay){++checks;if(!okay){printf("FAIL UI bridge check %u\n",checks);ExitProcess(1);}}
void PushString(void* state,const char* value,size_t size){
    Check(state==lua&&std::string(value,size)=="host");++pushes;SetLastError(0xdeadbeef);
    __asm{fninit
        fldpi
        pxor xmm0,xmm0
        pxor xmm7,xmm7
    }
}
void PushBoolean(void*,int){Check(false);}
__declspec(naked) void Continue(){__asm{
    pushfd
    pop dword ptr [original_flags]
    mov eax,7
    mov esp,ebp
    pop ebp
    ret
}}
__declspec(naked) void Invoke(){__asm{
    push ebp
    mov ebp,esp
    push ebx
    push esi
    push edi
    fxsave [original_fp]
    fninit
    fld1
    pcmpeqd xmm0,xmm0
    pcmpeqd xmm7,xmm7
    fxsave [before_fp]
    mov [before_sp],esp
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
    pop dword ptr [input_flags]
    push offset lua
    mov eax,0x11111111
    mov ecx,0x22222222
    mov edx,0x33333333
    mov ebx,0x44444444
    mov esi,0x66666666
    mov edi,0x77777777
    call dword ptr [target]
    pushfd
    pop dword ptr [after_flags]
    mov [result],eax
    mov [result+4],ecx
    mov [result+8],edx
    mov [result+12],ebx
    mov [result+16],esi
    mov [result+20],edi
    add esp,4
    mov [after_sp],esp
    fxsave [after_fp]
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
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionUiGuardOracle(uint32_t edition){return U::Methods(static_cast<Crossplay::Edition>(edition))?1u:0u;}
int main(){
    using namespace CrossplayProduction;
    std::string metadata="stale";uint32_t flags=99;
    Check(!AppliedGuestChoices(flags,metadata)&&!flags&&metadata.empty());
    for(const char* command:{"applied-flags","applied-template"}){
        metadata="stale";Check(!UiCommand(command,metadata)&&metadata.empty());
    }
    const std::pair<const wchar_t*,std::wstring> values[]={
        {L"WEPLAY_CROSSPLAY_V1",L"1"},{L"WEPLAY_CROSSPLAY_V1_ROLE",L"host"},{L"WEPLAY_CROSSPLAY_V1_PEER",L"2"},
        {L"WEPLAY_CROSSPLAY_V1_PORT",L"32000"},{L"WEPLAY_CROSSPLAY_V1_SESSION",std::wstring(32,L'1')},
        {L"WEPLAY_CROSSPLAY_V1_KEY",std::wstring(64,L'2')},{L"WEPLAY_CROSSPLAY_V1_IDENTITY",L"01"+std::wstring(30,L'0')},
        {L"WEPLAY_CROSSPLAY_V1_CONTENT",std::wstring(64,L'3')},{L"WEPLAY_CROSSPLAY_V1_NAME",L"Host"},
        {L"WEPLAY_CROSSPLAY_V1_PROFILE",L"Z:\\Fixture\\Profiles"},{L"WEPLAY_CROSSPLAY_V1_GOLD_AUTH_PORT",L"32001"},
        {L"WEPLAY_CROSSPLAY_V1_GOLD_CONFIG_PORT",L"32002"}};
    for(const auto& value:values)Check(SetEnvironmentVariableW(value.first,value.second.c_str())!=FALSE);
    Crossplay::Digest executable{};executable[0]=1;Check(PrepareLaunch(Crossplay::Edition::Gold,executable,L"Z:\\Fixture\\Profiles"));
    for(const auto& value:values)Check(SetEnvironmentVariableW(value.first,nullptr)!=FALSE);
    for(const char* command:{"applied-flags","applied-template"}){
        metadata="stale";Check(!UiCommand(command,metadata)&&metadata.empty());
    }
    U::installed=true;U::owner=GetCurrentThreadId();U::test_push_string=&PushString;U::test_push_boolean=&PushBoolean;
    lua[4]=reinterpret_cast<uintptr_t>(argument);lua[5]=lua[4]+8;argument[0]=reinterpret_cast<uintptr_t>(text);argument[1]=0xfffffffb;
    U::resume=reinterpret_cast<uintptr_t>(&Continue);
    for(unsigned history=0;history<2;++history)for(unsigned route=0;route<2;++route)for(unsigned df=0;df<2;++df){
        const std::string command=route?"weplay-crossplay-v1:mode":"ordinary console output";
        memset(text,0,sizeof(text));const auto length=static_cast<uint32_t>(command.size());memcpy(text+12,&length,4);memcpy(text+16,command.data(),command.size());
        U::profile=U::Pin(history?Crossplay::Edition::UbisoftHistory:Crossplay::Edition::Gold);
        target=reinterpret_cast<uintptr_t>(history?&U::HistoryBridge:&U::GoldBridge);pushes=0;desired_df=df;
        SetLastError(0xabcdef12);Invoke();
        Check(before_sp==after_sp&&GetLastError()==0xabcdef12&&memcmp(before_fp,after_fp,288)==0);
        Check(result[0]==(route?1u:7u)&&result[2]==0x33333333&&result[3]==0x44444444&&result[4]==0x66666666&&result[5]==0x77777777);
        Check(result[1]==(!route&&history?reinterpret_cast<uintptr_t>(lua):0x22222222));
        Check(after_flags==(route?input_flags:original_flags)&&pushes==route);
    }
    printf("PASS %u production UI bridge checks: both original/handled routes, Gold/History prefixes, DF0/1, exact stack/register/flags/LastError/x87/SSE and readonly output.\n",checks);
}
