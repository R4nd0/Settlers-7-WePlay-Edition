#pragma once
#include "crossplay_production_observer.h"
#include "crossplay_session_config_codec.h"

// Observe the original History configuration consumer after it returns.
// A queued clone or a pre-existing matching map alone is not this receipt.
namespace CrossplayProduction {
namespace ConfigurationObserver {
struct Profile {uintptr_t table=0,dispatcher=0,event=0;uint64_t fingerprint=0;};
inline Profile Pin(Crossplay::Edition edition){
    if(edition==Crossplay::Edition::UbisoftHistory)return {0x114dd44,0x841060,0x114dd0c,0xc0d929e13f402749ull};
    if(edition==Crossplay::Edition::SteamHistory)return {0x114c270,0x840e30,0x114c238,0x7699b03a4264f1c9ull};
    return {};
}
inline Profile profile{};
inline DWORD owner=0;
inline uintptr_t original_dispatch=0;
inline bool installed=false;
inline void (*capture)(void*,const unsigned char*)=nullptr;
inline bool Methods(Crossplay::Edition edition){
    using CrossplayNativeCodec::Detail::Readable;const auto p=Pin(edition);
    if(!p.table || !Readable(reinterpret_cast<void*>(p.table+4),4)
        || *reinterpret_cast<const uintptr_t*>(p.table+4)!=p.dispatcher
        || !Readable(reinterpret_cast<void*>(p.dispatcher),0xc10))return false;
    uint64_t hash=14695981039346656037ull;
    const auto* code=reinterpret_cast<const unsigned char*>(p.dispatcher);
    for(size_t i=0;i<0xc10;++i)hash=(hash^code[i])*1099511628211ull;
    return hash==p.fingerprint;
}
#ifdef S7_PRODUCTION_CONFIGURATION_OBSERVER_TESTING
inline bool test_authorized=false;
#endif
inline void __cdecl Capture(void* self,const unsigned char* event){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    using CrossplayNativeCodec::Detail::Readable;
    const auto* launch=CurrentLaunch();bool authorized=launch && launch->mixed && !launch->host;
#ifdef S7_PRODUCTION_CONFIGURATION_OBSERVER_TESTING
    authorized=authorized || test_authorized;
#endif
    if(!installed || !authorized || owner!=GetCurrentThreadId() || !capture
        || !Readable(self,0x50) || *static_cast<uintptr_t*>(self)!=profile.table
        || !Readable(event,0x3c) || *reinterpret_cast<const uintptr_t*>(event)!=profile.event
        || CrossplayChat::Word(event+4)!=CrossplaySessionConfig::Type || event[12]!=1)return;
    capture(self,event);
}
__declspec(naked) inline void Dispatch(){__asm{
    push ebp
    mov ebp,esp
    sub esp,4
    mov dword ptr [ebp-4],ecx
    push dword ptr [ebp+8]
    call dword ptr [original_dispatch]
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push dword ptr [ebp+8]
    push dword ptr [ebp-4]
    call Capture
    add esp,8
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    mov esp,ebp
    pop ebp
    ret 4
}}
inline bool Install(Crossplay::Edition edition,DWORD thread,void (*callback)(void*,const unsigned char*)){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    const auto* launch=CurrentLaunch();
    if(installed || thread!=GetCurrentThreadId() || !callback || !launch || !launch->mixed || launch->host
        || launch->channel.edition!=edition || !Methods(edition)
        || !CrossplayNativeCodec::SessionConfigDetail::Methods(edition))return false;
    const auto next=Pin(edition);
    profile=next;owner=thread;capture=callback;original_dispatch=next.dispatcher;
    if(!SessionObserver::Exchange(next.table+4,next.dispatcher,reinterpret_cast<uintptr_t>(&Dispatch))){
        profile={};owner=0;capture=nullptr;original_dispatch=0;return false;
    }
    installed=true;return true;
}
inline bool Release(){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    if(!installed || owner!=GetCurrentThreadId()
        || !SessionObserver::Exchange(profile.table+4,reinterpret_cast<uintptr_t>(&Dispatch),original_dispatch))return false;
    installed=false;profile={};owner=0;capture=nullptr;original_dispatch=0;return true;
}
}
}
