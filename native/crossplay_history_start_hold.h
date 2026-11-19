#pragma once
#include "crossplay_history_host_ready.h"
#include "crossplay_game_start_codec.h"
#include "crossplay_outgoing_observer.h"
#include "crossplay_resume_start.h"

// Same capture-only contract as GoldStartHold: copy a borrowed ORIGINAL start
// event, never replay/enqueue it. The shared setup/world barrier remains separate.
namespace CrossplayNativeStart {
inline void HistoryStartEntry();inline void HistoryStartBridge();
inline uintptr_t history_start_resume=0;
struct HistoryStartHold {
    using Callback=bool (__stdcall*)(void*,const void*);
    struct State {
        const CrossplayNative::ParticipantEvent* binding=nullptr;CrossplayNative::LobbyAdmission* admission=nullptr;
        CrossplayHistoryHostReady::View roots{};uintptr_t main=0,manager=0;DWORD owner=0;
        std::array<char,CrossplaySessionConfig::MaximumPath+1> map{};size_t map_size=0;
        Callback callback=nullptr;bool installed=false,attempted=false,captured=false,inside=false,failed=false;
        unsigned failure=0;
        Crossplay::Edition edition=Crossplay::Edition::SteamHistory;
        const CrossplayNative::RoomLobbyAdmission* room=nullptr;
        unsigned char room_slot=0,room_capacity=0;
        uint32_t host_connection=UINT32_MAX;
        std::string save_token;
    };
    static State& Current(){static State state;return state;}
    static constexpr uintptr_t Site=0xec770d;
    struct Profile {uintptr_t start=0,site=0,main=0,main_vt=0,send=0;uint64_t fingerprint=0;};
    static Profile Pin(Crossplay::Edition edition){
        if(edition==Crossplay::Edition::SteamHistory)return {0xec7660,Site,0x13a230c,0x113d7ec,0xeda9f0,0xac3ced5b1f62db3cull};
        if(edition==Crossplay::Edition::UbisoftHistory)return {0xec7f70,0xec801d,0x13a5210,0x113f2c0,0xedb300,0x966924e7198c3dffull};
        return {};
    }
    static bool Jump(Crossplay::Edition edition=Crossplay::Edition::SteamHistory){
        const auto site=Pin(edition).site;
        if(!site || !CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(site),5))return false;
        const auto* code=reinterpret_cast<const unsigned char*>(site);int32_t delta=0;memcpy(&delta,code+1,4);
        return code[0]==0xe9 && site+5+delta==reinterpret_cast<uintptr_t>(&HistoryStartEntry);
    }
    static bool Methods(Crossplay::Edition edition=Crossplay::Edition::SteamHistory){
        using CrossplayNativeCodec::Detail::Readable;
        const auto pin=Pin(edition);
        std::array<unsigned char,208> code{};
        if(!pin.site || !Readable(reinterpret_cast<void*>(pin.start),code.size()))return false;
        memcpy(code.data(),reinterpret_cast<void*>(pin.start),code.size());
        const unsigned char original[5]={0x8b,0x50,0x14,0xff,0xd2};
        if(Current().installed){
            if(Current().edition!=edition || !Jump(edition))return false;
            memcpy(code.data()+pin.site-pin.start,original,5);
        }else if(memcmp(code.data()+pin.site-pin.start,original,5))return false;
        uint64_t hash=14695981039346656037ull;for(auto byte:code)hash=(hash^byte)*1099511628211ull;
        return hash==pin.fingerprint && CrossplayNativeCodec::GameStartDetail::Methods(edition)
            && CrossplayProduction::OutgoingObserver::Methods(edition);
    }
    static void Reject(unsigned stage){auto& state=Current();if(!state.failed)state.failure=stage;state.failed=true;}
    static bool Bound(const State& state,bool ready){
        const auto* launch=CrossplayProduction::CurrentLaunch();
        const auto pin=Pin(state.edition);
        if(!launch || !launch->mixed || !launch->host || launch->channel.edition!=state.edition || !pin.site
            || state.owner!=GetCurrentThreadId() || !state.binding
            || !state.map_size || state.map_size>CrossplaySessionConfig::MaximumPath
            || state.map[state.map_size])return false;
        using namespace CrossplayNativeCodec;CrossplayNative::LobbySnapshot lobby;uintptr_t send=0;
        CrossplaySessionConfig::Connections ids{};
        CrossplaySessionConfig::RoomConnections routes;
        if(state.room){
            CrossplayNative::RoomRoute local;
            if(!launch->IsRoom() || launch->room.channel.slot!=state.room_slot || launch->room.capacity!=state.room_capacity
                || !state.room->Resolve(state.room_slot,false,local) || !local.local || !local.host
                || local.receiver_cid!=state.host_connection || local.receiver!=state.edition
                || !state.room->CaptureConfigurationRoutes(state.room_capacity,routes) || routes.host!=state.host_connection)return false;
        }else{
            if(!state.admission || launch->channel.expected_peer!=Crossplay::Edition::Gold)return false;
            ids={state.admission->OwnConnection(),state.admission->PeerLocalConnection()};
            if(ids.host!=state.host_connection || !SessionConfigDetail::Roster(*state.binding,ids,true))return false;
        }
        if(!state.binding->CaptureLobby(lobby) || !lobby.host
            || lobby.instance!=state.roots.session
            || reinterpret_cast<uintptr_t>(CrossplayNativeCodec::Detail::Transport(state.edition,send))!=state.manager
            || !state.manager || send!=pin.send)return false;
        if(!Detail::Readable(reinterpret_cast<void*>(pin.main),4)
            || *reinterpret_cast<const uintptr_t*>(pin.main)!=state.main)return false;
        const auto* main=reinterpret_cast<const unsigned char*>(state.main);
        const auto* session=reinterpret_cast<const unsigned char*>(lobby.instance);
        if(!Detail::Readable(main,0x18) || CrossplayChat::Word(main)!=pin.main_vt
            || CrossplayChat::Word(main+0x10)!=state.manager || CrossplayChat::Word(main+0x14)!=lobby.instance
            || !Detail::Readable(session,0x2c) || (!state.room && CrossplayChat::Word(session+0x28)!=2))return false;
        auto* cache=ReadyStateDetail::HistoryConfiguration(state.edition);
        if(reinterpret_cast<uintptr_t>(cache)!=state.roots.cache)return false;
        if(!ready)return true;
        CrossplayAIProfile::CacheLock lock(state.edition,cache);if(!lock.acquired)return false;
        std::string map,save;
        if(!SessionConfigDetail::NativeString(cache+0x24,map) || map!=std::string(state.map.data(),state.map_size)
            || !SessionConfigDetail::NativeString(cache+0x34,save) || save!=state.save_token)return false;
        const auto configuration=SessionConfigDetail::Pin(state.edition);
        if(state.room)return SessionConfigDetail::FieldsChecked(configuration,cache+0x24,cache+0x34,cache+0x44,
            [&](size_t index,unsigned bit,unsigned privacy,uint32_t slot,uint32_t station,uint32_t kind){
                return CrossplaySessionConfig::RoomSlotValid(index,bit,privacy,slot,station,kind,routes,true)
                    && (routes.stations[index]==UINT32_MAX || bit==1);
            },state.save_token);
        if(!SessionConfigDetail::Fields(configuration,cache+0x24,cache+0x34,cache+0x44,ids))return false;
        const auto* slots=*reinterpret_cast<const unsigned char* const*>(cache+0x44);
        // Capture may precede delivery of the original UI's closed-slot events.
        // Full neutral/AI preparation and setup-root equality belong to the
        // later world barrier; no world permission is granted by this hold.
        return slots[4]==1 && slots[0x1c]==1;
    }
#ifdef S7_HISTORY_START_HOLD_TESTING
    inline static void (__cdecl* probe)(void*,void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)=nullptr;
#endif
    static void __fastcall Send(void* manager,void* original,const void* event,uint32_t mode,uint32_t a,uint32_t b,uint32_t c){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;auto& state=Current();
#ifdef S7_HISTORY_START_HOLD_TESTING
        if(probe)probe(manager,original,event,mode,a,b,c);
#endif
        if(!state.installed || state.failed || state.attempted || state.inside || !state.callback){Reject(1);return;}
        if(reinterpret_cast<uintptr_t>(manager)!=state.manager){Reject(2);return;}
        if(reinterpret_cast<uintptr_t>(original)!=Pin(state.edition).send){Reject(3);return;}
        if(mode!=2 || a || b || c){Reject(4);return;}
        if(!Methods(state.edition)){Reject(5);return;}
        if(!Bound(state,true)){Reject(6);return;}
        state.attempted=true;
        const auto pin=CrossplayNativeCodec::GameStartDetail::Pin(state.edition);
        if(!CrossplayNativeCodec::GameStartDetail::Source(pin,event,state.host_connection)){Reject(7);return;}
        state.inside=true;
        try{state.captured=state.callback(manager,event) && !state.failed;}catch(...){Reject(8);}
        state.inside=false;if(!state.captured)Reject(9);
    }
private:
    static bool InstallPrepared(State next,const std::string& map){
        auto& state=Current();
        const auto pin=Pin(next.edition);
        if(state.installed || !next.callback || !CrossplaySessionConfig::RelativeMap(map) || !Methods(next.edition)
            || !CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(pin.main),4))return false;
        uintptr_t send=0;
        next.main=*reinterpret_cast<const uintptr_t*>(pin.main);
        next.manager=reinterpret_cast<uintptr_t>(CrossplayNativeCodec::Detail::Transport(next.edition,send));
        next.map_size=map.size();memcpy(next.map.data(),map.c_str(),map.size()+1);
        if(!Bound(next,false))return false;
        auto* at=reinterpret_cast<unsigned char*>(pin.site);unsigned char old[5]{};memcpy(old,at,5);
        unsigned char patch[5]={0xe9};const int32_t delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&HistoryStartEntry)-(pin.site+5));
        memcpy(patch+1,&delta,4);DWORD protection=0,unused=0;
        if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&protection))return false;
        history_start_resume=pin.site+5;next.installed=true;state=next;memcpy(at,patch,5);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),at,5)!=FALSE;
        const bool restored=VirtualProtect(at,5,protection,&unused)!=FALSE;
        if(!flushed || !restored){
            DWORD temporary=0;if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&temporary))__fastfail(7);
            memcpy(at,old,5);const bool rollback_flushed=FlushInstructionCache(GetCurrentProcess(),at,5)!=FALSE;
            const bool rollback_restored=VirtualProtect(at,5,protection,&unused)!=FALSE;
            if(!rollback_flushed || !rollback_restored)__fastfail(7);state={};history_start_resume=0;return false;
        }
        return true;
    }
public:
    static bool InstallBound(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,
                             const std::string& map,Callback callback,Crossplay::Edition edition=Crossplay::Edition::SteamHistory){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        if(Current().installed || !callback || !CrossplaySessionConfig::RelativeMap(map) || !Methods(edition))return false;
        State next;next.binding=&binding;next.admission=&admission;next.owner=GetCurrentThreadId();next.callback=callback;
        next.edition=edition;next.host_connection=admission.OwnConnection();
        const CrossplaySessionConfig::Connections ids{next.host_connection,admission.PeerLocalConnection()};
        return CrossplayHistoryHostReady::Context(binding,admission,ids,next.roots,edition) && InstallPrepared(next,map);
    }
    static bool InstallRoomBound(const CrossplayNative::ParticipantEvent& binding,const CrossplayNative::RoomLobbyAdmission& admission,
                                 unsigned char slot,unsigned char capacity,const std::string& map,Callback callback,
                                 Crossplay::Edition edition,const std::string& save_token={}){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        if(Current().installed || !callback || !CrossplaySessionConfig::RelativeMap(map) || !Methods(edition)
            || (!save_token.empty() && !CrossplayResumeStart::SlotName(save_token)))return false;
        CrossplayNative::RoomRoute local;
        CrossplayNative::LobbySnapshot lobby;
        if(!admission.Resolve(slot,false,local) || !local.local || !local.host || local.receiver!=edition
            || !binding.CaptureLobby(lobby) || !lobby.host)return false;
        State next;next.binding=&binding;next.room=&admission;next.owner=GetCurrentThreadId();next.callback=callback;
        next.edition=edition;next.host_connection=local.receiver_cid;
        next.save_token=save_token;
        next.room_slot=slot;next.room_capacity=capacity;next.roots.session=lobby.instance;
        next.roots.cache=reinterpret_cast<uintptr_t>(CrossplayNativeCodec::ReadyStateDetail::HistoryConfiguration(edition));
        return next.roots.cache && InstallPrepared(next,map);
    }
    static bool Release(){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;auto& state=Current();
        const auto site=Pin(state.edition).site;
        if(!state.installed || state.inside || state.owner!=GetCurrentThreadId() || !Jump(state.edition))return false;
        auto* at=reinterpret_cast<unsigned char*>(site);const unsigned char original[5]={0x8b,0x50,0x14,0xff,0xd2};
        DWORD protection=0,unused=0;if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&protection))return false;
        memcpy(at,original,5);const bool flushed=FlushInstructionCache(GetCurrentProcess(),at,5)!=FALSE;
        const bool restored=VirtualProtect(at,5,protection,&unused)!=FALSE;
        if(!flushed || !restored)__fastfail(7);state={};history_start_resume=0;return true;
    }
};
__declspec(naked) inline void HistoryStartEntry(){__asm{
    mov edx,dword ptr [eax+0x14]
    push dword ptr [history_start_resume]
    jmp HistoryStartBridge
}}
__declspec(naked) inline void HistoryStartBridge(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push dword ptr [edi+56]
    push dword ptr [edi+52]
    push dword ptr [edi+48]
    push dword ptr [edi+44]
    push dword ptr [edi+40]
    mov ecx,dword ptr [edi+24]
    mov edx,dword ptr [edi+20]
    call HistoryStartHold::Send
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    ret 20
}}
}
