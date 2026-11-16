#pragma once
#include "crossplay_gold_local_observer.h"
#include "crossplay_game_start_codec.h"
namespace CrossplayNativeStart {
inline void GoldStartBridge();
// This opt-in capture-only primitive deliberately does not enqueue a world.
// It replaces only the original fresh-start send call, not the general sender.
// The callback must copy/validate its borrowed event before returning. There is
// no replay operation here. Release restores only the owned callsite; it never
// queues the held event. Ordinary adapters do not yet include this file.
struct GoldStartHold {
    using Callback=bool (__stdcall*)(void*,const void*);
    struct State {
        CrossplayNativeObserver::GoldLocalObserver::State roots{};
        Callback callback=nullptr;
        uint32_t host=UINT32_MAX;
        bool attempted=false,captured=false,failed=false;
    };
    static State& Current(){static State s;return s;}
    static bool Methods() {
        using CrossplayNativeCodec::Detail::Readable;
        const auto* body=reinterpret_cast<const unsigned char*>(0xa35eb0);
        if(!Readable(body,254))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<254;++i)hash=(hash^body[i])*1099511628211ull;
        const unsigned char site[]={0x89,0x5c,0x24,0x34,0xff,0xd2};
        return hash==0x37a8fb622ea55f5cull && !memcmp(reinterpret_cast<void*>(0xa35f64),site,6);
    }
    static void __fastcall Send(void* manager,void* original,const void* event,uint32_t mode,uint32_t a,uint32_t b,uint32_t c) {
        const DWORD error=GetLastError();auto& s=Current();
        // A partially installed hook is never allowed to start an unbridged
        // world. No fabricated native result or readiness flag is returned.
        if(!s.roots.installed || s.attempted || s.failed || !s.callback
            || s.roots.owner!=GetCurrentThreadId() || reinterpret_cast<uintptr_t>(manager)!=s.roots.manager
            || reinterpret_cast<uintptr_t>(original)!=0xa4e430 || mode || a || b || c
            || !CrossplayNativeObserver::GoldLocalObserver::Root(s.roots)) {
            s.failed=true;SetLastError(error);return;
        }
        s.attempted=true;
        const auto p=CrossplayNativeCodec::GameStartDetail::Pin(Crossplay::Edition::Gold);
        if(!CrossplayNativeCodec::GameStartDetail::Source(p,event,s.host))s.failed=true;
        else {
            s.roots.inside=true;
            try {const bool accepted=s.callback(manager,event);s.captured=accepted && !s.failed;}catch(...){s.failed=true;}
            s.roots.inside=false;
            if(!s.captured)s.failed=true;
        }
        SetLastError(error);
    }
    static bool InstallFor(const CrossplayNative::ParticipantEvent& binding,Callback callback,CrossplayNative::LobbyAdmission* admission) {
        const DWORD incoming=GetLastError();auto& s=Current();if(s.roots.installed || !callback || !Methods()
            || !CrossplayNativeCodec::GameStartDetail::Methods(Crossplay::Edition::Gold))return false;
        State next;next.callback=callback;
        if(!CrossplayNativeObserver::GoldLocalObserver::CaptureHost(binding,admission,next.roots,next.host))return false;
        auto* at=reinterpret_cast<unsigned char*>(0xa35f64);unsigned char old[6]{};memcpy(old,at,6);
        unsigned char patch[6]={0xe9,0,0,0,0,0x90};
        const int32_t relative=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&GoldStartBridge)-(0xa35f64+5));memcpy(patch+1,&relative,4);
        DWORD protection=0;if(!VirtualProtect(at,6,PAGE_EXECUTE_READWRITE,&protection))return false;
        next.roots.installed=true;s=next;memcpy(at,patch,6);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),at,6)!=FALSE;
        DWORD ignored=0;const bool restored=VirtualProtect(at,6,protection,&ignored)!=FALSE;
        if(!flushed || !restored) {
            DWORD temporary=0;
            if(!VirtualProtect(at,6,PAGE_EXECUTE_READWRITE,&temporary))__fastfail(7);
            memcpy(at,old,6);
            const bool rollback_flushed=FlushInstructionCache(GetCurrentProcess(),at,6)!=FALSE;
            const bool rollback_restored=VirtualProtect(at,6,protection,&ignored)!=FALSE;
            if(!rollback_flushed || !rollback_restored)__fastfail(7);
            s={};SetLastError(incoming);return false;
        }
        SetLastError(incoming);return true;
    }
    static bool Install(const CrossplayNative::ParticipantEvent& binding,Callback callback){return InstallFor(binding,callback,nullptr);}
    static bool InstallBound(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,Callback callback){return InstallFor(binding,callback,&admission);}
    static bool Release(){
        const DWORD incoming=GetLastError();auto& s=Current();constexpr uintptr_t at=0xa35f64;
        if(!s.roots.installed || s.roots.inside || s.roots.owner!=GetCurrentThreadId()
            || !CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(at),6))return false;
        const auto* code=reinterpret_cast<const unsigned char*>(at);int32_t relative=0;memcpy(&relative,code+1,4);
        if(code[0]!=0xe9 || code[5]!=0x90 || at+5+relative!=reinterpret_cast<uintptr_t>(&GoldStartBridge))return false;
        const unsigned char original[]={0x89,0x5c,0x24,0x34,0xff,0xd2};DWORD protection=0,unused=0;
        if(!VirtualProtect(reinterpret_cast<void*>(at),6,PAGE_EXECUTE_READWRITE,&protection))return false;
        memcpy(reinterpret_cast<void*>(at),original,6);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),6)!=FALSE;
        const bool restored=VirtualProtect(reinterpret_cast<void*>(at),6,protection,&unused)!=FALSE;
        if(!flushed || !restored)__fastfail(7);
        s={};SetLastError(incoming);return true;
    }
};
__declspec(naked) inline void GoldStartBridge() {
    __asm {
        mov dword ptr [esp+0x34],ebx
        pushfd
        pushad
        cld
        mov edi,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        push dword ptr [edi+52]
        push dword ptr [edi+48]
        push dword ptr [edi+44]
        push dword ptr [edi+40]
        push dword ptr [edi+36]
        mov ecx,dword ptr [edi+24]
        mov edx,dword ptr [edi+20]
        call GoldStartHold::Send
        fxrstor [esp]
        mov esp,edi
        popad
        popfd
        lea esp,[esp+20]
        push 0x0a35f6a
        ret
    }
}
}
