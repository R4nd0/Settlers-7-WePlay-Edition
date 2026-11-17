#pragma once
#include "crossplay_gold_guest_host_identity.h"
#include "crossplay_history_host_guest_identity.h"

// One original Confirmed consumer after authenticated peer identity binding.
// Never copy another engine's native castle object or write achieved Ready.
namespace CrossplayNative {
enum class PeerCastleObservation { Pending, Applied, Invalid };
namespace PeerCastlePreparationDetail {
struct View {uintptr_t session=0,rows=0;unsigned mode=0,slot=0;size_t stride=0,castle_at=0;};
inline bool Same(const View& a,const View& b){return a.session && a.session==b.session && a.rows==b.rows && a.mode==b.mode
    && a.slot==b.slot && a.stride==b.stride && a.castle_at==b.castle_at;}
inline bool Context(Crossplay::Edition edition,const ParticipantEvent& binding,LobbyAdmission& admission,View& output,Participant& peer){
    output={};peer={};const bool gold=edition==Crossplay::Edition::Gold;
    if(!gold && edition!=Crossplay::Edition::SteamHistory)return false;
    const CrossplaySessionConfig::Connections ids=gold?CrossplaySessionConfig::Connections{admission.PeerLocalConnection(),admission.OwnConnection()}
        :CrossplaySessionConfig::Connections{admission.OwnConnection(),admission.PeerLocalConnection()};
    if(!CrossplayNativeCodec::SessionConfigDetail::Roster(binding,ids,!gold))return false;
    LobbySnapshot lobby;if(!binding.CaptureLobby(lobby))return false;
    uintptr_t rows=0;unsigned mode=0;
    if(gold){GoldGuestRowDetail::Interval v;if(!GoldGuestRowDetail::Capture(lobby,v))return false;rows=v.rows;mode=v.mode;}
    else{HistoryClaimDetail::Interval v;if(!HistoryHostGuestRowDetail::Capture(binding,admission,v))return false;rows=v.rows;mode=v.mode;}
    const size_t stride=gold?0x5c4:0x5a8;
    for(const auto& member:lobby.members){
        const auto& p=member.player;if(p.slot<1 || p.slot>2)return false;
        const auto* row=reinterpret_cast<const unsigned char*>(rows)+p.slot*stride;std::string name;
        if(CrossplayChat::Word(row+0x5c)!=p.slot || CrossplayChat::Word(row+0x60)!=3 || CrossplayChat::Word(row+0x64)!=1
            || !CrossplayNativeCodec::SessionConfigDetail::NativeString(row+0x1c,name) || name!=p.name)return false;
        if(!p.local)peer=p;
    }
    if(peer.slot!=(gold?1u:2u) || peer.connection!=admission.PeerLocalConnection())return false;
    output={lobby.instance,rows,mode,peer.slot,stride,gold?0x574u:0x55cu};return true;
}
inline bool SamePeer(const Participant& a,const Participant& b){return a.connection==b.connection && a.slot==b.slot && a.principal==b.principal
    && a.station==b.station && a.name==b.name && a.host==b.host && a.local==b.local;}
}
class PeerCastlePreparation {
    bool attempted=false,consumed=false,applied=false;DWORD owner=0;ULONGLONG started=0;
    Crossplay::Edition edition{};PeerCastlePreparationDetail::View view;Participant expected_peer;
    uintptr_t event_table=0;uint32_t event_type=0,event_source=0;std::vector<unsigned char> expected_castle;
public:
    bool Begin(Crossplay::Edition value,ParticipantEvent& binding,LobbyAdmission& admission){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
        if(attempted)return false;attempted=true;
        PeerCastlePreparationDetail::View before,after;Participant source,actual;CrossplayCastleSetup::Value castle;uint32_t extra=0;
        if(!admission.CapturePeerSetup(source,castle,extra) || !PeerCastlePreparationDetail::Context(value,binding,admission,before,actual)
            || !PeerCastlePreparationDetail::SamePeer(source,actual) || !CrossplayCastleSetup::Encode(castle,expected_castle))return false;
        ParticipantEvent event;
        if(!event.Bind(value) || !event.ConstructConfirmedSetup(actual,castle,extra))return false;
        const auto* raw=static_cast<const unsigned char*>(event.Get());const size_t local_at=value==Crossplay::Edition::Gold?0x98:0x90;
        if(!CrossplayNativeCodec::Detail::Readable(raw,local_at+1) || raw[local_at]
            || CrossplayChat::Word(raw+4)!=(7u|(3u<<16)) || CrossplayChat::Word(raw+8)!=actual.connection)return false;
        event_table=CrossplayChat::Word(raw);event_type=CrossplayChat::Word(raw+4);event_source=CrossplayChat::Word(raw+8);
        if(!PeerCastlePreparationDetail::Context(value,binding,admission,after,source)
            || !PeerCastlePreparationDetail::Same(before,after) || !PeerCastlePreparationDetail::SamePeer(actual,source))return false;
        owner=GetCurrentThreadId();started=GetTickCount64();edition=value;view=before;expected_peer=actual;
        return event.EnqueueLocal() && event.Reset();
    }
    bool Consumed(void* session,const unsigned char* event){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        const size_t local_at=edition==Crossplay::Edition::Gold?0x98:0x90;
        if(!owner || owner!=GetCurrentThreadId() || consumed || applied || reinterpret_cast<uintptr_t>(session)!=view.session
            || !CrossplayNativeCodec::Detail::Readable(event,local_at+1) || event[local_at] || CrossplayChat::Word(event)!=event_table
            || CrossplayChat::Word(event+4)!=event_type || CrossplayChat::Word(event+8)!=event_source)return false;
        consumed=true;return true;
    }
    PeerCastleObservation Observe(ParticipantEvent& binding,LobbyAdmission& admission){
        struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
        CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
        if(!owner || owner!=GetCurrentThreadId() || GetTickCount64()-started>10000)return PeerCastleObservation::Invalid;
        if(!consumed)return PeerCastleObservation::Pending;
        PeerCastlePreparationDetail::View current;Participant actual;
        if(!admission.Poll() || !PeerCastlePreparationDetail::Context(edition,binding,admission,current,actual)
            || !PeerCastlePreparationDetail::Same(view,current) || !PeerCastlePreparationDetail::SamePeer(expected_peer,actual))return PeerCastleObservation::Invalid;
        CrossplayCastleSetup::Value castle;std::vector<unsigned char> encoded;
        const auto* source=reinterpret_cast<const unsigned char*>(view.rows)+view.slot*view.stride+view.castle_at;
        if(!binding.CaptureCastleSetup(source,castle) || !CrossplayCastleSetup::Encode(castle,encoded))return PeerCastleObservation::Invalid;
        if(encoded!=expected_castle)return PeerCastleObservation::Pending;
        applied=true;return PeerCastleObservation::Applied;
    }
};
}
