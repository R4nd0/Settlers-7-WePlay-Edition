#pragma once
#include "crossplay_chat.h"

// Only captured original end-of-turn batches. All turn/checksum scalars stay
// opaque and bit-exact. This is not command transport or a synchronization proof.
namespace CrossplayTurnBatch {
constexpr uint32_t Class=0x02729889,Type=7u|(9u<<16),NestedType=7u|(8u<<16);
constexpr size_t Header=48,BaseWire=21,ItemWire=29,MaxItems=64,MaxWire=BaseWire+ItemWire*MaxItems;
inline constexpr unsigned char Magic[8]={'S','7','T','B','A','T',0,1};
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t source) {
    if(source==UINT32_MAX || wire.size()<BaseWire || wire.size()>MaxWire
        || CrossplayChat::Word(wire.data())!=Class || CrossplayChat::Word(wire.data()+4)!=Type
        || CrossplayChat::Word(wire.data()+8)!=source || wire[12]>1)return false;
    const uint32_t count=CrossplayChat::Word(wire.data()+17);
    if(count>MaxItems || wire.size()!=BaseWire+ItemWire*count)return false;
    for(uint32_t i=0;i<count;++i) {
        const auto* item=wire.data()+BaseWire+i*ItemWire;
        if(CrossplayChat::Word(item)!=NestedType || CrossplayChat::Word(item+4)!=source || item[8]>1)return false;
    }
    return true;
}
inline bool Encode(const Crossplay::Peer& peer,uint32_t sequence,const std::vector<unsigned char>& wire,
                   uint32_t source,std::vector<unsigned char>& frame) {
    frame.clear();if(!sequence || !Inspect(wire,source))return false;
    frame.assign(Header,0);memcpy(frame.data(),Magic,8);
    memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayChat::Put(frame.data()+40,sequence);CrossplayChat::Put(frame.data()+44,static_cast<uint32_t>(wire.size()));
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,uint32_t previous,const std::vector<unsigned char>& frame,
                   uint32_t source,uint32_t local_source,std::vector<unsigned char>& wire) {
    wire.clear();
    if(previous==UINT32_MAX || local_source==UINT32_MAX || frame.size()<Header+BaseWire || frame.size()>Header+MaxWire
        || memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || CrossplayChat::Word(frame.data()+40)!=previous+1
        || CrossplayChat::Word(frame.data()+44)!=frame.size()-Header)return false;
    std::vector<unsigned char> candidate(frame.begin()+Header,frame.end());
    if(!Inspect(candidate,source))return false;
    CrossplayChat::Put(candidate.data()+8,local_source);
    const uint32_t count=CrossplayChat::Word(candidate.data()+17);
    for(uint32_t i=0;i<count;++i)CrossplayChat::Put(candidate.data()+BaseWire+i*ItemWire+4,local_source);
    if(!Inspect(candidate,local_source))return false;
    wire=std::move(candidate);return true;
}
}
