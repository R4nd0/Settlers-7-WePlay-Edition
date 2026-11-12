#pragma once
#include "crossplay_ready.h"

// An authenticated snapshot of the Gold host's actually consumed configuration.
// No requested state, fabricated consumer acknowledgement or permission to start.
namespace CrossplayReadyState {
constexpr size_t Size=50;
struct Snapshot { uint32_t revision=0; unsigned char host=0,guest=0; };
inline bool Valid(const Snapshot& value) {return value.revision && value.host<=1 && value.guest<=1;}
inline bool Encode(const Crossplay::Peer& peer,const Snapshot& value,std::vector<unsigned char>& frame) {
    frame.clear();if(!Valid(value))return false;
    const unsigned char magic[8]={'S','7','R','S','T','A','T',1};frame.assign(magic,magic+8);
    frame.insert(frame.end(),peer.local_id.begin(),peer.local_id.end());
    frame.insert(frame.end(),peer.remote_id.begin(),peer.remote_id.end());
    frame.resize(Size);CrossplayChat::Put(frame.data()+40,6);CrossplayChat::Put(frame.data()+44,value.revision);
    frame[48]=value.host;frame[49]=value.guest;return true;
}
inline bool Decode(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame,uint32_t previous,Snapshot& result) {
    result={};const unsigned char magic[8]={'S','7','R','S','T','A','T',1};
    if(frame.size()!=Size || memcmp(frame.data(),magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || CrossplayChat::Word(frame.data()+40)!=6)return false;
    const Snapshot value{CrossplayChat::Word(frame.data()+44),frame[48],frame[49]};
    if(!Valid(value) || previous==UINT32_MAX || value.revision!=previous+1)return false;
    result=value;return true;
}
}
