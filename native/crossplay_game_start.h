#pragma once
#include "crossplay_session_config.h"
#include <algorithm>

// Experimental fresh-game event transport. The caller must independently
// compare actual content and the complete native setup before supplying its
// revision/digest. A digest alone is not evidence of compatible game rules.
// No resume path, Ready acknowledgement, world or turn result is synthesized.
namespace CrossplayGameStart {
constexpr uint32_t Class=0x8bf00ae9,Type=7u|(7u<<16);
constexpr size_t WireSize=21,Header=76,FrameSize=Header+WireSize;
struct Agreement {uint32_t revision=0;Crossplay::Digest setup{};};
inline bool Valid(const Agreement& value) {
    return value.revision && std::any_of(value.setup.begin(),value.setup.end(),[](unsigned char x){return x!=0;});
}
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t host) {
    return host!=UINT32_MAX && wire.size()==WireSize && CrossplayChat::Word(wire.data())==Class
        && CrossplayChat::Word(wire.data()+4)==Type && CrossplayChat::Word(wire.data()+8)==host
        && wire[12]==1 && !memcmp(wire.data()+13,"Str_",4) && !CrossplayChat::Word(wire.data()+17);
}
inline bool Encode(const Crossplay::Peer& peer,const Agreement& agreement,
                   const std::vector<unsigned char>& wire,uint32_t host,std::vector<unsigned char>& frame) {
    frame.clear();
    if(!Valid(agreement) || !Inspect(wire,host))return false;
    const unsigned char magic[8]={'S','7','S','T','A','R',0,1};frame.assign(magic,magic+8);
    frame.insert(frame.end(),peer.local_id.begin(),peer.local_id.end());
    frame.insert(frame.end(),peer.remote_id.begin(),peer.remote_id.end());
    frame.resize(Header);CrossplayChat::Put(frame.data()+40,agreement.revision);
    memcpy(frame.data()+44,agreement.setup.data(),agreement.setup.size());
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,const Agreement& agreement,
                   const std::vector<unsigned char>& frame,uint32_t source_host,uint32_t local_host,
                   std::vector<unsigned char>& wire) {
    wire.clear();const unsigned char magic[8]={'S','7','S','T','A','R',0,1};
    if(!Valid(agreement) || local_host==UINT32_MAX || frame.size()!=FrameSize || memcmp(frame.data(),magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)!=agreement.revision
        || memcmp(frame.data()+44,agreement.setup.data(),agreement.setup.size()))return false;
    std::vector<unsigned char> decoded(frame.begin()+Header,frame.end());
    if(!Inspect(decoded,source_host))return false;
    // Native event type, asynchronous flag and empty fresh-game path are kept.
    // Only the independently admitted numeric host routing CID is projected.
    CrossplayChat::Put(decoded.data()+8,local_host);
    if(!Inspect(decoded,local_host))return false;
    wire=std::move(decoded);return true;
}
}
