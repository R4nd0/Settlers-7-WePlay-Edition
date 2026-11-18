#pragma once
#include "crossplay_channel.h"
#include <algorithm>

namespace CrossplayNative {
inline bool GoldRoutingAlias(const Crossplay::Id& projection,uint32_t& result) {
    result=UINT32_MAX;
    uint32_t alias=0;
    for(unsigned i=0;i<4;++i)alias|=uint32_t(projection[i])<<(8*i);
    if(!alias || alias>0x7fffffffu
        || std::any_of(projection.begin()+4,projection.end(),[](unsigned char c){return c!=0;}))return false;
    result=alias;
    return true;
}
// Numeric routing CIDs remain separate from principal IDs, network slots and
// Storm GUIDs. This is the same receiver-local projection used by admission.
inline bool ProjectConnection(Crossplay::Edition receiver,uint32_t receiver_own,
                              const Crossplay::Id& projection,uint32_t& result) {
    result=UINT32_MAX;
    if(receiver_own==UINT32_MAX)return false;
    uint32_t candidate=0;
    if(receiver==Crossplay::Edition::Gold) {
        if(!receiver_own || !GoldRoutingAlias(projection,candidate))return false;
    } else if(receiver==Crossplay::Edition::UbisoftHistory || receiver==Crossplay::Edition::SteamHistory) {
        candidate=receiver_own==0?1u:0u;
    } else return false;
    if(candidate==receiver_own)return false;
    result=candidate;return true;
}

// Proposal in the virtual room's receiver-local number space. The original
// local CID is preserved. Require native roster readback before using it.
inline bool PlanRoomConnections(const Crossplay::RoomSnapshot& room,unsigned char local_slot,uint32_t local_cid,
        std::array<uint32_t,Crossplay::RoomSnapshot::MaximumMembers>& result) {
    result.fill(UINT32_MAX);
    if(room.member_count<2 || room.member_count>result.size() || !local_slot
        || local_slot>room.member_count || local_cid==UINT32_MAX)return false;
    const auto edition=room.members[local_slot-1].edition;
    const bool gold=edition==Crossplay::Edition::Gold;
    if(!gold && edition!=Crossplay::Edition::SteamHistory && edition!=Crossplay::Edition::UbisoftHistory)return false;
    if(gold && !local_cid)return false;
    auto planned=result;
    planned[local_slot-1]=local_cid;
    uint32_t next=0;
    for(size_t i=0;i<room.member_count;++i) {
        if(i+1==local_slot)continue;
        if(gold) {
            uint32_t alias=0;
            if(!GoldRoutingAlias(room.members[i].in_local_namespace,alias)
                || std::find(planned.begin(),planned.end(),alias)!=planned.end())return false;
            planned[i]=alias;
        } else {
            if(next==local_cid)++next;
            planned[i]=next++;
        }
    }
    result=planned;
    return true;
}
}
