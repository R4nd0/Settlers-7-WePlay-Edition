#pragma once
#include "crossplay_setup_signature_codec.h"
#include <algorithm>

// Opt-in framing for original enclosing setup bytes on an authenticated Channel.
// It never parses/applies native objects, acknowledges Ready or authorizes a
// world. The caller must bind the descriptor to its actual owned checkpoint.
namespace CrossplaySetupRootStream {
constexpr size_t MaximumBytes=131072,ChunkBytes=16384,Header=92;
static_assert(Header+ChunkBytes<=Crossplay::Channel::MaximumPayload,"root chunk exceeds channel limit");
struct Descriptor {
    uint32_t revision=0,format=0,bytes=0;
    Crossplay::Digest digest{};
};
struct Complete {
    Descriptor descriptor{};
    std::vector<unsigned char> bytes;
};
inline bool Valid(const Descriptor& value) {
    return value.revision && (value.format==1 || value.format==2) && value.bytes && value.bytes<=MaximumBytes
        && std::any_of(value.digest.begin(),value.digest.end(),[](unsigned char b){return b!=0;});
}
inline bool Valid(const Crossplay::Peer& peer) {
    const auto nonzero=[](const Crossplay::Id& id){return std::any_of(id.begin(),id.end(),[](unsigned char b){return b!=0;});};
    return peer.local_id!=peer.remote_id && nonzero(peer.local_id) && nonzero(peer.remote_id);
}
inline bool Encode(const Crossplay::Peer& peer,uint32_t revision,uint32_t format,
                   const std::vector<unsigned char>& bytes,std::vector<std::vector<unsigned char>>& frames,
                   Descriptor* descriptor=nullptr) {
    frames.clear();if(descriptor)*descriptor={};
    if(!Valid(peer) || !revision || (format!=1 && format!=2) || bytes.empty() || bytes.size()>MaximumBytes)return false;
    Descriptor value{revision,format,static_cast<uint32_t>(bytes.size()),{}};
    if(!CrossplaySetupSignatureNative::Hash(bytes,value.digest) || !Valid(value))return false;
    const unsigned char magic[8]={'S','7','R','O','O','T',0,1};
    for(size_t offset=0;offset<bytes.size();offset+=ChunkBytes) {
        const size_t length=(std::min)(ChunkBytes,bytes.size()-offset);
        std::vector<unsigned char> frame(Header+length);
        memcpy(frame.data(),magic,8);memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
        CrossplayChat::Put(frame.data()+40,value.revision);CrossplayChat::Put(frame.data()+44,value.format);
        CrossplayChat::Put(frame.data()+48,value.bytes);CrossplayChat::Put(frame.data()+52,static_cast<uint32_t>(offset));
        CrossplayChat::Put(frame.data()+56,static_cast<uint32_t>(length));memcpy(frame.data()+60,value.digest.data(),32);
        memcpy(frame.data()+Header,bytes.data()+offset,length);frames.push_back(std::move(frame));
    }
    if(descriptor)*descriptor=value;return true;
}
class Receiver {
public:
    enum class State { New,Receiving,Complete,Consumed,Failed };
private:
    State state=State::New;
    Crossplay::Peer peer{};
    Descriptor expected{};
    std::vector<unsigned char> bytes;
    bool Fail(){bytes.clear();state=State::Failed;return false;}
public:
    Receiver()=default;
    Receiver(const Receiver&)=delete;
    Receiver& operator=(const Receiver&)=delete;
    State GetState()const{return state;}
    bool Bind(const Crossplay::Peer& other,const Descriptor& descriptor) {
        if(state!=State::New || !Valid(other) || !Valid(descriptor))return Fail();
        peer=other;expected=descriptor;bytes.reserve(expected.bytes);state=State::Receiving;return true;
    }
    bool Accept(const std::vector<unsigned char>& frame) {
        if(state!=State::Receiving || frame.size()<Header || frame.size()>Header+ChunkBytes)return Fail();
        const unsigned char magic[8]={'S','7','R','O','O','T',0,1};
        if(memcmp(frame.data(),magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
            || memcmp(frame.data()+24,peer.local_id.data(),16)
            || CrossplayChat::Word(frame.data()+40)!=expected.revision || CrossplayChat::Word(frame.data()+44)!=expected.format
            || CrossplayChat::Word(frame.data()+48)!=expected.bytes || CrossplayChat::Word(frame.data()+52)!=bytes.size()
            || memcmp(frame.data()+60,expected.digest.data(),32))return Fail();
        const size_t length=CrossplayChat::Word(frame.data()+56);
        if(bytes.size()>=expected.bytes || length!=(std::min)(ChunkBytes,expected.bytes-bytes.size())
            || frame.size()!=Header+length)return Fail();
        bytes.insert(bytes.end(),frame.begin()+Header,frame.end());
        if(bytes.size()==expected.bytes) {
            Crossplay::Digest digest{};
            if(!CrossplaySetupSignatureNative::Hash(bytes,digest) || digest!=expected.digest)return Fail();
            state=State::Complete;
        }
        return true;
    }
    bool Take(Complete& result) {
        result={};if(state!=State::Complete)return false;
        result.descriptor=expected;result.bytes=std::move(bytes);state=State::Consumed;return true;
    }
};
}
