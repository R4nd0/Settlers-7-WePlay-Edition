#pragma once
#include "crossplay_gold_local_observer.h"
#include "crossplay_game_start_codec.h"
namespace CrossplayNativeStart {
inline void GoldStartBridge();
// This opt-in capture-only primitive deliberately does not enqueue a world.
// It replaces only the original fresh-start send call, not the general sender.
// The callback must copy/validate its borrowed event before returning. There is
// no release/replay operation here. Ordinary adapters never include this file.
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
            try {const bool accepted=s.callback(manager,event);s.captured=accepted && !s.failed;}catch(...){s.failed=true;}
            if(!s.captured)s.failed=true;
        }
        SetLastError(error);
    }
    static bool Install(const CrossplayNative::ParticipantEvent& binding,Callback callback) {
        auto& s=Current();if(s.roots.installed || !callback || !Methods()
            || !CrossplayNativeCodec::GameStartDetail::Methods(Crossplay::Edition::Gold))return false;
        CrossplayNative::LobbySnapshot lobby;uintptr_t sender=0;
        auto* manager=CrossplayNativeCodec::Detail::Transport(Crossplay::Edition::Gold,sender);
        if(!manager || sender!=0xa4e430 || !binding.CaptureLobby(lobby) || !lobby.host || lobby.local_slot!=1
            || lobby.members.size()!=1 || !lobby.instance)return false;
        State next;next.callback=callback;next.host=lobby.members[0].player.connection;
        next.roots.owner=GetCurrentThreadId();next.roots.manager=reinterpret_cast<uintptr_t>(manager);
        next.roots.main=*reinterpret_cast<const uintptr_t*>(0x10f83dc);next.roots.session=lobby.instance;
        if(next.host==UINT32_MAX || !CrossplayNativeObserver::GoldLocalObserver::Root(next.roots))return false;
        auto* at=reinterpret_cast<unsigned char*>(0xa35f64);unsigned char old[6]{};memcpy(old,at,6);
        unsigned char patch[6]={0xe9,0,0,0,0,0x90};
        const int32_t relative=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&GoldStartBridge)-(0xa35f64+5));memcpy(patch+1,&relative,4);
        DWORD protection=0;if(!VirtualProtect(at,6,PAGE_EXECUTE_READWRITE,&protection))return false;
        next.roots.installed=true;s=next;memcpy(at,patch,6);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),at,6)!=FALSE;
        DWORD ignored=0;const bool restored=VirtualProtect(at,6,protection,&ignored)!=FALSE;
        if(!flushed || !restored) {
            DWORD temporary=0;
            if(VirtualProtect(at,6,PAGE_EXECUTE_READWRITE,&temporary)) {
                memcpy(at,old,6);FlushInstructionCache(GetCurrentProcess(),at,6);VirtualProtect(at,6,protection,&ignored);
            }
            s.failed=true;s.callback=nullptr;return false;
        }
        return true;
    }
};
__declspec(naked) inline void GoldStartBridge() {
    __asm {
        mov dword ptr [esp+0x34],ebx
        call GoldStartHold::Send
        push 0x0a35f6a
        ret
    }
}
}
