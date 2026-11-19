#pragma once
#include "crossplay_history_host_configuration.h"
#include "crossplay_configuration_observer.h"
#include "crossplay_ai_preparation_source.h"
#include "crossplay_history_claim.h"

// Reverse-path primitive only. Queue an actual Gold guest toggle intent through
// the original Steam host constructor/consumer. Never supply an achieved Ready.
namespace CrossplayHistoryHostReady {
struct Region {uintptr_t at=0;size_t bytes=0;uint64_t hash=0;};
struct Profile {uintptr_t preparation=0;std::array<Region,3> methods{};};
inline Profile Pin(Crossplay::Edition edition){
    if(edition==Crossplay::Edition::SteamHistory)return {0x13a2300,{{
        {0x6909f0,42,0xb52c294d9ab2cd5cull},{0x690940,44,0xaa88c12a3348c3ffull},{0x656a20,58,0xfd49b8c797ab85e5ull}}}};
    if(edition==Crossplay::Edition::UbisoftHistory)return {0x13a5218,{{
        {0x690e10,42,0x76e5d96dc804c8ecull},{0x690d60,44,0x1654c334e566fd53ull},{0x656e40,58,0x8316408d56fd74cbull}}}};
    return {};
}
inline bool Methods(Crossplay::Edition edition=Crossplay::Edition::SteamHistory,unsigned* stage=nullptr){
    const auto profile=Pin(edition);
    if(stage)*stage=11;
    if(!profile.preparation || !CrossplayNativeCodec::ReadyDetail::Methods(edition))return false;
    if(stage)*stage=12;
    if(!CrossplayProduction::ConfigurationObserver::Methods(edition))return false;
    unsigned part=13;
    for(const auto& region:profile.methods){
        if(stage)*stage=part++;
        const auto* code=reinterpret_cast<const unsigned char*>(region.at);
        if(!CrossplayNativeCodec::Detail::Readable(code,region.bytes))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<region.bytes;++i)hash=(hash^code[i])*1099511628211ull;
        if(hash!=region.hash)return false;
    }
    if(stage)*stage=0;
    return true;
}
inline void* ConstructWith(uintptr_t address,void* output,uint32_t source){
    using Construct=void* (__thiscall*)(void*,uint16_t,uint32_t);
    return reinterpret_cast<Construct>(address)(output,29,source);
}
struct View {uintptr_t session=0,cache=0,manager=0,configuration=0,rows=0;unsigned mode=0;};
inline bool Same(const View& a,const View& b){
    return a.session && a.session==b.session && a.cache==b.cache && a.manager==b.manager
        && a.configuration==b.configuration && a.rows==b.rows && a.mode==b.mode;
}
inline bool Context(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,
                    CrossplaySessionConfig::Connections ids,View& output,
                    Crossplay::Edition edition=Crossplay::Edition::SteamHistory){
    using namespace CrossplayNativeCodec;output={};
    if(admission.GetState()!=CrossplayNative::AdmissionState::RosterBound || !admission.Poll()
        || ids.host!=admission.OwnConnection() || ids.guest!=admission.PeerLocalConnection()
        || !Methods(edition) || !SessionConfigDetail::Roster(binding,ids,true))return false;
    CrossplayNative::LobbySnapshot lobby;if(!binding.CaptureLobby(lobby))return false;
    const auto* session=reinterpret_cast<const unsigned char*>(lobby.instance);
    if(!Detail::Readable(session,0x2c) || CrossplayChat::Word(session+0x28)!=2)return false;
    auto* cache=ReadyStateDetail::HistoryConfiguration(edition);
    if(!cache || !SessionConfigDetail::Fields(SessionConfigDetail::Pin(edition),cache+0x24,cache+0x34,cache+0x44,ids))return false;
    CrossplayAIPreparationSource::Snapshot preparation{};unsigned stage=0;
    if(!CrossplayAIPreparationSource::Capture(edition,preparation,stage) || !preparation.same)return false;
    const auto pointer=Pin(edition).preparation;
    const auto* manager=*reinterpret_cast<const unsigned char* const*>(pointer);
    const unsigned mode=manager[4];const auto* buffer=manager+((mode-1)&1)*0xc4;
    const auto configuration=CrossplayChat::Word(buffer+0x98);
    const auto rows=CrossplayChat::Word(reinterpret_cast<const unsigned char*>(configuration)+4);
    for(unsigned slot=1;slot<=2;++slot){
        const auto* row=reinterpret_cast<const unsigned char*>(rows)+slot*0x5a8;
        if(CrossplayChat::Word(row+0x5c)!=slot || CrossplayChat::Word(row+0x60)!=3
            || CrossplayChat::Word(row+0x64)!=1 || row[0x71]>1)return false;
    }
    output={lobby.instance,reinterpret_cast<uintptr_t>(cache),reinterpret_cast<uintptr_t>(manager),configuration,rows,mode};
    return *reinterpret_cast<const unsigned char* const*>(pointer)==manager && manager[4]==mode
        && CrossplayChat::Word(buffer+0x98)==configuration;
}
struct OwnedEvent {
    alignas(4) unsigned char bytes[0x18]{};bool initialized=false;
    uintptr_t table=0;
    ~OwnedEvent(){if(initialized)reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(
        *reinterpret_cast<const uintptr_t*>(table))(bytes,0);}
};
struct Pending {
    DWORD owner=0;View view{};CrossplaySessionConfig::Connections ids{};unsigned char before=0;ULONGLONG started=0;
    Crossplay::Edition edition=Crossplay::Edition::SteamHistory;
};
inline bool Enqueue(const CrossplayNative::ParticipantEvent& binding,CrossplayNative::LobbyAdmission& admission,
                    CrossplaySessionConfig::Connections ids,const std::vector<unsigned char>& intent,Pending& pending,
                    Crossplay::Edition edition=Crossplay::Edition::SteamHistory){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;pending={};
    using namespace CrossplayNativeCodec;View before,after;
    if(!CrossplayReady::Validate(intent,ids.guest) || !Context(binding,admission,ids,before,edition))return false;
    auto* cache=reinterpret_cast<unsigned char*>(before.cache);
    CrossplayAIProfile::CacheLock lock(edition,cache);if(!lock.acquired)return false;
    const auto* slots=*reinterpret_cast<const unsigned char* const*>(cache+0x44);
    const auto prior=slots[0x18+4];if(prior>1)return false;
    OwnedEvent event;const auto pin=ReadyDetail::Pin(edition);
    auto* result=ConstructWith(Pin(edition).methods[0].at,event.bytes,ids.guest);
    event.table=pin.event_vt;
    event.initialized=CrossplayChat::Word(event.bytes)==pin.event_vt;
    if(!event.initialized || result!=event.bytes || !ReadyDetail::Source(pin,event.bytes,ids.guest)
        || !Context(binding,admission,ids,after,edition) || !Same(before,after))return false;
    // The constructor leaves the network-slot/Ready payload untouched. The
    // original host consumer uses only this authenticated source's intent and
    // toggles its own cache/preparation row; no payload Ready value is written.
    uintptr_t send=0;void* transport=Detail::Transport(edition,send);if(!transport)return false;
    pending={GetCurrentThreadId(),before,ids,prior,GetTickCount64(),edition};
    reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,event.bytes,0,0,0,0);
    return true;
}
inline CrossplayNativeCodec::ReadyObservation Observe(const CrossplayNative::ParticipantEvent& binding,
                    CrossplayNative::LobbyAdmission& admission,const Pending& pending,uint32_t revision,
                    CrossplayReadyState::Snapshot& output,std::array<unsigned char,4>* actual=nullptr){
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    using namespace CrossplayNativeCodec;output={};if(actual)actual->fill(0xff);
    if(!pending.owner || pending.owner!=GetCurrentThreadId() || !revision
        || GetTickCount64()-pending.started>10000)return ReadyObservation::Invalid;
    View current;if(!Context(binding,admission,pending.ids,current,pending.edition) || !Same(current,pending.view))return ReadyObservation::Invalid;
    auto* cache=reinterpret_cast<unsigned char*>(current.cache);
    CrossplayAIProfile::CacheLock lock(pending.edition,cache);if(!lock.acquired)return ReadyObservation::Pending;
    const auto* slots=*reinterpret_cast<const unsigned char* const*>(cache+0x44);
    const auto* rows=reinterpret_cast<const unsigned char*>(current.rows);
    if(slots[4]>1 || slots[0x1c]>1 || rows[0x5a8+0x71]>1 || rows[2*0x5a8+0x71]>1)return ReadyObservation::Invalid;
    if(actual)*actual={slots[4],slots[0x1c],rows[0x5a8+0x71],rows[2*0x5a8+0x71]};
    const auto expected=static_cast<unsigned char>(!pending.before);
    // The original request targets the guest only. The host's own preparation
    // flag is not changed by it and need not equal the host's cached Ready bit.
    if(slots[0x1c]!=expected || rows[2*0x5a8+0x71]!=expected)return ReadyObservation::Pending;
    const CrossplayReadyState::Snapshot state{revision,slots[4],slots[0x1c]};
    if(!CrossplayReadyState::Valid(state))return ReadyObservation::Invalid;
    output=state;return ReadyObservation::Applied;
}

inline bool EnqueueRoom(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                        const CrossplayNative::RoomLobbyAdmission& admission,unsigned char local_slot,
                        unsigned char source_slot,const CrossplaySessionConfig::RoomConnections& routes,
                        const std::vector<unsigned char>& intent,unsigned* stage=nullptr,const std::string& save_token={}) {
    using namespace CrossplayNativeCodec;
    using namespace CrossplayNative::HistoryClaimDetail;
    LoadProgressDetail::FpScope preserve;
    CrossplayNative::RoomRoute local,guest;
    CrossplayNative::LobbySnapshot lobby;
    CrossplaySessionConfig::RoomConnections current;
    if(stage)*stage=1;
    if(!Methods(edition,stage))return false;
    if(stage)*stage=2;
    if(!admission.Resolve(local_slot,false,local) || !local.local || !local.host
        || !admission.Resolve(source_slot,false,guest) || guest.local || guest.host || guest.receiver!=edition
        || !binding.CaptureLobby(lobby) || !lobby.host || !CrossplayReady::Validate(intent,guest.receiver_cid)
        || !admission.CaptureConfigurationRoutes(routes.capacity,current) || current.host!=routes.host
        || current.stations!=routes.stations || local.receiver_cid!=routes.host)return false;
    if(stage)*stage=3;
    auto* cache=ReadyStateDetail::HistoryConfiguration(edition);
    if(!cache)return false;
    CrossplayAIProfile::CacheLock lock(edition,cache);
    if(!lock.acquired || !SessionConfigDetail::FieldsChecked(SessionConfigDetail::Pin(edition),
        cache+0x24,cache+0x34,cache+0x44,[&](size_t index,unsigned ready,unsigned privacy,uint32_t slot,uint32_t station,uint32_t state){
            return CrossplaySessionConfig::RoomSlotValid(index,ready,privacy,slot,station,state,routes,true);
        },save_token))return false;
    CrossplayNative::HistoryClaimDetail::Pin preparation;
    Interval prepared;
    CrossplayNative::Participant target;
    CrossplayCastleSetup::Value castle;
    uint32_t extra=0;
    if(stage)*stage=4;
    if(!guest.player_slot || guest.player_slot>routes.capacity || !Layout(edition,preparation)
        || !Capture(preparation,lobby,prepared,routes.capacity)
        || !admission.CaptureMemberSetup(guest.player_slot,target,castle,extra)
        || target.connection!=guest.receiver_cid || target.local || target.host)return false;
    const auto* row=reinterpret_cast<const unsigned char*>(prepared.rows)+guest.player_slot*0x5a8;
    if(stage)*stage=5;
    if(CrossplayChat::Word(row+0x5c)==UINT32_MAX
        && (!CrossplayNative::HistoryClaimDetail::Methods(edition)
            || !ClaimOpenHumanRow(preparation,prepared,target)))return false;
    if(CrossplayChat::Word(row+0x5c)!=guest.player_slot || CrossplayChat::Word(row+0x60)!=3
        || CrossplayChat::Word(row+0x64)!=1 || row[0x71]>1)return false;
    OwnedEvent event;
    if(stage)*stage=6;
    const auto pin=ReadyDetail::Pin(edition);
    auto* result=ConstructWith(CrossplayHistoryHostReady::Pin(edition).methods[0].at,event.bytes,guest.receiver_cid);
    event.table=pin.event_vt;
    event.initialized=CrossplayChat::Word(event.bytes)==pin.event_vt;
    if(!event.initialized || result!=event.bytes || !ReadyDetail::Source(pin,event.bytes,guest.receiver_cid))return false;
    CrossplayNative::RoomRoute target_now;
    if(stage)*stage=7;
    if(!admission.Resolve(source_slot,false,target_now) || target_now.receiver_cid!=guest.receiver_cid
        || target_now.player_slot!=guest.player_slot || ReadyStateDetail::HistoryConfiguration(edition)!=cache)return false;
    uintptr_t send=0;
    void* transport=Detail::Transport(edition,send);
    if(!transport)return false;
    reinterpret_cast<void (__thiscall*)(void*,const void*,uint32_t,uint32_t,uint32_t,uint32_t)>(send)(transport,event.bytes,0,0,0,0);
    if(stage)*stage=0;
    return true;
}
}
