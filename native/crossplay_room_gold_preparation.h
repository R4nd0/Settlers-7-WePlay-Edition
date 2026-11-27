#pragma once
#include "crossplay_human_profile_selection.h"

namespace CrossplayRoomGoldPreparation {
enum class Completion { BindRows, QueueConfirmations };

inline bool Apply(const CrossplayNative::ParticipantEvent& binding,const CrossplayNative::RoomLobbyAdmission& admission,
                  const CrossplaySessionConfig::RoomConnections& routes,uintptr_t session,Completion completion) {
    using namespace CrossplayNative;
    using namespace GoldGuestIdentityDetail;
    using CrossplayNativeCodec::Detail::Readable;
    using CrossplayNativeCodec::SessionConfigDetail::NativeString;
    constexpr auto edition=Crossplay::Edition::Gold;
    constexpr size_t stride=0x5c4,bytes=25*stride;
    LobbySnapshot lobby;
    CrossplayAIPreparationSource::View view;
    CrossplaySessionConfig::RoomConnections actual;
    unsigned stage=0;
    if(!CrossplayHumanProfileSelection::Methods() || !binding.CaptureLobby(lobby)
        || lobby.host || lobby.instance!=session || !admission.CaptureConfigurationRoutes(routes.capacity,actual)
        || actual.stations!=routes.stations || actual.host!=routes.host
        || !CrossplayAIPreparationSource::CaptureView(edition,routes.capacity,view,stage) || view.getrows!=view.rows
        || !Readable(view.rows,bytes))return false;
    struct Entry {
        Participant player;
        CrossplayCastleSetup::Value castle;
        uint32_t extra=0,id=0;
        bool claim=false;
    };
    std::array<Entry,CrossplaySessionConfig::Slots> entries;
    for(const auto& member:lobby.members){
        const auto slot=member.player.slot;
        if(!slot || slot>routes.capacity || routes.stations[slot-1]!=member.player.connection)return false;
        auto& entry=entries[slot-1];
        if(entry.id || !admission.CaptureMemberSetup(slot,entry.player,entry.castle,entry.extra)
            || entry.player.connection!=member.player.connection || entry.player.principal==UINT32_MAX
            || entry.player.name.empty() || entry.player.name.size()>64 || entry.player.name.find('\0')!=std::string::npos)return false;
        const auto current=CrossplayAIPreparationSource::ReadRow(view,slot,true);
        entry.claim=current.slot==UINT32_MAX && current.controller==1;
        if(current.kind!=1 || (!entry.claim && (current.slot!=slot || current.controller!=3)))return false;
        entry.id=entry.player.principal?entry.player.principal:slot;
        auto* row=view.rows+slot*stride;
        if(!HistoryRoleDetail::Writable(row+0x18,20) || !HistoryRoleDetail::Writable(row+0x5c,8))return false;
        const auto& previous=*reinterpret_cast<const Text*>(row+0x1c);
        if(!Storage(previous))return false;
        if(previous.data)for(size_t i=0;i<25;++i)
            if(i!=slot && *reinterpret_cast<const char* const*>(view.rows+i*stride+0x1c)==previous.data)return false;
    }
    ParticipantEvent confirmed;
    if(!view.Stable() || (completion==Completion::QueueConfirmations && !confirmed.Bind(edition)))return false;
    const std::vector<unsigned char> original(view.rows,view.rows+bytes);
    for(const auto& entry:entries){
        if(!entry.id)continue;
        const auto slot=entry.player.slot;
        auto* row=view.rows+slot*stride;
        if(entry.claim){
            auto* configuration=const_cast<unsigned char*>(view.writer);
            GoldGuestRowDetail::PrepareWith(0x9dfec0,configuration,slot);
            if(CrossplayChat::Word(row+0x60)!=2 || !GoldGuestRowDetail::ClaimWith(0x9dff10,configuration,slot,slot))return false;
        }
        CrossplayGoldClosedPreparation::SetWith(0x5b1f90,row,entry.id);
        const auto& name=entry.player.name;
        const Text value{name.c_str(),static_cast<uint32_t>(name.size()+1),static_cast<uint32_t>(name.size()),0};
        if(CopyWith(0x415ef0,row+0x1c,&value)!=row+0x1c)return false;
        const auto& copied=*reinterpret_cast<const Text*>(row+0x1c);
        std::string observed;
        if(!Storage(copied) || copied.data==value.data || !NativeString(row+0x1c,observed) || observed!=name
            || CrossplayChat::Word(row+0x18)!=entry.id || CrossplayChat::Word(row+0x5c)!=slot
            || CrossplayChat::Word(row+0x60)!=3 || CrossplayChat::Word(row+0x64)!=1)return false;
    }
    if(!view.Stable() || !admission.CaptureConfigurationRoutes(routes.capacity,actual)
        || actual.stations!=routes.stations || actual.host!=routes.host)return false;
    for(size_t i=0;i<bytes;++i){
        const size_t slot=i/stride,field=i%stride;
        if(slot && slot<=routes.capacity && entries[slot-1].id
            && ((field>=0x18 && field<0x2c) || (field>=0x5c && field<0x64)))continue;
        if(original[i]!=view.rows[i])return false;
    }
    if(completion==Completion::BindRows)return true;
    for(const auto& entry:entries)
        if(entry.id && (!confirmed.ConstructConfirmedSetup(entry.player,entry.castle,entry.extra)
            || !confirmed.EnqueueLocal() || !confirmed.Reset()))return false;
    return true;
}
}
