#pragma once
#include "crossplay_chat.h"

// A genuine guest-originated toggle request, never a supplied Ready result.
// The original host decides whether and how to process it. Native slot 2 is
// already assigned by the separate authenticated two-human admission path.
namespace CrossplayReady {
constexpr uint32_t Class=0x42102439,Type=7u|(29u<<16);
constexpr size_t Header=44,WireSize=13,NativeStateSize=18;
inline bool Validate(const std::vector<unsigned char>& wire,uint32_t guest) {
    return guest!=UINT32_MAX && wire.size()==WireSize
        && CrossplayChat::Word(wire.data())==Class && CrossplayChat::Word(wire.data()+4)==Type
        && CrossplayChat::Word(wire.data()+8)==guest && wire[12]==1;
}
inline bool NativeState(const std::vector<unsigned char>& wire,uint32_t guest) {
    return wire.size()==NativeStateSize && Validate(std::vector<unsigned char>(wire.begin(),wire.begin()+WireSize),guest)
        && CrossplayChat::Word(wire.data()+13)==2 && wire[17]<=1;
}
inline bool Encode(const Crossplay::Peer& peer,const std::vector<unsigned char>& wire,
                   uint32_t guest,std::vector<unsigned char>& frame) {
    frame.clear();if(!Validate(wire,guest))return false;
    const unsigned char magic[8]={'S','7','R','E','A','D','Y',1};frame.assign(magic,magic+8);
    frame.insert(frame.end(),peer.local_id.begin(),peer.local_id.end());
    frame.insert(frame.end(),peer.remote_id.begin(),peer.remote_id.end());
    frame.resize(Header);CrossplayChat::Put(frame.data()+40,WireSize);
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame,
                   uint32_t source_guest,uint32_t local_guest,std::vector<unsigned char>& wire) {
    wire.clear();const unsigned char magic[8]={'S','7','R','E','A','D','Y',1};
    if(local_guest==UINT32_MAX || frame.size()!=Header+WireSize || memcmp(frame.data(),magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)!=WireSize)return false;
    std::vector<unsigned char> decoded(frame.begin()+Header,frame.end());
    if(!Validate(decoded,source_guest))return false;
    // History's real toggle caller leaves NetworkSlot/ReadyState uninitialized.
    // They are absent from this intent frame. Only the authenticated sender's
    // routing CID changes; this frame contains no desired or achieved state.
    CrossplayChat::Put(decoded.data()+8,local_guest);wire=std::move(decoded);return true;
}
}
