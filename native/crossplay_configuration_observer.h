#pragma once
#include "crossplay_production_observer.h"
#include "crossplay_session_config_codec.h"
#include <array>

// Observe the build-specific original configuration consumer after it returns.
// A queued clone or a pre-existing matching map alone is not this receipt.
namespace CrossplayProduction {
namespace ConfigurationObserver {
struct Profile {uintptr_t table=0,dispatcher=0,event=0;uint64_t fingerprint=0;size_t method_bytes=0xc10,cache_bytes=0x50,event_bytes=0x3c;};
inline Profile Pin(Crossplay::Edition edition){
    if(edition==Crossplay::Edition::Gold)return {0xf4e074,0x98e0f0,0xf4e0c0,0xabb76b3f794cf68bull,0x920,0x60,0x40};
    if(edition==Crossplay::Edition::UbisoftHistory)return {0x114dd44,0x841060,0x114dd0c,0xc0d929e13f402749ull};
    if(edition==Crossplay::Edition::SteamHistory)return {0x114c270,0x840e30,0x114c238,0x7699b03a4264f1c9ull};
    return {};
}
inline Profile profile{};
inline DWORD owner=0;
inline uintptr_t original_dispatch=0;
inline bool installed=false;
inline void (*capture)(void*,const unsigned char*)=nullptr;
inline bool host_closed=false;
inline uintptr_t closed_event=0;
inline void (*capture_closed)(void*,const unsigned char*)=nullptr;
using DispatcherNormalizer=bool (*)(Crossplay::Edition,uintptr_t,unsigned char*,size_t);
inline DispatcherNormalizer normalize_installed_resume=nullptr;
inline bool Methods(Crossplay::Edition edition){
    using CrossplayNativeCodec::Detail::Readable;const auto p=Pin(edition);
    if(!p.table || !Readable(reinterpret_cast<void*>(p.table+4),4)
        || *reinterpret_cast<const uintptr_t*>(p.table+4)!=p.dispatcher
        || !Readable(reinterpret_cast<void*>(p.dispatcher),p.method_bytes))return false;
    uint64_t hash=14695981039346656037ull;
    const auto* code=reinterpret_cast<const unsigned char*>(p.dispatcher);
    for(size_t i=0;i<p.method_bytes;++i)hash=(hash^code[i])*1099511628211ull;
    if(hash==p.fingerprint)return true;
    std::array<unsigned char,0xc10> original{};
    if(!normalize_installed_resume || p.method_bytes>original.size())return false;
    memcpy(original.data(),code,p.method_bytes);
    if(!normalize_installed_resume(edition,p.dispatcher,original.data(),p.method_bytes))return false;
    hash=14695981039346656037ull;
    for(size_t i=0;i<p.method_bytes;++i)hash=(hash^original[i])*1099511628211ull;
    return hash==p.fingerprint;
}
#ifdef S7_PRODUCTION_CONFIGURATION_OBSERVER_TESTING
inline bool test_authorized=false;
#endif
inline void __cdecl Capture(void* self,const unsigned char* event){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    using CrossplayNativeCodec::Detail::Readable;
    const auto* launch=CurrentLaunch();bool authorized=launch && launch->mixed && (host_closed?launch->host:!launch->host);
#ifdef S7_PRODUCTION_CONFIGURATION_OBSERVER_TESTING
    authorized=authorized || test_authorized;
#endif
    if(!installed || !authorized || owner!=GetCurrentThreadId()
        || !Readable(self,profile.cache_bytes) || *static_cast<uintptr_t*>(self)!=profile.table)return;
    if(host_closed){
        if(capture_closed && closed_event && Readable(event,20) && *reinterpret_cast<const uintptr_t*>(event)==closed_event
            && CrossplayChat::Word(event+4)==(7u|(55u<<16)) && event[12]==1)capture_closed(self,event);
        return;
    }
    if(capture_closed && closed_event && Readable(event,20) && *reinterpret_cast<const uintptr_t*>(event)==closed_event
        && CrossplayChat::Word(event+4)==(7u|(55u<<16)) && event[12]==1){capture_closed(self,event);return;}
    if(!capture || !Readable(event,profile.event_bytes) || *reinterpret_cast<const uintptr_t*>(event)!=profile.event
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
inline bool Install(Crossplay::Edition edition,DWORD thread,void (*callback)(void*,const unsigned char*),
                    void (*closed_callback)(void*,const unsigned char*)=nullptr){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    const auto* launch=CurrentLaunch();
    if(installed || thread!=GetCurrentThreadId() || !callback || !launch || !launch->mixed || launch->host
        || launch->channel.edition!=edition || !Methods(edition)
        || !CrossplayNativeCodec::SessionConfigDetail::Methods(edition))return false;
    const auto next=Pin(edition);
    profile=next;owner=thread;capture=callback;host_closed=false;
    closed_event=closed_callback?(edition==Crossplay::Edition::Gold?0xf4df88:edition==Crossplay::Edition::SteamHistory?0x113e9cc:0x1140488):0;
    capture_closed=closed_callback;original_dispatch=next.dispatcher;
    if(!SessionObserver::Exchange(next.table+4,next.dispatcher,reinterpret_cast<uintptr_t>(&Dispatch))){
        profile={};owner=0;capture=nullptr;capture_closed=nullptr;closed_event=0;original_dispatch=0;return false;
    }
    installed=true;return true;
}
// Separate opt-in host consumer receipt. Normal guest configuration observation
// keeps its original role/event/size policy and callback.
inline bool InstallHostClosed(Crossplay::Edition edition,DWORD thread,void (*callback)(void*,const unsigned char*)){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    const auto* launch=CurrentLaunch();
    if(installed || thread!=GetCurrentThreadId() || !callback || !launch || !launch->mixed || !launch->host
        || launch->channel.edition!=edition || edition!=Crossplay::Edition::SteamHistory || !Methods(edition)
        || !CrossplayNativeCodec::SessionConfigDetail::Methods(edition))return false;
    const auto next=Pin(edition);profile=next;owner=thread;capture=nullptr;host_closed=true;closed_event=0x113e9cc;
    capture_closed=callback;original_dispatch=next.dispatcher;
    if(!SessionObserver::Exchange(next.table+4,next.dispatcher,reinterpret_cast<uintptr_t>(&Dispatch))){
        profile={};owner=0;capture_closed=nullptr;host_closed=false;closed_event=0;original_dispatch=0;return false;
    }
    installed=true;return true;
}
inline bool Release(){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    if(!installed || owner!=GetCurrentThreadId()
        || !SessionObserver::Exchange(profile.table+4,reinterpret_cast<uintptr_t>(&Dispatch),original_dispatch))return false;
    installed=false;profile={};owner=0;capture=nullptr;capture_closed=nullptr;host_closed=false;closed_event=0;original_dispatch=0;return true;
}
}
}
