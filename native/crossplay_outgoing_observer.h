#pragma once
#include "crossplay_production_observer.h"

// Borrow original targeted/broadcast events before forwarding them exactly once.
// The bridge preserves the complete incoming CPU state and tail-calls the
// original method. A callback never owns the event or suppresses native delivery.
namespace CrossplayProduction { namespace OutgoingObserver {
using Callback=void (*)(void*,const void*,uint32_t,uint32_t);
struct Profile {uintptr_t sites[3]{},target=0,broadcast=0;bool gold=false;uintptr_t dispatcher=0;size_t bytes=0;uint64_t fingerprint=0;};
inline Profile Pin(Crossplay::Edition edition){
    if(edition==Crossplay::Edition::Gold)return {{0xa4e4ad,0xa4e4c1,0xa4e487},0xa4e520,0xa4e5f0,true,0xa4e430,0xe1,0x6b1a903a26be84feull};
    if(edition==Crossplay::Edition::UbisoftHistory)return {{0xedb3ec,0xedb3fe,0xedb352},0xedb280,0xedb200,false,0xedb300,0x103,0xec5c8a124fd87645ull};
    if(edition==Crossplay::Edition::SteamHistory)return {{0xedaadc,0xedaaee,0xedaa42},0xeda970,0xeda8f0,false,0xeda9f0,0x103,0x1962ae679d86aa56ull};
    return {};
}
inline Profile profile{};
inline uintptr_t target_method=0,broadcast_method=0,manager=0;
inline std::array<uintptr_t,3> replacements{};
inline DWORD owner=0;
inline Callback callback=nullptr;
inline bool installed=false,inside=false,failed=false;
#ifdef S7_PRODUCTION_OUTGOING_TESTING
inline bool test_authorized=false;
#endif
inline void __cdecl Record(void* source,const void* event,uint32_t mode,uint32_t target){
    const DWORD incoming=GetLastError();const auto* launch=CurrentLaunch();
    bool authorized=launch && launch->mixed;
#ifdef S7_PRODUCTION_OUTGOING_TESTING
    authorized=authorized || test_authorized;
#endif
    if(installed && authorized && !failed && !inside && callback && owner==GetCurrentThreadId()
        && reinterpret_cast<uintptr_t>(source)==manager){
        inside=true;
        try{callback(source,event,mode,target);}catch(...){failed=true;}
        inside=false;
    }
    SetLastError(incoming);
}
__declspec(naked) inline void GoldTarget(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push dword ptr [edi+44]
    push 1
    push dword ptr [edi+48]
    push dword ptr [edi+40]
    call Record
    add esp,16
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    jmp dword ptr [target_method]
}}
__declspec(naked) inline void GoldBroadcast(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push 0xffffffff
    push 2
    push dword ptr [edi+44]
    push dword ptr [edi+40]
    call Record
    add esp,16
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    jmp dword ptr [broadcast_method]
}}
__declspec(naked) inline void HistoryTarget(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push dword ptr [edi+40]
    push 1
    push dword ptr [edi+44]
    push dword ptr [edi+24]
    call Record
    add esp,16
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    jmp dword ptr [target_method]
}}
__declspec(naked) inline void HistoryBroadcast(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push 0xffffffff
    push 2
    push dword ptr [edi+40]
    push dword ptr [edi+24]
    call Record
    add esp,16
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    jmp dword ptr [broadcast_method]
}}
inline uintptr_t Bridge(const Profile& p,unsigned i){
    return i<2?(p.gold?reinterpret_cast<uintptr_t>(&GoldTarget):reinterpret_cast<uintptr_t>(&HistoryTarget))
        :(p.gold?reinterpret_cast<uintptr_t>(&GoldBroadcast):reinterpret_cast<uintptr_t>(&HistoryBroadcast));
}
inline bool Matches(uintptr_t at,uintptr_t expected){
    if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(at),5))return false;
    const auto* code=reinterpret_cast<const unsigned char*>(at);int32_t delta=0;memcpy(&delta,code+1,4);
    return code[0]==0xe8 && at+5+delta==expected;
}
inline constexpr size_t MaximumDispatcherBytes=0x103;
using DispatcherBytes=std::array<unsigned char,MaximumDispatcherBytes>;
// Normalize only our three verified CALL displacements in a detached copy.
// This permits a later owned local observer to retain the complete original
// dispatcher fingerprint instead of weakening it after these hooks are active.
inline bool CanonicalDispatcher(const Profile& p,bool patched,const std::array<uintptr_t,3>& bridges,DispatcherBytes& bytes){
    if(!p.dispatcher || !p.bytes || p.bytes>bytes.size())return false;
    for(unsigned i=0;i<3;++i){
        if(p.sites[i]<p.dispatcher || p.sites[i]-p.dispatcher>p.bytes || p.bytes-(p.sites[i]-p.dispatcher)<5)return false;
        const auto at=p.sites[i]-p.dispatcher;int32_t relative=0;memcpy(&relative,bytes.data()+at+1,4);
        const auto original=i<2?p.target:p.broadcast;
        const auto expected=patched?bridges[i]:original;
        if(!expected || bytes[at]!=0xe8 || p.sites[i]+5+relative!=expected)return false;
        if(patched){const int32_t restored=static_cast<int32_t>(original-p.sites[i]-5);memcpy(bytes.data()+at+1,&restored,4);}
    }
    uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<p.bytes;++i)hash=(hash^bytes[i])*1099511628211ull;
    return hash==p.fingerprint;
}
inline bool OriginalDispatcher(Crossplay::Edition edition,DispatcherBytes& bytes){
    const auto p=Pin(edition);bytes={};
    if(!p.dispatcher || p.bytes>bytes.size() || !CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(p.dispatcher),p.bytes))return false;
    if(installed){
        if(failed || inside || owner!=GetCurrentThreadId() || !manager || !callback || profile.dispatcher!=p.dispatcher
            || profile.target!=p.target || profile.broadcast!=p.broadcast || profile.gold!=p.gold)return false;
        for(unsigned i=0;i<3;++i)if(profile.sites[i]!=p.sites[i] || replacements[i]!=Bridge(p,i))return false;
    }
    memcpy(bytes.data(),reinterpret_cast<const void*>(p.dispatcher),p.bytes);
    return CanonicalDispatcher(p,installed,replacements,bytes);
}
inline bool Methods(Crossplay::Edition edition){
    DispatcherBytes bytes{};return OriginalDispatcher(edition,bytes);
}
inline bool Exchange(uintptr_t at,uintptr_t expected,uintptr_t replacement){
    if(!Matches(at,expected))return false;
    DWORD protection=0,unused=0;
    if(!VirtualProtect(reinterpret_cast<void*>(at),5,PAGE_EXECUTE_READWRITE,&protection))return false;
    const int32_t next=static_cast<int32_t>(replacement-at-5);memcpy(reinterpret_cast<void*>(at+1),&next,4);
    // If protection/cache restoration fails, continuing through a partially
    // installed native hook is unsafe; do not report a successful install.
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),5)!=FALSE;
    const bool restored=VirtualProtect(reinterpret_cast<void*>(at),5,protection,&unused)!=FALSE;
    if(!flushed || !restored)__fastfail(7);
    return true;
}
inline bool Install(Crossplay::Edition edition,DWORD thread,Callback observer){
    const DWORD incoming=GetLastError();const auto* launch=CurrentLaunch();const auto p=Pin(edition);
    if(installed || !observer || thread!=GetCurrentThreadId() || !launch || !launch->mixed
        || launch->channel.edition!=edition || !Methods(edition))return false;
    uintptr_t send=0;auto* transport=CrossplayNativeCodec::Detail::Transport(edition,send);
    if(!transport)return false;
    for(unsigned i=0;i<3;++i)if(!Matches(p.sites[i],i<2?p.target:p.broadcast))return false;
    profile=p;target_method=p.target;broadcast_method=p.broadcast;manager=reinterpret_cast<uintptr_t>(transport);
    for(unsigned i=0;i<3;++i)replacements[i]=Bridge(p,i);
    owner=thread;callback=observer;inside=false;failed=false;
    unsigned changed=0;
    for(;changed<3;++changed)if(!Exchange(p.sites[changed],changed<2?p.target:p.broadcast,Bridge(p,changed)))break;
    if(changed!=3){
        while(changed){--changed;if(!Exchange(p.sites[changed],Bridge(p,changed),changed<2?p.target:p.broadcast))__fastfail(7);}
        profile={};target_method=broadcast_method=manager=0;replacements={};owner=0;callback=nullptr;
        SetLastError(incoming);return false;
    }
    installed=true;SetLastError(incoming);return true;
}
inline bool Release(){
    const DWORD incoming=GetLastError();
    if(!installed || owner!=GetCurrentThreadId() || inside)return false;
    for(unsigned i=0;i<3;++i)if(!Matches(profile.sites[i],Bridge(profile,i)))return false;
    unsigned changed=0;
    for(;changed<3;++changed)if(!Exchange(profile.sites[changed],Bridge(profile,changed),changed<2?profile.target:profile.broadcast))break;
    if(changed!=3){
        while(changed){--changed;if(!Exchange(profile.sites[changed],changed<2?profile.target:profile.broadcast,Bridge(profile,changed)))__fastfail(7);}
        SetLastError(incoming);return false;
    }
    installed=false;profile={};target_method=broadcast_method=manager=0;replacements={};owner=0;callback=nullptr;failed=inside=false;
    SetLastError(incoming);return true;
}
}}
