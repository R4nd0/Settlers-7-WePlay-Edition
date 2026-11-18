#pragma once
#include "crossplay_history_role.h"

// Experimental presentation gate for an admitted History guest. This is a
// read-only check, never a join result, Ready result or permission to start.
namespace CrossplayNative {
namespace HistoryPreparationDetail {
struct Layout { uint32_t manager_pointer,manager_type,backend_pointer,session_type; };
inline bool LayoutFor(Crossplay::Edition edition,Layout& result) {
    result={};
    if(edition==Crossplay::Edition::UbisoftHistory)result={0x13a5218,0x113ae54,0x1379e14,0x113fe90};
    else if(edition==Crossplay::Edition::SteamHistory)result={0x13a2300,0x113939c,0x1376f04,0x113e3ac};
    else return false;
    return true;
}
template<class Read> bool MatchGuestRows(const Layout& pin,const LobbySnapshot& snapshot,unsigned capacity,
                                        Read read,uint32_t& row_base,bool host_only=false) {
    row_base=0;
    if(!snapshot.instance || snapshot.host || capacity<2 || capacity>CrossplaySessionConfig::Slots
        || !snapshot.local_slot || snapshot.local_slot>capacity
        || snapshot.members.size()<2 || snapshot.members.size()>capacity)return false;
    std::array<const Participant*,CrossplaySessionConfig::Slots> players{};
    std::array<uint32_t,CrossplaySessionConfig::Slots> principals{};
    const Participant* host=nullptr;
    for(const auto& member:snapshot.members) {
        const auto& p=member.player;
        if(member.status!=2 || p.slot<1 || p.slot>capacity || players[p.slot-1]
            || p.local!=(p.slot==snapshot.local_slot) || (p.host && (p.local || host))
            || p.name.empty() || p.name.size()>128)return false;
        const uint32_t principal=p.principal?p.principal:p.slot;
        if(principal==UINT32_MAX)return false;
        for(size_t i=0;i<players.size();++i)
            if(players[i] && (principals[i]==principal || players[i]->connection==p.connection))return false;
        players[p.slot-1]=&p;
        principals[p.slot-1]=principal;
        if(p.host)host=&p;
    }
    const auto* local_player=players[snapshot.local_slot-1];
    if(!host || !local_player)return false;
    auto value=[&](uint32_t address,auto& result) {
        return address>=0x10000 && address<=0x7fffffffu-sizeof(result) && read(address,&result,sizeof(result));
    };
    uint32_t manager=0,type=0,configuration=0,rows=0,duplicate=0;uint8_t mode=0,slots=0;
    if(!value(pin.manager_pointer,manager) || manager>0x7fffffff-0x190
        || !value(manager,type) || type!=pin.manager_type || !value(manager+4,mode) || mode>1
        || !value(manager+((mode-1)&1)*0xc4+0x98,configuration) || configuration>0x7fffffff-8
        || !value(configuration,duplicate) || !value(configuration+4,rows) || rows!=duplicate
        || rows>0x7fffffff-0x8dc0 || !value(rows+0x8db8,slots) || slots!=capacity)return false;
    // A host-only update reads the same authenticated roster and backend,
    // but only needs the original preparation fields for its target row.
    for(unsigned i=0;i<capacity;++i) {
        if(!players[i] || (host_only && players[i]!=host))continue;
        const uint32_t row=rows+(i+1)*0x5a8;
        uint32_t id=0,slot=0,state=0,kind=0,data=0,name_capacity=0,length=0;
        if(!value(row+0x18,id) || id!=principals[i] || !value(row+0x5c,slot) || slot!=i+1
            || !value(row+0x60,state) || state!=3 || !value(row+0x64,kind) || kind!=1
            || !value(row+0x1c,data) || !value(row+0x20,name_capacity) || !value(row+0x24,length)
            || length!=players[i]->name.size() || name_capacity<length+1 || data<0x10000
            || data>0x7fffffff-length-1)return false;
        char name[129]{};
        if(!read(data,name,length+1) || memcmp(name,players[i]->name.c_str(),length+1))return false;
    }
    uint32_t backend=0,session=0,local_cid=0,host_cid=0;
    if(!value(pin.backend_pointer,backend) || backend>0x7fffffff-0xa50
        || !value(backend+0xa4c,session) || session>0x7fffffff-0x28
        || !value(session,type) || type!=pin.session_type
        || !value(session+0x20,local_cid) || local_cid!=local_player->connection
        || !value(session+0x24,host_cid) || host_cid!=host->connection)return false;
    // Re-read the ownership roots before accepting the snapshot.
    uint32_t current=0;uint8_t current_mode=0;
    if(!value(pin.manager_pointer,current) || current!=manager || !value(manager+4,current_mode) || current_mode!=mode
        || !value(manager+((mode-1)&1)*0xc4+0x98,current) || current!=configuration
        || !value(configuration+4,current) || current!=rows
        || !value(pin.backend_pointer,current) || current!=backend || !value(backend+0xa4c,current) || current!=session)return false;
    row_base=rows;return true;
}
template<class Read> bool Matches(const Layout& pin,const LobbySnapshot& snapshot,Read read) {
    if(snapshot.local_slot!=2 || snapshot.members.size()!=2)return false;
    for(const auto& member:snapshot.members)
        if(member.player.slot>2 || member.player.host!=(member.player.slot==1))return false;
    uint32_t rows=0;
    return MatchGuestRows(pin,snapshot,4,read,rows);
}
}
inline bool HistoryGuestPresentationReady(Crossplay::Edition edition,ParticipantEvent& binding,LobbyAdmission& admission) {
    HistoryPreparationDetail::Layout pin{};
    if(!HistoryPreparationDetail::LayoutFor(edition,pin)
        || admission.GetState()!=AdmissionState::RosterBound || !admission.Poll())return false;
    LobbySnapshot snapshot;
    if(!binding.CaptureLobby(snapshot))return false;
    return HistoryPreparationDetail::Matches(pin,snapshot,[](uint32_t address,void* output,size_t size) {
        const auto* input=reinterpret_cast<const void*>(address);
        if(!CrossplayNativeCodec::Detail::Readable(input,size))return false;
        memcpy(output,input,size);return true;
    });
}
}
