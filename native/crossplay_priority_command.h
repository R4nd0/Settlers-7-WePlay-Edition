#pragma once
#include "crossplay_chat.h"

// Original CLogicStateEvent containing one CTaskPrioritizeEvent. The transport
// retains full task handles and native command ordering; it never edits a ledger.
namespace CrossplayPriorityCommand {
inline constexpr uint32_t Class=0xdda00d09,Type=7u|(11u<<16),NestedClass=0x18904619,PriorityType=2u|(22u<<16);
inline constexpr size_t Header=48,WireSize=35,FrameSize=Header+WireSize;
inline constexpr unsigned char Magic[8]={'S','7','P','R','I','O',0,1};
inline bool PlayerSlot(uint32_t player){return player>=1 && player<=8;}
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t source,uint32_t player){
    if(source==UINT32_MAX || !PlayerSlot(player) || wire.size()!=WireSize)return false;
    const auto* p=wire.data();const auto id=CrossplayChat::Word(p+22),relative=CrossplayChat::Word(p+26);
    return CrossplayChat::Word(p)==Class && CrossplayChat::Word(p+4)==Type && CrossplayChat::Word(p+8)==source && p[12]<=1
        && CrossplayChat::Word(p+13)==NestedClass && CrossplayChat::Word(p+17)==PriorityType && p[21]==player
        && id && id!=UINT32_MAX && relative!=UINT32_MAX && id!=relative && CrossplayChat::Word(p+30)==player && p[34]<=1;
}
inline bool Encode(const Crossplay::Peer& peer,uint32_t sequence,const std::vector<unsigned char>& wire,uint32_t source,uint32_t player,std::vector<unsigned char>& output){
    output.clear();if(!sequence || !Inspect(wire,source,player))return false;
    std::vector<unsigned char> frame(Header,0);memcpy(frame.data(),Magic,8);memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayChat::Put(frame.data()+40,sequence);CrossplayChat::Put(frame.data()+44,WireSize);frame.insert(frame.end(),wire.begin(),wire.end());output=std::move(frame);return true;
}
inline bool Decode(const Crossplay::Peer& peer,uint32_t previous,const std::vector<unsigned char>& frame,uint32_t source,uint32_t local_source,uint32_t player,std::vector<unsigned char>& output){
    output.clear();
    if(previous==UINT32_MAX || local_source==UINT32_MAX || frame.size()!=FrameSize || memcmp(frame.data(),Magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)!=previous+1 || CrossplayChat::Word(frame.data()+44)!=WireSize)return false;
    std::vector<unsigned char> wire(frame.begin()+Header,frame.end());if(!Inspect(wire,source,player))return false;
    CrossplayChat::Put(wire.data()+8,local_source);if(!Inspect(wire,local_source,player))return false;output=std::move(wire);return true;
}
}
