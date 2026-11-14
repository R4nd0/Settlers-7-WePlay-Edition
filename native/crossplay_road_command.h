#pragma once
#include "crossplay_build_command.h"

// Opt-in normal-street segment only. Native preview, placement, owner and map
// validation remain responsible for accepting this original player command.
namespace CrossplayRoadCommand {
constexpr uint32_t Class=CrossplayBuildCommand::Class,Type=CrossplayBuildCommand::Type;
constexpr uint32_t NestedClass=0x0bf3c089,PlacementType=2u|(11u<<16),NormalStreetType=1;
constexpr size_t Header=48,WireSize=47,FrameSize=Header+WireSize;
inline constexpr unsigned char Magic[8]={'S','7','R','O','A','D',0,1};
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t source,uint32_t player) {
    if(source==UINT32_MAX || (player!=1 && player!=2) || wire.size()!=WireSize)return false;
    const auto* p=wire.data();
    if(CrossplayChat::Word(p)!=Class || CrossplayChat::Word(p+4)!=Type || CrossplayChat::Word(p+8)!=source
        || p[12]>1 || CrossplayChat::Word(p+13)!=NestedClass || CrossplayChat::Word(p+17)!=PlacementType
        || p[21]!=player || CrossplayChat::Word(p+22)!=NormalStreetType || CrossplayChat::Word(p+42)!=player || p[46]>1)return false;
    for(size_t offset:{26u,30u,34u,38u})if(!CrossplayBuildCommand::PositionBits(CrossplayChat::Word(p+offset)))return false;
    return true;
}
inline bool Encode(const Crossplay::Peer& peer,uint32_t sequence,const std::vector<unsigned char>& wire,
                   uint32_t source,uint32_t player,std::vector<unsigned char>& frame) {
    frame.clear();if(!sequence || !Inspect(wire,source,player))return false;
    frame.assign(Header,0);memcpy(frame.data(),Magic,8);
    memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayChat::Put(frame.data()+40,sequence);CrossplayChat::Put(frame.data()+44,WireSize);
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,uint32_t previous,const std::vector<unsigned char>& frame,
                   uint32_t source,uint32_t local_source,uint32_t player,std::vector<unsigned char>& wire) {
    wire.clear();
    if(previous==UINT32_MAX || local_source==UINT32_MAX || frame.size()!=FrameSize
        || memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || CrossplayChat::Word(frame.data()+40)!=previous+1
        || CrossplayChat::Word(frame.data()+44)!=WireSize)return false;
    std::vector<unsigned char> candidate(frame.begin()+Header,frame.end());if(!Inspect(candidate,source,player))return false;
    CrossplayChat::Put(candidate.data()+8,local_source);if(!Inspect(candidate,local_source,player))return false;
    wire=std::move(candidate);return true;
}
}
