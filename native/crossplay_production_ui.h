#pragma once
#include "crossplay_production_runtime.h"
#include "crossplay_load_progress_codec.h"
namespace CrossplayProduction {
// Read-only Lua UI metadata. These strings grant neither native readiness nor
// start permission and expose no identity, profile, endpoint or secret.
inline bool UiCommand(const std::string& command,std::string& output){
    output.clear();const auto* launch=CurrentLaunch();if(!launch)return false;
    if(command=="mode"){output=launch->mixed?(launch->host?"host":"guest"):"solo";return true;}
    if(command=="bootstrap-map"){
        if(!launch->mixed || launch->host)return false;
        output=BootstrapMap();return !output.empty();
    }
    if(command=="bootstrap-flags" || command=="bootstrap-template"){
        uint32_t flags=0;std::string map_template;
        if(!launch->mixed || launch->host || !BootstrapChoices(flags,map_template))return false;
        output=command=="bootstrap-flags"?std::to_string(flags):std::move(map_template);return true;
    }
    if(command!="phase")return false;
    switch(RuntimePhase()){
    case Phase::Dormant:output="pending";break;
    case Phase::LobbyCaptured:output="local-lobby";break;
    case Phase::Connecting:output="connecting";break;
    case Phase::Admitting:output="admitting";break;
    case Phase::RosterBound:output="roster-bound";break;
    case Phase::Closed:output="closed";break;
    case Phase::Failed:output="failed";break;
    }
    return true;
}
namespace UiConsole {
struct Profile {uintptr_t entry=0,push_boolean=0,push_string=0;bool gold=false;};
inline Profile Pin(Crossplay::Edition edition){
    if(edition==Crossplay::Edition::Gold)return {0x9842a0,0xbd5070,0xbe4e60,true};
    if(edition==Crossplay::Edition::UbisoftHistory)return {0x5d1550,0x44c6e0,0x45b790,false};
    if(edition==Crossplay::Edition::SteamHistory)return {0x5d0fb0,0x44c6f0,0x45b7a0,false};
    return {};
}
inline Profile profile{};
inline DWORD owner=0;
inline uintptr_t resume=0;
inline bool installed=false;
inline unsigned char before[6]{},after[6]{};
inline bool Word(uintptr_t at,uint32_t& value){
    if(at<0x10000 || at>UINT32_MAX-3 || !CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(at),4))return false;
    memcpy(&value,reinterpret_cast<void*>(at),4);return true;
}
#ifdef S7_PRODUCTION_UI_TESTING
inline void (*test_push_string)(void*,const char*,size_t)=nullptr;
inline void (*test_push_boolean)(void*,int)=nullptr;
#endif
inline void PushString(void* state,const std::string& value){
#ifdef S7_PRODUCTION_UI_TESTING
    if(test_push_string){test_push_string(state,value.data(),value.size());return;}
#endif
    reinterpret_cast<void (__cdecl*)(void*,const char*,size_t)>(profile.push_string)(state,value.data(),value.size());
}
inline void PushBoolean(void* state,bool value){
#ifdef S7_PRODUCTION_UI_TESTING
    if(test_push_boolean){test_push_boolean(state,value?1:0);return;}
#endif
    reinterpret_cast<void (__cdecl*)(void*,int)>(profile.push_boolean)(state,value?1:0);
}
inline int __cdecl Handle(void* state){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    if(!installed || owner!=GetCurrentThreadId() || !state || !CurrentLaunch())return 0;
    const auto l=reinterpret_cast<uintptr_t>(state);uint32_t base=0,top=0,tag=0,string=0,length=0;
    if(l>UINT32_MAX-0x18 || !Word(l+0x10,base) || !Word(l+0x14,top) || base>UINT32_MAX-8 || top!=base+8
        || !Word(base+4,tag) || tag!=0xfffffffb || !Word(base,string) || string>UINT32_MAX-16
        || !Word(static_cast<uintptr_t>(string)+12,length))return 0;
    constexpr char prefix[]="weplay-crossplay-v1:";constexpr size_t prefix_size=sizeof(prefix)-1;
    if(length<prefix_size || length>80 || string>UINT32_MAX-16-length
        || !CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(string+16),length+1))return 0;
    const auto* text=reinterpret_cast<const char*>(string+16);
    if(text[length] || memcmp(text,prefix,prefix_size))return 0;
    bool okay=false;std::string output;
    if(!memchr(text,0,length))try{okay=UiCommand(std::string(text+prefix_size,length-prefix_size),output);}catch(...){okay=false;}
    if(okay)PushString(state,output);else PushBoolean(state,false);
    return 1;
}
__declspec(naked) inline void GoldBridge(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push dword ptr [edi+40]
    call Handle
    add esp,4
    test eax,eax
    jz original
    mov [edi+28],eax
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    ret
original:
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    push ebp
    mov ebp,esp
    and esp,0xfffffff8
    jmp dword ptr [resume]
}}
__declspec(naked) inline void HistoryBridge(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push dword ptr [edi+40]
    call Handle
    add esp,4
    test eax,eax
    jz original
    mov [edi+28],eax
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    ret
original:
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    push ebp
    mov ebp,esp
    mov ecx,[ebp+8]
    jmp dword ptr [resume]
}}
inline uint64_t Hash(uintptr_t at,unsigned size){
    if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(at),size))return 0;
    uint64_t result=14695981039346656037ull;
    for(unsigned i=0;i<size;++i)result=(result^reinterpret_cast<const unsigned char*>(at)[i])*1099511628211ull;
    return result;
}
inline bool Methods(Crossplay::Edition edition){
    const auto p=Pin(edition);if(!p.entry)return false;
    if(Hash(p.push_boolean,52)!=(p.gold?0x350fe20e2467c638ull:0x1e662574076b4968ull)
        || Hash(p.push_string,83)!=(p.gold?0xd96b319ce9571b3full:0x22f1e54a41cf813eull)
        || Hash(p.entry,p.gold?65:12)!=(p.gold?0x294cae055aca0f9cull:0x9849fbcb94dae3bull))return false;
    if(!p.gold)return true;
    uint32_t registration=0;
    return Word(0x10abdf4,registration) && (!registration || registration==0x10abde4)
        && Hash(0x10abde8,12)==0xfc462d47493c0f35ull && Hash(0xf4d2ec,15)==0xf39f0463db4d00ceull
        && Hash(0xbe4c90,96)==0x3e9b6b3c5e1b6edeull && Hash(0xe7f6c0,58)==0x9c9ec1b87290f1eaull
        && Hash(0xe7f700,58)==0x72143618878b6ceaull;
}
// Full game-image binding must precede this call in the protected initializer.
inline bool Install(Crossplay::Edition edition,DWORD thread){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    const auto* launch=CurrentLaunch();
    if(installed || thread!=GetCurrentThreadId() || !launch || (launch->mixed && launch->channel.edition!=edition) || !Methods(edition))return false;
    profile=Pin(edition);owner=thread;resume=profile.entry+6;
    memcpy(before,reinterpret_cast<void*>(profile.entry),6);memset(after,0,sizeof(after));after[0]=0xe9;after[5]=0x90;
    const auto target=profile.gold?&GoldBridge:&HistoryBridge;
    const auto displacement=static_cast<int32_t>(reinterpret_cast<uintptr_t>(target)-(profile.entry+5));memcpy(after+1,&displacement,4);
    DWORD previous=0,ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(profile.entry),6,PAGE_EXECUTE_READWRITE,&previous))return false;
    memcpy(reinterpret_cast<void*>(profile.entry),after,6);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(profile.entry),6)!=FALSE;
    const bool restored=VirtualProtect(reinterpret_cast<void*>(profile.entry),6,previous,&ignored)!=FALSE;
    if(!flushed || !restored)__fastfail(7);
    installed=true;return true;
}
}
}
