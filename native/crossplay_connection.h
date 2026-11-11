#pragma once
#include "crossplay_channel.h"
#include <algorithm>

namespace CrossplayNative {
// Numeric routing CIDs remain separate from principal IDs, network slots and
// Storm GUIDs. This is the same receiver-local projection used by admission.
inline bool ProjectConnection(Crossplay::Edition receiver,uint32_t receiver_own,
                              const Crossplay::Id& projection,uint32_t& result) {
    result=UINT32_MAX;
    if(receiver_own==UINT32_MAX)return false;
    uint32_t candidate=0;
    if(receiver==Crossplay::Edition::Gold) {
        for(unsigned i=0;i<4;++i)candidate|=uint32_t(projection[i])<<(8*i);
        if(!receiver_own || !candidate || candidate>0x7fffffffu
            || std::any_of(projection.begin()+4,projection.end(),[](unsigned char c){return c!=0;}))return false;
    } else if(receiver==Crossplay::Edition::UbisoftHistory || receiver==Crossplay::Edition::SteamHistory) {
        candidate=receiver_own==0?1u:0u;
    } else return false;
    if(candidate==receiver_own)return false;
    result=candidate;return true;
}
}
