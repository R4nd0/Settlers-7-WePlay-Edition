#pragma once
#include "crossplay_ready_state.h"
#include "crossplay_ready_codec.h"
#include "crossplay_history_preparation.h"
#ifdef S7_CROSSPLAY_ROOM_LOBBY_LAB
#include "crossplay_history_claim.h"
#endif

namespace CrossplayNativeCodec {
namespace ReadyStateDetail {
inline bool WritableReadyRow(unsigned char* row) {
    return Detail::Readable(row,0x5a8) && CrossplayNative::HistoryRoleDetail::Writable(row+0x71,1) && row[0x71]<=1;
}
inline bool Methods(Crossplay::Edition edition) {
    if(edition!=Crossplay::Edition::UbisoftHistory && edition!=Crossplay::Edition::SteamHistory)return false;
    const bool steam=edition==Crossplay::Edition::SteamHistory;
    const uintptr_t addresses[]={steam?0x585d80u:0x5862c0u,steam?0x841a40u:0x841c70u,steam?0x690800u:0x690c20u};
    const size_t lengths[]={13,0x181,0xa0};uint64_t hash=14695981039346656037ull;
    for(unsigned i=0;i<3;++i) {
        const auto* bytes=reinterpret_cast<const unsigned char*>(addresses[i]);
        if(!Detail::Readable(bytes,lengths[i]))return false;
        for(size_t j=0;j<lengths[i];++j)hash=(hash^bytes[j])*1099511628211ull;
    }
    return hash==(steam?0x2fe23817e3a73bfbull:0xcf0aa08ba2878210ull);
}
inline void SetReady(Crossplay::Edition edition,unsigned char* row,uint32_t value) {
    const uintptr_t setter=edition==Crossplay::Edition::SteamHistory?0x585d80:0x5862c0;
    reinterpret_cast<void (__thiscall*)(void*,uint32_t)>(setter)(row,value);
}
inline bool Preparation(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                        CrossplaySessionConfig::Connections ids,CrossplayNative::LobbySnapshot& snapshot,unsigned char*& rows) {
    rows=nullptr;CrossplayNative::HistoryPreparationDetail::Layout pin{};
    if(!SessionConfigDetail::Roster(binding,ids,false) || !binding.CaptureLobby(snapshot)
        || !CrossplayNative::HistoryPreparationDetail::LayoutFor(edition,pin)
        || !CrossplayNative::HistoryPreparationDetail::Matches(pin,snapshot,[](uint32_t at,void* out,size_t size) {
            const auto* value=reinterpret_cast<const void*>(at);if(!Detail::Readable(value,size))return false;
            memcpy(out,value,size);return true;
        }))return false;
    const auto* session=reinterpret_cast<const unsigned char*>(snapshot.instance);
    if(!Detail::Readable(session,0x2c) || CrossplayChat::Word(session+0x28)!=2)return false;
    const auto* manager=*reinterpret_cast<const unsigned char* const*>(pin.manager_pointer);
    const auto* config=*reinterpret_cast<const unsigned char* const*>(manager+((manager[4]-1)&1)*0xc4+0x98);
    rows=*reinterpret_cast<unsigned char* const*>(config+4);
    for(unsigned slot=1;slot<=2;++slot)
        if(!WritableReadyRow(rows+slot*0x5a8))return false;
    return true;
}
inline unsigned char* HistoryConfiguration(Crossplay::Edition edition) {
    if(edition!=Crossplay::Edition::UbisoftHistory && edition!=Crossplay::Edition::SteamHistory)return nullptr;
    uintptr_t send=0;auto* transport=static_cast<unsigned char*>(Detail::Transport(edition,send));if(!transport)return nullptr;
    const auto* head=*reinterpret_cast<const unsigned char* const*>(transport+4);
    if(!Detail::Readable(head,12))return nullptr;
    const auto* node=*reinterpret_cast<const unsigned char* const*>(head);unsigned char* result=nullptr;unsigned count=0;
    const uintptr_t expected=SessionConfigDetail::Pin(edition).event_vt+0x38;
    for(;node!=head && count<64;++count) {
        if(!Detail::Readable(node,12))return nullptr;
        auto* plugin=*reinterpret_cast<unsigned char* const*>(node+8);
        if(!Detail::Readable(plugin,4))return nullptr;
        if(*reinterpret_cast<const uintptr_t*>(plugin)==expected) {
            if(result || !Detail::Readable(plugin,0x50))return nullptr;result=plugin;
        }
        node=*reinterpret_cast<const unsigned char* const*>(node);
    }
    return node==head?result:nullptr;
}
}
inline bool CaptureGoldReadySnapshot(const CrossplayNative::ParticipantEvent& binding,
                                    const PendingReadyToggle& pending,uint32_t revision,CrossplayReadyState::Snapshot& result) {
    result={};if(!pending.configuration || !revision || !ReadyDetail::Methods(Crossplay::Edition::Gold))return false;
    auto* config=SessionConfigDetail::GoldConfiguration();if(reinterpret_cast<uintptr_t>(config)!=pending.configuration)return false;
    SessionConfigDetail::ConfigLock lock(config);if(!lock.acquired)return false;
    unsigned char* current=nullptr;unsigned char* guest=nullptr;uintptr_t session=0;
    if(!GoldReadySlot(binding,pending.ids,current,guest,session) || current!=config || session!=pending.session || guest[4]!=pending.after)return false;
    const CrossplayReadyState::Snapshot value{revision,(guest-0x18)[4],guest[4]};
    if(!CrossplayReadyState::Valid(value))return false;result=value;return true;
}
struct PendingHistoryReadyState {uintptr_t session=0,rows=0;CrossplayReadyState::Snapshot value{};};
inline bool ApplyHistoryReadySnapshot(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                                     CrossplaySessionConfig::Connections ids,const CrossplayReadyState::Snapshot& value,
                                     PendingHistoryReadyState& pending) {
    pending={};if(!CrossplayReadyState::Valid(value) || !ReadyStateDetail::Methods(edition))return false;
    CrossplayNative::LobbySnapshot snapshot;unsigned char* rows=nullptr;
    if(!ReadyStateDetail::Preparation(edition,binding,ids,snapshot,rows))return false;
    CrossplayNative::ParticipantEvent events[2];
    for(const auto& member:snapshot.members) {
        const auto slot=member.player.slot;auto* row=rows+slot*0x5a8;CrossplayCastleSetup::Value design;
        if(!events[slot-1].Bind(edition) || !binding.CaptureCastleSetup(row+0x55c,design)
            || !events[slot-1].ConstructConfirmedSetup(member.player,design,CrossplayChat::Word(row+0x30),slot==1?value.host!=0:value.guest!=0))return false;
    }
    CrossplayNative::LobbySnapshot current;unsigned char* current_rows=nullptr;
    if(!ReadyStateDetail::Preparation(edition,binding,ids,current,current_rows) || current.instance!=snapshot.instance || current_rows!=rows)return false;
    // Original mode zero clones both owned events. The original leaf setters
    // project only observed host state into this same, guarded preparation buffer.
    // Native queue consumers, rather than this helper, update the session cache.
    for(unsigned i=0;i<2;++i)if(!events[i].EnqueueLocal())return false;
    ReadyStateDetail::SetReady(edition,rows+0x5a8,value.host);
    ReadyStateDetail::SetReady(edition,rows+2*0x5a8,value.guest);
    pending={snapshot.instance,reinterpret_cast<uintptr_t>(rows),value};return true;
}
inline ReadyObservation ObserveHistoryReadySnapshot(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                                                    CrossplaySessionConfig::Connections ids,const PendingHistoryReadyState& pending) {
    if(!pending.session || !ReadyStateDetail::Methods(edition))return ReadyObservation::Invalid;
    CrossplayNative::LobbySnapshot snapshot;unsigned char* rows=nullptr;
    if(!ReadyStateDetail::Preparation(edition,binding,ids,snapshot,rows) || snapshot.instance!=pending.session
        || reinterpret_cast<uintptr_t>(rows)!=pending.rows)return ReadyObservation::Invalid;
    auto* config=ReadyStateDetail::HistoryConfiguration(edition);if(!config)return ReadyObservation::Invalid;
    const auto* begin=*reinterpret_cast<const unsigned char* const*>(config+0x44);
    const auto* end=*reinterpret_cast<const unsigned char* const*>(config+0x48);
    if(!Detail::Readable(begin,8*0x18) || reinterpret_cast<uintptr_t>(end)-reinterpret_cast<uintptr_t>(begin)!=8*0x18)return ReadyObservation::Invalid;
    if(begin[4]>1 || begin[0x1c]>1)return ReadyObservation::Invalid;
    return begin[4]==pending.value.host && begin[0x1c]==pending.value.guest
        && rows[0x5a8+0x71]==pending.value.host && rows[2*0x5a8+0x71]==pending.value.guest?ReadyObservation::Applied:ReadyObservation::Pending;
}
#ifdef S7_CROSSPLAY_ROOM_LOBBY_LAB
struct PendingHistoryRoomReady {uintptr_t session=0,row=0;unsigned char slot=0,value=0;};
namespace ReadyStateDetail {
inline bool RoomReadyRow(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
        const CrossplayNative::RoomLobbyAdmission& admission,const CrossplaySessionConfig::RoomConnections& ids,
        unsigned char slot,CrossplayNative::LobbySnapshot& lobby,CrossplayNative::Participant& target,unsigned char*& row) {
    row=nullptr;
    if(!Methods(edition) || !slot || slot>ids.capacity || slot>CrossplaySessionConfig::Slots
        || ids.stations[slot-1]==UINT32_MAX)return false;
    CrossplayNative::HistoryPreparationDetail::Layout pin{};
    CrossplaySessionConfig::RoomConnections current;
    uint32_t row_base=0;
    if(!CrossplayNative::HistoryPreparationDetail::LayoutFor(edition,pin)
        || !admission.CaptureConfigurationRoutes(ids.capacity,current) || current.host!=ids.host || current.stations!=ids.stations
        || !binding.CaptureLobby(lobby)
        || !CrossplayNative::HistoryPreparationDetail::MatchGuestRows(pin,lobby,ids.capacity,
            [](uint32_t at,void* out,size_t size){
                const auto* value=reinterpret_cast<const void*>(at);if(!Detail::Readable(value,size))return false;
                memcpy(out,value,size);return true;
            },row_base,slot))return false;
    // CaptureLobby pins the actual session phase. History adjusts +0x28
    // separately in its native timing logic, so it cannot gate lobby Ready.
    for(const auto& member:lobby.members) {
        const auto& player=member.player;
        if(player.connection==ids.stations[slot-1] && player.slot==slot) {
            row=reinterpret_cast<unsigned char*>(row_base+slot*0x5a8u);
            if(!WritableReadyRow(row))return false;
            target=player;return true;
        }
    }
    return false;
}
inline bool ClaimRoomReadyRow(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
        const CrossplayNative::RoomLobbyAdmission& admission,const CrossplaySessionConfig::RoomConnections& ids,
        unsigned char slot,CrossplayCastleSetup::Value& design,uint32_t& extra) {
    using namespace CrossplayNative::HistoryClaimDetail;
    if(!slot || slot>ids.capacity || slot>CrossplaySessionConfig::Slots
        || ids.stations[slot-1]==UINT32_MAX || ids.stations[slot-1]==ids.host
        || !CrossplayNative::HistoryClaimDetail::Methods(edition))return false;
    const auto host=std::find(ids.stations.begin(),ids.stations.end(),ids.host);
    if(host==ids.stations.end())return false;
    const auto host_slot=static_cast<unsigned char>(host-ids.stations.begin()+1);
    CrossplayNative::LobbySnapshot lobby,after;
    CrossplayNative::Participant host_player,target;
    unsigned char* host_row=nullptr;
    Pin pin;
    Interval before,verified;
    if(!RoomReadyRow(edition,binding,admission,ids,host_slot,lobby,host_player,host_row)
        || !CrossplayNative::HistoryClaimDetail::Layout(edition,pin)
        || !admission.CaptureMemberSetup(slot,target,design,extra) || target.connection!=ids.stations[slot-1]
        || !Capture(pin,lobby,before,ids.capacity))return false;
    const auto* open=OpenHumanRow(pin,before,target);
    if(!open || CrossplayChat::Word(open+0x5c)!=UINT32_MAX || !ClaimOpenHumanRow(pin,before,target))return false;
    unsigned char* claimed=nullptr;
    return RoomReadyRow(edition,binding,admission,ids,slot,after,target,claimed) && claimed==open
        && Capture(pin,after,verified,ids.capacity) && Same(before,verified);
}
}
inline bool ApplyHistoryRoomReady(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
        const CrossplayNative::RoomLobbyAdmission& admission,const CrossplaySessionConfig::RoomConnections& ids,
        unsigned char slot,unsigned char ready,PendingHistoryRoomReady& pending) {
    if(pending.session || ready>1)return false;
    CrossplayNative::LobbySnapshot lobby,current;
    CrossplayNative::Participant target,verified;
    unsigned char* row=nullptr;
    unsigned char* current_row=nullptr;
    CrossplayCastleSetup::Value design;
    uint32_t extra=0;
    CrossplayNative::ParticipantEvent event;
    if(!event.Bind(edition))return false;
    if(ReadyStateDetail::RoomReadyRow(edition,binding,admission,ids,slot,lobby,target,row)) {
        if(!binding.CaptureCastleSetup(row+0x55c,design))return false;
        extra=CrossplayChat::Word(row+0x30);
    } else if(!ReadyStateDetail::ClaimRoomReadyRow(edition,binding,admission,ids,slot,design,extra)
        || !ReadyStateDetail::RoomReadyRow(edition,binding,admission,ids,slot,lobby,target,row))return false;
    if(!event.ConstructConfirmedSetup(target,design,extra,ready!=0)
        || !ReadyStateDetail::RoomReadyRow(edition,binding,admission,ids,slot,current,verified,current_row)
        || current.instance!=lobby.instance || current_row!=row || !event.EnqueueLocal())return false;
    ReadyStateDetail::SetReady(edition,row,ready);
    pending={lobby.instance,reinterpret_cast<uintptr_t>(row),slot,ready};
    return true;
}
inline ReadyObservation ObserveHistoryRoomReady(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
        const CrossplayNative::RoomLobbyAdmission& admission,const CrossplaySessionConfig::RoomConnections& ids,
        const PendingHistoryRoomReady& pending) {
    CrossplayNative::LobbySnapshot lobby;
    CrossplayNative::Participant target;
    unsigned char* row=nullptr;
    if(!pending.session || !ReadyStateDetail::RoomReadyRow(edition,binding,admission,ids,pending.slot,lobby,target,row)
        || lobby.instance!=pending.session || reinterpret_cast<uintptr_t>(row)!=pending.row)return ReadyObservation::Invalid;
    return row[0x71]==pending.value?ReadyObservation::Applied:ReadyObservation::Pending;
}
#endif
}
