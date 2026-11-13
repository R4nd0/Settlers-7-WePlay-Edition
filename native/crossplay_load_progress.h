#pragma once
#include "crossplay_chat.h"

// Original load-progress messages only. Never invent a completion/Ready value.
// The authenticated channel and an independently owned started-world interval
// are prerequisites. A forwarded scalar does not establish simulation sync.
namespace CrossplayLoadProgress {
// Native outgoing observation: low subtype 7, class/category 10 in the high
// word. The runtime type is independent of the factory class identifier.
constexpr uint32_t Class=0xe1d414c9,Type=7u|(10u<<16);
constexpr size_t WireSize=17,Header=48,FrameSize=Header+WireSize;
inline constexpr unsigned char Magic[8]={'S','7','L','P','R','O','G',1};
inline bool Scalar(uint32_t bits) {
    // IEEE single precision: nonnegative finite 0..100, including negative
    // zero. Inspect bits without changing the game's FP state or rounding mode.
    return bits==0x80000000u || bits<=0x42c80000u;
}
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t source) {
    return source!=UINT32_MAX && wire.size()==WireSize && CrossplayChat::Word(wire.data())==Class
        && CrossplayChat::Word(wire.data()+4)==Type && CrossplayChat::Word(wire.data()+8)==source
        && wire[12]<=1 && Scalar(CrossplayChat::Word(wire.data()+13));
}
inline bool Encode(const Crossplay::Peer& peer,uint32_t sequence,const std::vector<unsigned char>& wire,
                   uint32_t source,std::vector<unsigned char>& frame) {
    frame.clear();if(!sequence || sequence>256 || !Inspect(wire,source))return false;
    frame.assign(FrameSize,0);memcpy(frame.data(),Magic,8);
    memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayChat::Put(frame.data()+40,sequence);CrossplayChat::Put(frame.data()+44,WireSize);
    memcpy(frame.data()+Header,wire.data(),wire.size());return true;
}
inline bool Decode(const Crossplay::Peer& peer,uint32_t previous,const std::vector<unsigned char>& frame,
                   uint32_t source,uint32_t local_source,std::vector<unsigned char>& wire) {
    wire.clear();
    if(previous>=256 || local_source==UINT32_MAX || frame.size()!=FrameSize || memcmp(frame.data(),Magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)!=previous+1 || CrossplayChat::Word(frame.data()+44)!=WireSize)return false;
    std::vector<unsigned char> candidate(frame.begin()+Header,frame.end());
    if(!Inspect(candidate,source))return false;
    CrossplayChat::Put(candidate.data()+8,local_source);
    if(!Inspect(candidate,local_source))return false;
    wire=std::move(candidate);return true;
}
}
