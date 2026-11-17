#pragma once
#include "crossplay_history_host_ready.h"
#include "crossplay_game_start_codec.h"
#include "crossplay_outgoing_observer.h"

// Same capture-only contract as GoldStartHold: copy a borrowed ORIGINAL start
// event, never replay/enqueue it. The shared setup/world barrier remains separate.
namespace CrossplayNativeStart {
inline void HistoryStartEntry();inline void HistoryStartBridge();
struct HistoryStartHold {
    using Callback=bool (__stdcall*)(void*,const void*);
    struct State {
        const CrossplayNative::ParticipantEvent* binding=nullptr;CrossplayNative::LobbyAdmission* admission=nullptr;
        CrossplayHistoryHostReady::View roots{};uintptr_t main=0,manager=0;DWORD owner=0;
        std::array<char,CrossplaySessionConfig::MaximumPath+1> map{};size_t map_size=0;
        Callback callback=nullptr;bool installed=false,attempted=false,captured=false,inside=false,failed=false;
        unsigned failure=0;
    };
    static State& Current(){static State state;return state;}
    static constexpr uintptr_t Site=0xec770d;
    static bool Jump(){
        if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(Site),5))return false;
        const auto* code=reinterpret_cast<const unsigned char*>(Site);int32_t delta=0;memcpy(&delta,code+1,4);
        return code[0]==0xe9 && Site+5+delta==reinterpret_cast<uintptr_t>(&HistoryStartEntry);
    }
    static bool Methods(){
        using CrossplayNativeCodec::Detail::Readable;
        constexpr uintptr_t start=0xec7660;std::array<unsigned char,208> code{};
        if(!Readable(reinterpret_cast<void*>(start),code.size()))return false;
        memcpy(code.data(),reinterpret_cast<void*>(start),code.size());
        const unsigned char original[5]={0x8b,0x50,0x14,0xff,0xd2};
        if(Current().installed){if(!Jump())return false;memcpy(code.data()+Site-start,original,5);}
        else if(memcmp(code.data()+Site-start,original,5))return false;
        uint64_t hash=14695981039346656037ull;for(auto byte:code)hash=(hash^byte)*1099511628211ull;
        return hash==0xac3ced5b1f62db3cull && CrossplayNativeCodec::GameStartDetail::Methods(Crossplay::Edition::SteamHistory)
            && CrossplayProduction::OutgoingObserver::Methods(Crossplay::Edition::SteamHistory);
    }
    static void Reject(unsigned stage){auto& state=Current();if(!state.failed)state.failure=stage;state.failed=true;}
    static bool Bound(const State& state,bool ready){
        const auto* launch=CrossplayProduction::CurrentLaunch();
        if(!launch || !launch->mixed || !launch->host || launch->channel.edition!=Crossplay::Edition::SteamHistory
            || launch->channel.expected_peer!=Crossplay::Edition::Gold || state.owner!=GetCurrentThreadId()
            || !state.binding || !state.admission || !state.map_size || state.map_size>CrossplaySessionConfig::MaximumPath
            || state.map[state.map_size])return false;
        const CrossplaySessionConfig::Connections ids{state.admission->OwnConnection(),state.admission->PeerLocalConnection()};
        using namespace CrossplayNativeCodec;CrossplayNative::LobbySnapshot lobby;uintptr_t send=0;
        if(!SessionConfigDetail::Roster(*state.binding,ids,true) || !state.binding->CaptureLobby(lobby)
            || lobby.instance!=state.roots.session
            || reinterpret_cast<uintptr_t>(CrossplayNativeCodec::Detail::Transport(Crossplay::Edition::SteamHistory,send))!=state.manager
            || !state.manager || send!=0xeda9f0)return false;
        if(!Detail::Readable(reinterpret_cast<void*>(0x13a230c),4)
            || *reinterpret_cast<const uintptr_t*>(0x13a230c)!=state.main)return false;
        const auto* main=reinterpret_cast<const unsigned char*>(state.main);
        const auto* session=reinterpret_cast<const unsigned char*>(lobby.instance);
        if(!Detail::Readable(main,0x18) || CrossplayChat::Word(main)!=0x113d7ec
            || CrossplayChat::Word(main+0x10)!=state.manager || CrossplayChat::Word(main+0x14)!=lobby.instance
            || !Detail::Readable(session,0x2c) || CrossplayChat::Word(session+0x28)!=2)return false;
        auto* cache=ReadyStateDetail::HistoryConfiguration(Crossplay::Edition::SteamHistory);
        if(reinterpret_cast<uintptr_t>(cache)!=state.roots.cache)return false;
        if(!ready)return true;
        CrossplayAIProfile::CacheLock lock(Crossplay::Edition::SteamHistory,cache);if(!lock.acquired)return false;
        std::string map,save;
        if(!SessionConfigDetail::NativeString(cache+0x24,map) || map!=std::string(state.map.data(),state.map_size)
            || !SessionConfigDetail::NativeString(cache+0x34,save) || !save.empty()
            || !SessionConfigDetail::Fields(SessionConfigDetail::Pin(Crossplay::Edition::SteamHistory),cache+0x24,cache+0x34,cache+0x44,ids))return false;
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
        if(reinterpret_cast<uintptr_t>(original)!=0xeda9f0){Reject(3);return;}
        if(mode!=2 || a || b || c){Reject(4);return;}
        if(!Methods()){Reject(5);return;}
        if(!Bound(state,true)){Reject(6);return;}
        state.attempted=true;
        const auto pin=CrossplayNativeCodec::GameStartDetail::Pin(Crossplay::Edition::SteamHistory);
        if(!CrossplayNativeCodec::GameStartDetail::Source(pin,event,state.admission->OwnConnection())){Reject(7);return;}
        state.inside=true;
        try{state.captured=state.callback(manager,event) && !state.failed;}catch(...){Reject(8);}
        state.inside=false;if(!state.captured)Reject(9);
    }
    static bool InstallBound(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,
                             const std::string& map,Callback callback){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;auto& state=Current();
        if(state.installed || !callback || !CrossplaySessionConfig::RelativeMap(map) || !Methods())return false;
        State next;next.binding=&binding;next.admission=&admission;next.owner=GetCurrentThreadId();next.callback=callback;
        const CrossplaySessionConfig::Connections ids{admission.OwnConnection(),admission.PeerLocalConnection()};uintptr_t send=0;
        if(!CrossplayHistoryHostReady::Context(binding,admission,ids,next.roots))return false;
        next.main=*reinterpret_cast<const uintptr_t*>(0x13a230c);
        next.manager=reinterpret_cast<uintptr_t>(CrossplayNativeCodec::Detail::Transport(Crossplay::Edition::SteamHistory,send));
        next.map_size=map.size();memcpy(next.map.data(),map.c_str(),map.size()+1);
        if(!Bound(next,false))return false;
        auto* at=reinterpret_cast<unsigned char*>(Site);unsigned char old[5]{};memcpy(old,at,5);
        unsigned char patch[5]={0xe9};const int32_t delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&HistoryStartEntry)-(Site+5));
        memcpy(patch+1,&delta,4);DWORD protection=0,unused=0;
        if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&protection))return false;
        next.installed=true;state=next;memcpy(at,patch,5);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),at,5)!=FALSE;
        const bool restored=VirtualProtect(at,5,protection,&unused)!=FALSE;
        if(!flushed || !restored){
            DWORD temporary=0;if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&temporary))__fastfail(7);
            memcpy(at,old,5);const bool rollback_flushed=FlushInstructionCache(GetCurrentProcess(),at,5)!=FALSE;
            const bool rollback_restored=VirtualProtect(at,5,protection,&unused)!=FALSE;
            if(!rollback_flushed || !rollback_restored)__fastfail(7);state={};return false;
        }
        return true;
    }
    static bool Release(){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;auto& state=Current();
        if(!state.installed || state.inside || state.owner!=GetCurrentThreadId() || !Jump())return false;
        auto* at=reinterpret_cast<unsigned char*>(Site);const unsigned char original[5]={0x8b,0x50,0x14,0xff,0xd2};
        DWORD protection=0,unused=0;if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&protection))return false;
        memcpy(at,original,5);const bool flushed=FlushInstructionCache(GetCurrentProcess(),at,5)!=FALSE;
        const bool restored=VirtualProtect(at,5,protection,&unused)!=FALSE;
        if(!flushed || !restored)__fastfail(7);state={};return true;
    }
};
__declspec(naked) inline void HistoryStartEntry(){__asm{
    mov edx,dword ptr [eax+0x14]
    push 0x0ec7712
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
