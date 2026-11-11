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
template<class Read> bool Matches(const Layout& pin,const LobbySnapshot& snapshot,Read read) {
    if(!snapshot.instance || snapshot.host || snapshot.local_slot!=2 || snapshot.members.size()!=2)return false;
    const Participant* players[2]={};
    for(const auto& member:snapshot.members) {
        const auto& p=member.player;
        if(member.status!=2 || p.slot<1 || p.slot>2 || players[p.slot-1]
            || p.local!=(p.slot==2) || p.host!=(p.slot==1) || p.name.empty() || p.name.size()>128)return false;
        players[p.slot-1]=&p;
    }
    if(!players[0] || !players[1] || players[0]->connection==players[1]->connection)return false;
    const uint32_t ids[]={players[0]->principal?players[0]->principal:1,players[1]->principal?players[1]->principal:2};
    if(ids[0]==ids[1] || ids[0]==UINT32_MAX || ids[1]==UINT32_MAX)return false;
    auto value=[&](uint32_t address,auto& result) {
        return address>=0x10000 && address<=0x7fffffffu-sizeof(result) && read(address,&result,sizeof(result));
    };
    uint32_t manager=0,type=0,configuration=0,rows=0,duplicate=0;uint8_t mode=0,slots=0;
    if(!value(pin.manager_pointer,manager) || manager>0x7fffffff-0x190
        || !value(manager,type) || type!=pin.manager_type || !value(manager+4,mode) || mode>1
        || !value(manager+((mode-1)&1)*0xc4+0x98,configuration) || configuration>0x7fffffff-8
        || !value(configuration,duplicate) || !value(configuration+4,rows) || rows!=duplicate
        || rows>0x7fffffff-0x8dc0 || !value(rows+0x8db8,slots) || slots!=4)return false;
    for(unsigned i=0;i<2;++i) {
        const uint32_t row=rows+(i+1)*0x5a8;
        uint32_t id=0,slot=0,state=0,kind=0,data=0,capacity=0,length=0;
        if(!value(row+0x18,id) || id!=ids[i] || !value(row+0x5c,slot) || slot!=i+1
            || !value(row+0x60,state) || state!=3 || !value(row+0x64,kind) || kind!=1
            || !value(row+0x1c,data) || !value(row+0x20,capacity) || !value(row+0x24,length)
            || length!=players[i]->name.size() || capacity<length+1 || data<0x10000
            || data>0x7fffffff-length-1)return false;
        char name[129]{};
        if(!read(data,name,length+1) || memcmp(name,players[i]->name.c_str(),length+1))return false;
    }
    uint32_t backend=0,session=0,local=0,host=0;
    if(!value(pin.backend_pointer,backend) || backend>0x7fffffff-0xa50
        || !value(backend+0xa4c,session) || session>0x7fffffff-0x28
        || !value(session,type) || type!=pin.session_type
        || !value(session+0x20,local) || local!=players[1]->connection
        || !value(session+0x24,host) || host!=players[0]->connection)return false;
    // Re-read the ownership roots before accepting the snapshot.
    uint32_t current=0;uint8_t current_mode=0;
    return value(pin.manager_pointer,current) && current==manager && value(manager+4,current_mode) && current_mode==mode
        && value(manager+((mode-1)&1)*0xc4+0x98,current) && current==configuration
        && value(configuration+4,current) && current==rows
        && value(pin.backend_pointer,current) && current==backend && value(backend+0xa4c,current) && current==session;
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
