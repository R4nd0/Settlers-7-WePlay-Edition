#pragma once
#include "crossplay_chat.h"

namespace CrossplayDesyncTurn {
constexpr uint32_t Class=0xe972b5a9,Type=7u|(12u<<16),MaxSequence=1024;
constexpr size_t WireSize=17,Header=48,FrameSize=Header+WireSize;
inline constexpr unsigned char Magic[8]={'S','7','D','T','U','R',0,1};
// The final unsigned word is the original detection turn, including the native
// UINT32_MAX sentinel. It is never a locally generated checksum acknowledgement.
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t source) {
    return source!=UINT32_MAX && wire.size()==WireSize && CrossplayChat::Word(wire.data())==Class
        && CrossplayChat::Word(wire.data()+4)==Type && CrossplayChat::Word(wire.data()+8)==source
        && wire[12]<=1;
}
inline bool Encode(const Crossplay::Peer& peer,uint32_t sequence,const std::vector<unsigned char>& wire,
                   uint32_t source,std::vector<unsigned char>& frame) {
    frame.clear();if(!sequence || sequence>MaxSequence || !Inspect(wire,source))return false;
    frame.assign(FrameSize,0);memcpy(frame.data(),Magic,8);
    memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayChat::Put(frame.data()+40,sequence);CrossplayChat::Put(frame.data()+44,WireSize);
    memcpy(frame.data()+Header,wire.data(),wire.size());return true;
}
inline bool Decode(const Crossplay::Peer& peer,uint32_t previous,const std::vector<unsigned char>& frame,
                   uint32_t source,uint32_t local_source,std::vector<unsigned char>& wire) {
    wire.clear();
    if(previous>=MaxSequence || local_source==UINT32_MAX || frame.size()!=FrameSize || memcmp(frame.data(),Magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)!=previous+1 || CrossplayChat::Word(frame.data()+44)!=WireSize)return false;
    std::vector<unsigned char> candidate(frame.begin()+Header,frame.end());
    if(!Inspect(candidate,source))return false;
    CrossplayChat::Put(candidate.data()+8,local_source);
    if(!Inspect(candidate,local_source))return false;
    wire=std::move(candidate);return true;
}
}
