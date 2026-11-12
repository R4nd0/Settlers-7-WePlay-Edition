#pragma once
#include "crossplay_chat.h"
#include <algorithm>

// A diagnostic snapshot of the original global-rules and template-player
// writers. It excludes active lobby options, other map/template files and
// gameplay code. Equality never authorizes a start or proves determinism.
namespace CrossplaySetupSignature {
constexpr size_t FrameSize=80;
constexpr uint32_t MaximumWriterBytes=65536;
struct Signature {Crossplay::Digest digest{};uint32_t rules=0,players=0;};
inline bool Valid(const Signature& value) {
    return value.rules && value.rules<=MaximumWriterBytes && value.players && value.players<=MaximumWriterBytes
        && std::any_of(value.digest.begin(),value.digest.end(),[](unsigned char x){return x!=0;});
}
inline bool Equal(const Signature& a,const Signature& b) {
    return Valid(a) && Valid(b) && a.rules==b.rules && a.players==b.players && a.digest==b.digest;
}
inline bool Encode(const Crossplay::Peer& peer,const Signature& value,std::vector<unsigned char>& frame) {
    frame.clear();if(!Valid(value))return false;
    const unsigned char magic[8]={'S','7','S','S','I','G','N',1};frame.resize(FrameSize);
    memcpy(frame.data(),magic,8);memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayChat::Put(frame.data()+40,value.rules);CrossplayChat::Put(frame.data()+44,value.players);
    memcpy(frame.data()+48,value.digest.data(),32);return true;
}
// Call only after the existing authenticated channel accepts this peer/frame.
// The owner must retire a snapshot when any relevant setup changes; this codec
// deliberately makes no claim about its freshness, readiness or completeness.
inline bool Decode(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame,Signature& value) {
    value={};const unsigned char magic[8]={'S','7','S','S','I','G','N',1};
    if(frame.size()!=FrameSize || memcmp(frame.data(),magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16))return false;
    Signature decoded{};decoded.rules=CrossplayChat::Word(frame.data()+40);decoded.players=CrossplayChat::Word(frame.data()+44);
    memcpy(decoded.digest.data(),frame.data()+48,32);if(!Valid(decoded))return false;value=decoded;return true;
}
}
