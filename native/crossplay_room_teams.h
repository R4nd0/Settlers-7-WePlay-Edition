#pragma once
#include "crossplay_ai_preparation_source.h"
#include "crossplay_player_option_codec.h"

namespace CrossplayRoomTeams {
using Teams=std::array<unsigned char,CrossplaySessionConfig::Slots>;
constexpr unsigned char Offer[]={'S','7','R','T','E','A','M',1};
constexpr unsigned char Applied[]={'S','7','R','T','E','A','M',2};
constexpr unsigned char Committed[]={'S','7','R','T','E','A','M',3};
constexpr size_t Header=sizeof(Offer)+Crossplay::Digest{}.size();

inline bool Valid(const CrossplaySessionConfig::RoomConnections& routes,const Teams& teams,uint32_t ai_slot=0) {
    if(!CrossplaySessionConfig::ValidRoomConnections(routes)
        || (ai_slot && (ai_slot>routes.capacity || routes.stations[ai_slot-1]!=UINT32_MAX)))return false;
    for(size_t i=0;i<teams.size();++i) {
        const bool occupied=routes.stations[i]!=UINT32_MAX || i+1==ai_slot;
        if(occupied ? (!teams[i] || teams[i]>routes.capacity) : teams[i]!=0)return false;
    }
    return true;
}
inline bool Frame(const unsigned char (&kind)[8],const Crossplay::Digest& config,
                  const CrossplaySessionConfig::RoomConnections& routes,const Teams& teams,
                  std::vector<unsigned char>& output,uint32_t ai_slot=0) {
    output.clear();
    if(!Valid(routes,teams,ai_slot))return false;
    output.assign(kind,kind+8);
    output.insert(output.end(),config.begin(),config.end());
    output.insert(output.end(),teams.begin(),teams.end());
    return true;
}
inline bool Decode(const unsigned char (&kind)[8],const std::vector<unsigned char>& frame,
                   const Crossplay::Digest& config,const CrossplaySessionConfig::RoomConnections& routes,Teams& teams,uint32_t ai_slot=0) {
    teams={};
    if(frame.size()!=Header+teams.size() || memcmp(frame.data(),kind,8)
        || memcmp(frame.data()+8,config.data(),config.size()))return false;
    Teams candidate;
    std::copy(frame.begin()+Header,frame.end(),candidate.begin());
    if(!Valid(routes,candidate,ai_slot))return false;
    teams=candidate;
    return true;
}

inline bool Capture(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                    const CrossplayNative::RoomLobbyAdmission& admission,const CrossplaySessionConfig::RoomConnections& routes,
                    uintptr_t session,Teams& teams,uint32_t ai_slot=0) {
    teams={};
    CrossplaySessionConfig::RoomConnections actual;
    CrossplayNative::LobbySnapshot lobby;
    CrossplayAIPreparationSource::View view;
    unsigned stage=0;
    if(!session || !admission.CaptureConfigurationRoutes(routes.capacity,actual)
        || actual.host!=routes.host || actual.stations!=routes.stations
        || !binding.CaptureLobby(lobby) || lobby.instance!=session
        || !CrossplayAIPreparationSource::CaptureView(edition,routes.capacity,view,stage)
        || view.getrows!=view.rows)return false;
    Teams observed{};
    for(const auto& member:lobby.members){
        const auto& player=member.player;
        if(!player.slot || player.slot>routes.capacity || routes.stations[player.slot-1]!=player.connection)return false;
        const auto row=CrossplayAIPreparationSource::ReadRow(view,player.slot,true);
        if(row.slot!=player.slot || row.controller!=3 || row.kind!=1
            || row.profile!=(player.principal?player.principal:player.slot) || row.team>routes.capacity)return false;
        observed[player.slot-1]=static_cast<unsigned char>(row.team);
    }
    if(ai_slot){
        if(ai_slot>routes.capacity || routes.stations[ai_slot-1]!=UINT32_MAX)return false;
        const auto cpu=CrossplayAIPreparationSource::ReadRow(view,ai_slot,true);
        if(cpu.slot!=ai_slot || cpu.controller!=1 || cpu.kind!=2 || cpu.team>routes.capacity)return false;
        observed[ai_slot-1]=static_cast<unsigned char>(cpu.team);
    }
    if(!Valid(routes,observed,ai_slot) || !view.Stable()
        || !admission.CaptureConfigurationRoutes(routes.capacity,actual)
        || actual.host!=routes.host || actual.stations!=routes.stations)return false;
    teams=observed;
    return true;
}

inline bool Deliver(Crossplay::Edition edition,const CrossplayNative::ParticipantEvent& binding,
                    const CrossplayNative::RoomLobbyAdmission& admission,const CrossplaySessionConfig::RoomConnections& routes,
                    uintptr_t session,const Teams& expected,uint32_t ai_slot=0) {
    namespace Option=CrossplayNativeCodec::PlayerOptionDetail;
    Option::Spec spec{};
    Teams actual;
    if(!Valid(routes,expected,ai_slot) || !Capture(edition,binding,admission,routes,session,actual,ai_slot)
        || !Option::Layout(edition,CrossplayPlayerOption::Team,spec))return false;
    for(size_t i=0;i<expected.size();++i){
        if(expected[i]==actual[i])continue;
        std::vector<unsigned char> wire(18);
        CrossplayChat::Put(wire.data(),spec.klass);
        CrossplayChat::Put(wire.data()+4,spec.type);
        CrossplayChat::Put(wire.data()+8,routes.host);
        wire[12]=1;
        CrossplayChat::Put(wire.data()+13,static_cast<uint32_t>(i+1));
        wire[17]=expected[i];
        if(!Option::DeliverBound(edition,spec,wire,[&]{
            Teams current;
            return Capture(edition,binding,admission,routes,session,current,ai_slot);
        }))return false;
    }
    return true;
}
}
