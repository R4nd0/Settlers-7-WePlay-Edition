#pragma once
#include "crossplay_event_codec.h"
#include "crossplay_production_context.h"
// Observe the original dispatcher after it consumes its own event. Preserve
// its complete result, including x87/SSE and LastError; never dispatch directly
// from a socket or patch native event bytes in this observer.
namespace CrossplayProduction {
namespace SessionObserver {
struct Profile {uintptr_t table=0,dispatcher=0,participant=0;size_t event_bytes=0,local_at=0;bool gold=false;};
inline Profile Pin(Crossplay::Edition edition){
    if(edition==Crossplay::Edition::Gold)return {0xf569bc,0xa510e0,0xf56238,0x9c,0x98,true};
    if(edition==Crossplay::Edition::UbisoftHistory)return {0x11403fc,0xed9160,0x113f2f4,0x94,0x90,false};
    if(edition==Crossplay::Edition::SteamHistory)return {0x113e924,0xed8850,0x113d814,0x94,0x90,false};
    return {};
}
inline Profile profile{};
inline DWORD owner=0;
inline uintptr_t original_dispatch=0;
inline bool installed=false;
inline void (*capture)(void*,const unsigned char*)=nullptr;
inline void (*peer_capture)(void*,const unsigned char*)=nullptr;
inline bool Methods(Crossplay::Edition edition){
    using CrossplayNativeCodec::Detail::Readable;const auto p=Pin(edition);
    const unsigned char gold[]={0x55,0x8b,0xec,0x83,0xe4,0xf8,0x6a,0xff};
    const unsigned char history[]={0x55,0x8b,0xec,0x6a,0xff};
    return p.table && Readable(reinterpret_cast<void*>(p.table+4),4)
        && *reinterpret_cast<const uintptr_t*>(p.table+4)==p.dispatcher
        && Readable(reinterpret_cast<void*>(p.dispatcher),p.gold?sizeof(gold):sizeof(history))
        && !memcmp(reinterpret_cast<void*>(p.dispatcher),p.gold?gold:history,p.gold?sizeof(gold):sizeof(history));
}
#ifdef S7_PRODUCTION_OBSERVER_TESTING
inline bool test_authorized=false;
#endif
inline void __cdecl Capture(void* self,const unsigned char* event){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    using CrossplayNativeCodec::Detail::Readable;
    const auto* launch=CurrentLaunch();bool authorized=launch && launch->mixed;
#ifdef S7_PRODUCTION_OBSERVER_TESTING
    authorized=authorized || test_authorized;
#endif
    if(!installed || !authorized || GetCurrentThreadId()!=owner || !capture
        || !Readable(self,0x30) || *static_cast<uintptr_t*>(self)!=profile.table
        || !Readable(event,profile.event_bytes) || *reinterpret_cast<const uintptr_t*>(event)!=profile.participant
        || *reinterpret_cast<const uint16_t*>(event+6)!=3)return;
    if(event[profile.local_at]==1)capture(self,event);
    else if(!event[profile.local_at] && peer_capture && *reinterpret_cast<const uint32_t*>(event+4)==(7u|(3u<<16)))peer_capture(self,event);
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
inline bool Exchange(uintptr_t at,uintptr_t expected,uintptr_t replacement){
    using CrossplayNativeCodec::Detail::Readable;
    if(!Readable(reinterpret_cast<void*>(at),4) || *reinterpret_cast<const uintptr_t*>(at)!=expected)return false;
    DWORD previous=0,ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(at),4,PAGE_READWRITE,&previous))return false;
    const auto prior=InterlockedCompareExchange(reinterpret_cast<volatile LONG*>(at),static_cast<LONG>(replacement),static_cast<LONG>(expected));
    if(!VirtualProtect(reinterpret_cast<void*>(at),4,previous,&ignored))__fastfail(7);
    return static_cast<uintptr_t>(prior)==expected;
}
inline bool Install(Crossplay::Edition edition,DWORD thread,void (*callback)(void*,const unsigned char*),
                    void (*peer_callback)(void*,const unsigned char*)=nullptr){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    const auto* launch=CurrentLaunch();
    if(installed || thread!=GetCurrentThreadId() || !callback || !launch || !launch->mixed
        || launch->channel.edition!=edition || !Methods(edition))return false;
    profile=Pin(edition);owner=thread;capture=callback;peer_capture=peer_callback;original_dispatch=profile.dispatcher;
    if(!Exchange(profile.table+4,profile.dispatcher,reinterpret_cast<uintptr_t>(&Dispatch))){owner=0;capture=nullptr;peer_capture=nullptr;original_dispatch=0;profile={};return false;}
    installed=true;return true;
}
inline bool Release(){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    if(!installed || owner!=GetCurrentThreadId()
        || !Exchange(profile.table+4,reinterpret_cast<uintptr_t>(&Dispatch),original_dispatch))return false;
    installed=false;owner=0;capture=nullptr;peer_capture=nullptr;original_dispatch=0;profile={};return true;
}
}
}
