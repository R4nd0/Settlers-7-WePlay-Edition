#pragma once
#include "crossplay_gold_guest_ready.h"
#include "crossplay_gold_local_observer.h"

// The independent Gold SDK retains its real private slot1. Hold only its two
// original Ready notifications at their exact send boundaries for an admitted
// common Gold guest/slot2. The callback copies intent; only the actual common
// host may subsequently supply an observed achieved state. No replay API here.
namespace CrossplayNativeReady {
inline void GoldSdkReadyToggle();inline void GoldSdkReadyClear();inline void GoldSdkReadyBridge();
// Reuses the existing Gold root/guest-role checks and Ready intent codec. Only
// these two original SDK notifications are held; they do not report common
// session state. The authenticated native host remains the Ready authority.
struct GoldSdkReadyHold {
    using Callback=bool (__stdcall*)(void*,const void*);
    struct State {
        CrossplayNativeObserver::GoldLocalObserver::State roots{};
        const CrossplayNative::ParticipantEvent* binding=nullptr;CrossplayNative::LobbyAdmission* admission=nullptr;
        const CrossplayNative::GoldGuestRowClaim* claim=nullptr;const CrossplayNative::GoldGuestIdentity* identity=nullptr;
        const CrossplayNative::RoomLobbyAdmission* room=nullptr;
        unsigned char room_slot=0;
        uint32_t room_connection=UINT32_MAX;
        Callback callback=nullptr;unsigned patched=0,captured=0,failure=0;bool inside=false,failed=false;
    };
    static State& Current(){static State state;return state;}
    static void Reject(unsigned stage){auto& state=Current();if(!state.failed)state.failure=stage;state.failed=true;}
    static uintptr_t Site(unsigned i){return i?0xaa8272:0xaa8198;}
    static uintptr_t Entry(unsigned i){return reinterpret_cast<uintptr_t>(i?&GoldSdkReadyClear:&GoldSdkReadyToggle);}
    static bool Jump(unsigned i){
        const auto at=Site(i);if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(at),5))return false;
        const auto* bytes=reinterpret_cast<const unsigned char*>(at);int32_t delta=0;memcpy(&delta,bytes+1,4);
        return bytes[0]==0xe9 && at+5+delta==Entry(i);
    }
    static bool Methods(){
        using CrossplayNativeCodec::Detail::Readable;const auto& state=Current();
        const struct Region {uintptr_t at;size_t bytes;uint64_t hash;} regions[]={
            {0xaa80e0,217,0xb9dfcbbef57cccaeull},{0xaa81c0,211,0xe67ce863f3c39eaeull}};
        for(unsigned i=0;i<2;++i){
            const auto& region=regions[i];std::array<unsigned char,217> bytes{};
            if(!Readable(reinterpret_cast<void*>(region.at),region.bytes))return false;
            memcpy(bytes.data(),reinterpret_cast<void*>(region.at),region.bytes);
            const unsigned char original[5]={static_cast<unsigned char>(i?0x50:0x51),0x8b,0xcf,0xff,0xd2};
            if(state.roots.installed){
                if(state.patched!=3 || !Jump(i))return false;
                memcpy(bytes.data()+Site(i)-region.at,original,5);
            }else if(memcmp(bytes.data()+Site(i)-region.at,original,5))return false;
            uint64_t hash=14695981039346656037ull;
            for(size_t j=0;j<region.bytes;++j)hash=(hash^bytes[j])*1099511628211ull;
            if(hash!=region.hash)return false;
        }
        return CrossplayGoldGuestReady::Methods();
    }
    static bool Bound(const State& state){
        const auto* launch=CrossplayProduction::CurrentLaunch();
        if(!launch || !launch->mixed || launch->host || launch->channel.edition!=Crossplay::Edition::Gold
            || state.roots.owner!=GetCurrentThreadId() || !state.binding
            || !CrossplayNativeObserver::GoldLocalObserver::Root(state.roots))return false;
        if(state.room){
            CrossplayNative::RoomRoute local;
            CrossplayNative::LobbySnapshot lobby;
            const auto* native=reinterpret_cast<const unsigned char*>(state.roots.session);
            return launch->IsRoom() && launch->room.channel.slot==state.room_slot
                && state.room->Resolve(state.room_slot,false,local) && local.local && !local.host
                && local.receiver==Crossplay::Edition::Gold && local.receiver_cid==state.room_connection
                && state.binding->CaptureLobby(lobby) && lobby.instance==state.roots.session
                && !lobby.host && lobby.local_slot==local.player_slot
                && CrossplayNativeCodec::Detail::Readable(native,0x34) && CrossplayChat::Word(native+0x30)==2;
        }
        return launch->channel.expected_peer==Crossplay::Edition::SteamHistory
            && state.admission && state.claim && state.identity && CrossplayGoldGuestReady::Context(
                *state.binding,*state.admission,*state.claim,*state.identity,{state.admission->PeerLocalConnection(),state.admission->OwnConnection()});
    }
#ifdef S7_GOLD_SDK_READY_HOLD_TESTING
    inline static void (__cdecl* probe)(void*,void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t,uintptr_t)=nullptr;
#endif
    static unsigned NotificationFailure(const void* event,uint32_t own,uintptr_t continuation){
        using namespace CrossplayNativeCodec;const auto pin=ReadyDetail::Pin(Crossplay::Edition::Gold);
        if(continuation!=0xaa819d && continuation!=0xaa8277)return 5;
        if(!ReadyDetail::Source(pin,event,own) || !Detail::Readable(event,0x18))return 8;
        const auto* bytes=static_cast<const unsigned char*>(event);
        if(CrossplayChat::Word(bytes+0x10)!=1)return 9;
        // The original toggle can produce either Boolean. Its private bit is
        // discarded; only the original common host can determine achieved Ready.
        return bytes[0x14]<=1?0u:10u;
    }
    static void __fastcall Send(void* manager,void* original,const void* event,uint32_t mode,uint32_t a,uint32_t b,uint32_t c,uintptr_t continuation){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        auto& state=Current();
#ifdef S7_GOLD_SDK_READY_HOLD_TESTING
        if(probe)probe(manager,original,event,mode,a,b,c,continuation);
#endif
        if(!state.roots.installed || state.failed || state.inside || !state.callback || state.patched!=3){Reject(1);return;}
        if(reinterpret_cast<uintptr_t>(manager)!=state.roots.manager){Reject(2);return;}
        if(reinterpret_cast<uintptr_t>(original)!=0xa4e430){Reject(3);return;}
        if(mode!=2 || a || b || c){Reject(4);return;}
        if(continuation!=0xaa819d && continuation!=0xaa8277){Reject(5);return;}
        if(!Methods()){Reject(6);return;}
        if(!Bound(state)){Reject(7);return;}
        const auto connection=state.room?state.room_connection:state.admission->OwnConnection();
        const unsigned invalid=NotificationFailure(event,connection,continuation);
        if(invalid){Reject(invalid);return;}
        state.inside=true;bool accepted=false;
        try{accepted=state.callback(manager,event);}catch(...){Reject(11);}
        state.inside=false;
        if(!accepted || state.failed || state.captured==UINT32_MAX)Reject(12);
        else ++state.captured;
    }
    static bool Restore(unsigned i){
        if(!Jump(i))return false;auto* at=reinterpret_cast<unsigned char*>(Site(i));
        const unsigned char original[5]={static_cast<unsigned char>(i?0x50:0x51),0x8b,0xcf,0xff,0xd2};
        DWORD protection=0,unused=0;if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&protection))return false;
        memcpy(at,original,5);const bool flushed=FlushInstructionCache(GetCurrentProcess(),at,5)!=FALSE;
        const bool restored=VirtualProtect(at,5,protection,&unused)!=FALSE;
        if(!flushed || !restored)__fastfail(7);return true;
    }
private:
    static bool InstallPrepared(State next){
        auto& state=Current();if(state.roots.installed || !next.binding || !next.callback || !Methods())return false;
        CrossplayNative::LobbySnapshot lobby;uintptr_t send=0;
        auto* manager=CrossplayNativeCodec::Detail::Transport(Crossplay::Edition::Gold,send);
        if(!manager || send!=0xa4e430 || !next.binding->CaptureLobby(lobby))return false;
        next.roots.owner=GetCurrentThreadId();next.roots.manager=reinterpret_cast<uintptr_t>(manager);next.roots.session=lobby.instance;
        next.roots.main=*reinterpret_cast<const uintptr_t*>(0x10f83dc);
        if(!Bound(next))return false;
        next.roots.installed=true;state=next;
        for(unsigned i=0;i<2;++i){
            auto* at=reinterpret_cast<unsigned char*>(Site(i));DWORD protection=0,unused=0;
            if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&protection)){
                for(unsigned j=0;j<i;++j)if(!Restore(j))__fastfail(7);
                state={};return false;
            }
            unsigned char patch[5]={0xe9};const int32_t delta=static_cast<int32_t>(Entry(i)-(Site(i)+5));memcpy(patch+1,&delta,4);memcpy(at,patch,5);
            const bool flushed=FlushInstructionCache(GetCurrentProcess(),at,5)!=FALSE;
            const bool restored=VirtualProtect(at,5,protection,&unused)!=FALSE;
            if(!flushed || !restored)__fastfail(7);
            state.patched|=1u<<i;
        }
        return true;
    }
public:
    static bool InstallBound(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,
                             const CrossplayNative::GoldGuestRowClaim& claim,const CrossplayNative::GoldGuestIdentity& identity,Callback callback){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        State next;next.binding=&binding;next.admission=&admission;next.claim=&claim;next.identity=&identity;
        next.callback=callback;return InstallPrepared(next);
    }
    static bool InstallRoomBound(const CrossplayNative::ParticipantEvent& binding,const CrossplayNative::RoomLobbyAdmission& admission,
                                 unsigned char slot,Callback callback){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        CrossplayNative::RoomRoute local;
        if(!admission.Resolve(slot,false,local) || !local.local || local.host || local.receiver!=Crossplay::Edition::Gold)return false;
        State next;next.binding=&binding;next.room=&admission;next.room_slot=slot;
        next.room_connection=local.receiver_cid;next.callback=callback;return InstallPrepared(next);
    }
    static bool Release(){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        auto& state=Current();
        if(!state.roots.installed || state.inside || state.roots.owner!=GetCurrentThreadId() || state.patched!=3
            || !Jump(0) || !Jump(1))return false;
        if(!Restore(0) || !Restore(1))__fastfail(7);state={};return true;
    }
};
__declspec(naked) inline void GoldSdkReadyToggle(){__asm{
    push ecx
    mov ecx,edi
    push 0x0aa819d
    jmp GoldSdkReadyBridge
}}
__declspec(naked) inline void GoldSdkReadyClear(){__asm{
    push eax
    mov ecx,edi
    push 0x0aa8277
    jmp GoldSdkReadyBridge
}}
__declspec(naked) inline void GoldSdkReadyBridge(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push dword ptr [edi+36]
    push dword ptr [edi+56]
    push dword ptr [edi+52]
    push dword ptr [edi+48]
    push dword ptr [edi+44]
    push dword ptr [edi+40]
    mov ecx,dword ptr [edi+24]
    mov edx,dword ptr [edi+20]
    call GoldSdkReadyHold::Send
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    ret 20
}}
}
