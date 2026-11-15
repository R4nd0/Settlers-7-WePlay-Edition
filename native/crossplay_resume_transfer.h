#pragma once
#include "crossplay_neo_agreement.h"
#include "crossplay_chat.h"
#include <algorithm>

// Experimental saved-session file transport only. The caller must establish
// this exact checkpoint from a validated native save and mutually admit it.
// Receiving all bytes never supplies native file acknowledgement or Ready.
namespace CrossplayResumeTransfer {
constexpr uint32_t Class=0x7e56fc59u,Type=7u|(0x24u<<16);
// Gold's original sender uses 8 KiB; paced History senders can use 1 KiB.
constexpr uint32_t MaximumFileBytes=64u*1024u*1024u,MaximumFragmentBytes=8192;
constexpr size_t NativeHeader=25,ContextHeader=196,Header=ContextHeader+4;
constexpr unsigned char Magic[8]={'S','7','R','F','I','L',0,2};
struct Checkpoint {
    Crossplay::Digest content{},setup{},file{},neo{};
    uint32_t logic_turn=0,replay_turn=0,bytes=0,features=0;
    bool enabled=false;
};
inline bool Nonzero(const Crossplay::Digest& value) {
    return std::any_of(value.begin(),value.end(),[](unsigned char x){return x!=0;});
}
inline bool Valid(const Checkpoint& value) {
    return Nonzero(value.content) && Nonzero(value.setup) && Nonzero(value.file) && Nonzero(value.neo)
        && value.logic_turn<UINT32_MAX && value.replay_turn==value.logic_turn+1
        && value.bytes>=80 && value.bytes<=MaximumFileBytes
        && (value.features&~CrossplayNeo::All)==0
        && (value.features&(CrossplayNeo::Fifo|CrossplayNeo::Intent|CrossplayNeo::PriorityCommands))
            ==(CrossplayNeo::Fifo|CrossplayNeo::Intent|CrossplayNeo::PriorityCommands);
}
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t source,uint32_t total) {
    if(source==UINT32_MAX || total<80 || total>MaximumFileBytes || wire.size()<NativeHeader
        || wire.size()>NativeHeader+MaximumFragmentBytes)return false;
    const auto* p=wire.data();const auto length=CrossplayChat::Word(p+13);
    return CrossplayChat::Word(p)==Class && CrossplayChat::Word(p+4)==Type
        && CrossplayChat::Word(p+8)==source && p[12]==1
        && length<=MaximumFragmentBytes && length<=total && wire.size()==NativeHeader+length
        && CrossplayChat::Word(p+17)==total;
}
inline void PutCheckpoint(unsigned char* p,const Checkpoint& value) {
    memcpy(p,value.content.data(),32);memcpy(p+32,value.setup.data(),32);
    memcpy(p+64,value.file.data(),32);memcpy(p+96,value.neo.data(),32);
    CrossplayChat::Put(p+128,value.logic_turn);CrossplayChat::Put(p+132,value.replay_turn);
    CrossplayChat::Put(p+136,value.bytes);CrossplayChat::Put(p+140,value.features);
    CrossplayChat::Put(p+144,value.enabled?1u:0u);
}
inline bool Encode(const Crossplay::Peer& peer,const Checkpoint& checkpoint,uint32_t sequence,
                   uint32_t source,const std::vector<unsigned char>& wire,std::vector<unsigned char>& frame,uint32_t offset=0) {
    frame.clear();if(!Valid(checkpoint) || !sequence || !Inspect(wire,source,checkpoint.bytes)
        || offset>checkpoint.bytes || wire.size()-NativeHeader>checkpoint.bytes-offset)return false;
    frame.resize(Header);memcpy(frame.data(),Magic,8);
    memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    PutCheckpoint(frame.data()+40,checkpoint);CrossplayChat::Put(frame.data()+188,sequence);
    CrossplayChat::Put(frame.data()+192,static_cast<uint32_t>(wire.size()));
    CrossplayChat::Put(frame.data()+ContextHeader,offset);
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,const Checkpoint& checkpoint,uint32_t sequence,
                   uint32_t source,uint32_t local_source,const std::vector<unsigned char>& frame,
                   std::vector<unsigned char>& wire,uint32_t offset=0) {
    wire.clear();unsigned char expected[148]{};
    if(!Valid(checkpoint) || !sequence || local_source==UINT32_MAX || offset>checkpoint.bytes
        || frame.size()<Header+NativeHeader || frame.size()>Header+NativeHeader+MaximumFragmentBytes)return false;
    PutCheckpoint(expected,checkpoint);
    if(memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || memcmp(frame.data()+40,expected,sizeof(expected))
        || CrossplayChat::Word(frame.data()+188)!=sequence
        || CrossplayChat::Word(frame.data()+192)!=frame.size()-Header
        || CrossplayChat::Word(frame.data()+ContextHeader)!=offset)return false;
    std::vector<unsigned char> decoded(frame.begin()+Header,frame.end());
    // Keep our byte position separate from the native sender CID. The original
    // sender's stack pushes change ESP; its +8 argument is the CID local.
    if(!Inspect(decoded,source,checkpoint.bytes) || decoded.size()-NativeHeader>checkpoint.bytes-offset)return false;
    CrossplayChat::Put(decoded.data()+8,local_source);
    if(!Inspect(decoded,local_source,checkpoint.bytes))return false;
    wire=std::move(decoded);return true;
}
class Receiver {
    Checkpoint checkpoint_{};
    uint32_t sequence_=0,received_=0,checksum_=0;
    bool bound_=false,failed_=false,checksum_seen_=false,zero_end_=false;
public:
    bool Bind(const Checkpoint& checkpoint) {
        if(bound_ || failed_ || !Valid(checkpoint))return false;
        checkpoint_=checkpoint;bound_=true;return true;
    }
    bool Accept(const Crossplay::Peer& peer,uint32_t source,uint32_t local_source,
                const std::vector<unsigned char>& frame,std::vector<unsigned char>& wire) {
        wire.clear();if(!bound_ || failed_)return false;
        std::vector<unsigned char> decoded;
        if(sequence_==UINT32_MAX || !Decode(peer,checkpoint_,sequence_+1,source,local_source,frame,decoded,received_)) {
            failed_=true;return false;
        }
        const auto size=CrossplayChat::Word(decoded.data()+13);
        const auto checksum=CrossplayChat::Word(decoded.data()+21);
        if(zero_end_ || size>checkpoint_.bytes-received_
            || (!size && received_!=checkpoint_.bytes)
            || (checksum_seen_ && checksum!=checksum_)) {
            failed_=true;return false;
        }
        checksum_=checksum;checksum_seen_=true;zero_end_=!size;
        ++sequence_;received_+=size;wire=std::move(decoded);return true;
    }
    uint32_t ReceivedBytes()const{return received_;}
    bool Failed()const{return failed_;}
};
}
