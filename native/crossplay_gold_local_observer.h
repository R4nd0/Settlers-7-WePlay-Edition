#pragma once
#include "crossplay_event_codec.h"
#include "crossplay_outgoing_observer.h"
#include "crossplay_admission.h"
#include "crossplay_session_config_codec.h"
// Mixed-runtime observation only; production activation is gated separately.
// This neither suppresses a native event nor authorizes a cross-edition start.
namespace CrossplayNativeObserver {
inline void __stdcall GoldLocalRecordThunk(void* manager,const void* event,uint32_t mode);
inline void GoldLocalBridge();
// Borrowed, read-only callbacks at the original Gold local-queue branch.
// One-human diagnostics and explicitly admitted two-human host intervals have
// separate entry points. Known owned outgoing probes compose through a detached
// original-byte fingerprint; native forwarding remains unconditional.
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
        CrossplayProduction::OutgoingObserver::DispatcherBytes body{};
        const unsigned char site[]={0x8b,0x47,0x18,0x50,0xff,0x15,0x4c,0xf0,0xeb,0x00};
        return CrossplayProduction::OutgoingObserver::OriginalDispatcher(Crossplay::Edition::Gold,body)
            && Readable(reinterpret_cast<void*>(0xa4e4cf),sizeof(site)) && !memcmp(reinterpret_cast<void*>(0xa4e4cf),site,sizeof(site));
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
    static bool CaptureHost(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission* admission,State& candidate,uint32_t& host) {
        candidate={};host=UINT32_MAX;
        CrossplayNative::LobbySnapshot snapshot;uintptr_t send=0;
        auto* manager=CrossplayNativeCodec::Detail::Transport(Crossplay::Edition::Gold,send);
        if(!manager || send!=0xa4e430 || !binding.CaptureLobby(snapshot) || !snapshot.host || snapshot.local_slot!=1 || !snapshot.instance)return false;
        if(admission){
            const auto* launch=CrossplayProduction::CurrentLaunch();
            if(!launch || !launch->mixed || !launch->host || launch->channel.edition!=Crossplay::Edition::Gold
                || !admission->Poll() || admission->GetState()!=CrossplayNative::AdmissionState::RosterBound
                || snapshot.members.size()!=2 || !CrossplayNativeCodec::SessionConfigDetail::Roster(binding,
                    {admission->OwnConnection(),admission->PeerLocalConnection()},true))return false;
        }else if(snapshot.members.size()!=1)return false;
        if(CrossplayProduction::OutgoingObserver::installed
            && (CrossplayProduction::OutgoingObserver::owner!=GetCurrentThreadId()
                || CrossplayProduction::OutgoingObserver::manager!=reinterpret_cast<uintptr_t>(manager)))return false;
        host=admission?admission->OwnConnection():snapshot.members[0].player.connection;
        candidate.owner=GetCurrentThreadId();candidate.manager=reinterpret_cast<uintptr_t>(manager);
        candidate.session=snapshot.instance;candidate.main=*reinterpret_cast<const uintptr_t*>(0x10f83dc);
        return host!=UINT32_MAX && Root(candidate);
    }
#ifdef S7_CROSSPLAY_ROOM_LOBBY
    static bool CaptureRoomHost(const CrossplayNative::ParticipantEvent& binding,
        const CrossplayNative::RoomLobbyAdmission& admission,unsigned char local_transport_slot,
        State& candidate,uint32_t& host) {
        candidate={};host=UINT32_MAX;
        const auto* launch=CrossplayProduction::CurrentLaunch();
        CrossplayNative::RoomRoute route;
        CrossplayNative::LobbySnapshot snapshot;
        uintptr_t send=0;
        auto* manager=CrossplayNativeCodec::Detail::Transport(Crossplay::Edition::Gold,send);
        if(!launch || !launch->IsRoom() || !launch->host || launch->channel.edition!=Crossplay::Edition::Gold
            || !admission.Resolve(local_transport_slot,false,route) || !route.local || !route.host
            || route.receiver!=Crossplay::Edition::Gold || !manager || send!=0xa4e430 || !binding.CaptureLobby(snapshot)
            || !snapshot.host || snapshot.local_slot!=route.player_slot || !snapshot.instance)return false;
        if(CrossplayProduction::OutgoingObserver::installed
            && (CrossplayProduction::OutgoingObserver::owner!=GetCurrentThreadId()
                || CrossplayProduction::OutgoingObserver::manager!=reinterpret_cast<uintptr_t>(manager)))return false;
        host=route.receiver_cid;
        candidate.owner=GetCurrentThreadId();candidate.manager=reinterpret_cast<uintptr_t>(manager);
        candidate.session=snapshot.instance;candidate.main=*reinterpret_cast<const uintptr_t*>(0x10f83dc);
        return host!=UINT32_MAX && Root(candidate);
    }
#endif
    static bool InstallFor(const CrossplayNative::ParticipantEvent& binding,Callback callback,CrossplayNative::LobbyAdmission* admission) {
        const DWORD incoming=GetLastError();auto& state=Current();
        if(state.installed || !callback || !OriginalMethods())return false;
        State candidate;uint32_t host=UINT32_MAX;
        if(!CaptureHost(binding,admission,candidate,host))return false;
        candidate.callback=callback;
        auto* site=reinterpret_cast<unsigned char*>(0xa4e4cf);unsigned char before[10]{};memcpy(before,site,10);
        unsigned char replacement[10]={0xe9,0,0,0,0,0x90,0x90,0x90,0x90,0x90};
        const int32_t relative=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&GoldLocalBridge)-(0xa4e4cf+5));memcpy(replacement+1,&relative,4);
        DWORD protection=0;if(!VirtualProtect(site,10,PAGE_EXECUTE_READWRITE,&protection))return false;
        candidate.installed=true;state=candidate;memcpy(site,replacement,10);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,10)!=FALSE;
        DWORD unused=0;const bool restored=VirtualProtect(site,10,protection,&unused)!=FALSE;
        if(!flushed || !restored) {
            // No successful installation is reported after a partial mutation.
            DWORD temporary=0;
            if(!VirtualProtect(site,10,PAGE_EXECUTE_READWRITE,&temporary))__fastfail(7);
            memcpy(site,before,10);
            const bool rollback_flushed=FlushInstructionCache(GetCurrentProcess(),site,10)!=FALSE;
            const bool rollback_restored=VirtualProtect(site,10,protection,&unused)!=FALSE;
            if(!rollback_flushed || !rollback_restored)__fastfail(7);
            state={};SetLastError(incoming);return false;
        }
        SetLastError(incoming);return true;
    }
    static bool Install(const CrossplayNative::ParticipantEvent& binding,Callback callback){return InstallFor(binding,callback,nullptr);}
    static bool InstallBound(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,Callback callback){return InstallFor(binding,callback,&admission);}
    static bool Release(){
        const DWORD incoming=GetLastError();auto& state=Current();
        using CrossplayNativeCodec::Detail::Readable;
        constexpr uintptr_t at=0xa4e4cf;
        if(!state.installed || state.inside || state.owner!=GetCurrentThreadId() || !Readable(reinterpret_cast<void*>(at),10))return false;
        const auto* code=reinterpret_cast<const unsigned char*>(at);int32_t relative=0;memcpy(&relative,code+1,4);
        if(code[0]!=0xe9 || at+5+relative!=reinterpret_cast<uintptr_t>(&GoldLocalBridge))return false;
        for(unsigned i=5;i<10;++i)if(code[i]!=0x90)return false;
        const unsigned char original[]={0x8b,0x47,0x18,0x50,0xff,0x15,0x4c,0xf0,0xeb,0x00};
        DWORD protection=0,unused=0;
        if(!VirtualProtect(reinterpret_cast<void*>(at),10,PAGE_EXECUTE_READWRITE,&protection))return false;
        memcpy(reinterpret_cast<void*>(at),original,10);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),10)!=FALSE;
        const bool restored=VirtualProtect(reinterpret_cast<void*>(at),10,protection,&unused)!=FALSE;
        if(!flushed || !restored)__fastfail(7);
        state={};SetLastError(incoming);return true;
    }
};
inline void __stdcall GoldLocalRecordThunk(void* manager,const void* event,uint32_t mode) {
    GoldLocalObserver::Observe(manager,event,mode);
}
__declspec(naked) inline void GoldLocalBridge() {
    __asm {
        pushfd
        pushad
        cld
        mov edi,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        push dword ptr [ebp+0xc]
        push esi
        push dword ptr [edi]
        call GoldLocalRecordThunk
        fxrstor [esp]
        mov esp,edi
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
