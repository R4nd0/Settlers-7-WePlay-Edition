#pragma once
#include "crossplay_event_codec.h"
// Opt-in observation only; ordinary game adapters do not include this header.
// This neither suppresses a native event nor authorizes a cross-edition start.
namespace CrossplayNativeObserver {
inline void __stdcall GoldLocalRecordThunk(void* manager,const void* event,uint32_t mode);
inline void GoldLocalBridge();
// Borrowed, read-only callbacks at the original Gold local-queue branch.
// Install during the owned one-human preparation interval, before the existing
// targeted/broadcast callsite probes. Native forwarding remains unconditional.
struct GoldLocalObserver {
    using Callback=void (__stdcall*)(void*,const void*,uint32_t);
    struct State {
        Callback callback=nullptr;DWORD owner=0;
        uintptr_t manager=0,session=0,main=0;
        bool installed=false,inside=false,failed=false;
    };
    static State& Current(){static State state;return state;}
    static bool OriginalMethods() {
        using CrossplayNativeCodec::Detail::Readable;
        const auto* body=reinterpret_cast<const unsigned char*>(0xa4e430);
        if(!Readable(body,0xe1))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<0xe1;++i)hash=(hash^body[i])*1099511628211ull;
        const unsigned char site[]={0x8b,0x47,0x18,0x50,0xff,0x15,0x4c,0xf0,0xeb,0x00};
        return hash==0x6b1a903a26be84feull && !memcmp(reinterpret_cast<void*>(0xa4e4cf),site,sizeof(site));
    }
    static bool Root(const State& state) {
        using CrossplayNativeCodec::Detail::Readable;
        if(!Readable(reinterpret_cast<void*>(0x10f83dc),4))return false;
        const auto* main=*reinterpret_cast<const unsigned char* const*>(0x10f83dc);
        const auto* manager=reinterpret_cast<const unsigned char*>(state.manager);
        return reinterpret_cast<uintptr_t>(main)==state.main && Readable(main,0x18) && Readable(manager,0x1c)
            && *reinterpret_cast<const uint32_t*>(main)==0xf55600 && *reinterpret_cast<const uint32_t*>(manager)==0xf566cc
            && *reinterpret_cast<const uintptr_t*>(main+0x10)==state.manager
            && *reinterpret_cast<const uintptr_t*>(main+0x14)==state.session;
    }
    static void __stdcall Observe(void* manager,const void* event,uint32_t mode) {
        const DWORD incoming=GetLastError();auto& state=Current();
        if(state.installed && !state.failed && !state.inside && mode==0 && state.callback
            && state.owner==GetCurrentThreadId() && reinterpret_cast<uintptr_t>(manager)==state.manager) {
            if(!Root(state) || !CrossplayNativeCodec::Detail::Readable(event,16))state.failed=true;
            else {
                state.inside=true;
                try {state.callback(manager,event,mode);}catch(...){state.failed=true;}
                state.inside=false;
            }
        }
        SetLastError(incoming);
    }
    static bool Install(const CrossplayNative::ParticipantEvent& binding,Callback callback) {
        const DWORD incoming=GetLastError();auto& state=Current();
        if(state.installed || !callback || !OriginalMethods())return false;
        CrossplayNative::LobbySnapshot snapshot;uintptr_t send=0;
        auto* manager=CrossplayNativeCodec::Detail::Transport(Crossplay::Edition::Gold,send);
        if(!manager || send!=0xa4e430 || !binding.CaptureLobby(snapshot) || !snapshot.host || snapshot.local_slot!=1
            || snapshot.members.size()!=1 || !snapshot.instance)return false;
        State candidate;candidate.callback=callback;candidate.owner=GetCurrentThreadId();candidate.manager=reinterpret_cast<uintptr_t>(manager);
        candidate.session=snapshot.instance;candidate.main=*reinterpret_cast<const uintptr_t*>(0x10f83dc);
        if(!Root(candidate))return false;
        auto* site=reinterpret_cast<unsigned char*>(0xa4e4cf);unsigned char before[10]{};memcpy(before,site,10);
        unsigned char replacement[10]={0xe9,0,0,0,0,0x90,0x90,0x90,0x90,0x90};
        const int32_t relative=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&GoldLocalBridge)-(0xa4e4cf+5));memcpy(replacement+1,&relative,4);
        DWORD protection=0;if(!VirtualProtect(site,10,PAGE_EXECUTE_READWRITE,&protection))return false;
        candidate.installed=true;state=candidate;memcpy(site,replacement,10);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,10)!=FALSE;
        DWORD unused=0;const bool restored=VirtualProtect(site,10,protection,&unused)!=FALSE;
        if(!flushed || !restored) {
            // No successful installation is reported after a partial mutation.
            DWORD temporary=0;const bool writable=VirtualProtect(site,10,PAGE_EXECUTE_READWRITE,&temporary)!=FALSE;
            if(writable){memcpy(site,before,10);FlushInstructionCache(GetCurrentProcess(),site,10);VirtualProtect(site,10,protection,&unused);}
            state.failed=true;state.callback=nullptr;SetLastError(incoming);return false;
        }
        SetLastError(incoming);return true;
    }
};
inline void __stdcall GoldLocalRecordThunk(void* manager,const void* event,uint32_t mode) {
    GoldLocalObserver::Observe(manager,event,mode);
}
__declspec(naked) inline void GoldLocalBridge() {
    __asm {
        pushfd
        pushad
        push dword ptr [ebp+0xc]
        push esi
        push edi
        call GoldLocalRecordThunk
        popad
        popfd
        mov eax,dword ptr [edi+0x18]
        push eax
        // Exact original CALL [0xebf04c]; explicit bytes avoid MASM treating
        // the numeric indirect operand as a C++ expression.
        _emit 0xff
        _emit 0x15
        _emit 0x4c
        _emit 0xf0
        _emit 0xeb
        _emit 0x00
        push 0x0a4e4d9
        ret
    }
}
}
