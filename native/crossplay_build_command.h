#pragma once
#include "crossplay_chat.h"

// Opt-in laboratory storehouse-placement primitive. Other gameplay commands
// are deliberately excluded until their original schemas and native paths pass.
namespace CrossplayBuildCommand {
constexpr uint32_t Class=0xdda00d09,Type=7u|(11u<<16),NestedClass=0x63a02a69;
constexpr uint32_t PlacementType=2u|(4u<<16),StorehouseType=260,BuildingCategory=14;
constexpr size_t Header=48,WireSize=47,FrameSize=Header+WireSize;
inline constexpr unsigned char Magic[8]={'S','7','B','U','I','L',0,1};
inline bool PositionBits(uint32_t bits){return (bits&0x7fffffffu)<=0x4b000000u;}
inline bool OrientationBits(uint32_t bits){return (bits&0x7fffffffu)<=0x43b40000u;}
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t source,uint32_t player) {
    if(source==UINT32_MAX || (player!=1 && player!=2) || wire.size()!=WireSize)return false;
    const auto* p=wire.data();
    return CrossplayChat::Word(p)==Class && CrossplayChat::Word(p+4)==Type
        && CrossplayChat::Word(p+8)==source && p[12]<=1
        && CrossplayChat::Word(p+13)==NestedClass && CrossplayChat::Word(p+17)==PlacementType
        && p[21]==player && PositionBits(CrossplayChat::Word(p+22))
        && PositionBits(CrossplayChat::Word(p+26)) && OrientationBits(CrossplayChat::Word(p+30))
        && CrossplayChat::Word(p+34)==StorehouseType && CrossplayChat::Word(p+38)==BuildingCategory
        && CrossplayChat::Word(p+42)==player && p[46]<=1;
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
    std::vector<unsigned char> candidate(frame.begin()+Header,frame.end());
    if(!Inspect(candidate,source,player))return false;
    CrossplayChat::Put(candidate.data()+8,local_source);
    if(!Inspect(candidate,local_source,player))return false;
    wire=std::move(candidate);return true;
}
}
