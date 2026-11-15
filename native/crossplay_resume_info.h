#pragma once
#include "crossplay_resume_transfer.h"
#include "crossplay_session_config.h"

// The original subtype 0x25 reply contains one serialized native string and
// one opaque scalar. Neither field supplies file admission or native Ready.
namespace CrossplayResumeInfo {
constexpr uint32_t Class=0xa3872159u,Type=7u|(0x25u<<16);
constexpr size_t Header=CrossplayResumeTransfer::ContextHeader;
constexpr size_t MaximumWire=13+8+CrossplaySessionConfig::MaximumPath+1+4;
constexpr unsigned char Magic[8]={'S','7','R','I','N','F',0,1};
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t source) {
    if(source==UINT32_MAX || wire.size()<25 || wire.size()>MaximumWire
        || CrossplayChat::Word(wire.data())!=Class || CrossplayChat::Word(wire.data()+4)!=Type
        || CrossplayChat::Word(wire.data()+8)!=source || wire[12]!=1)return false;
    size_t at=13;std::string text;
    return CrossplaySessionConfig::String(wire,at,text) && wire.size()-at==4;
}
inline bool Encode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                   uint32_t source,const std::vector<unsigned char>& wire,std::vector<unsigned char>& frame) {
    frame.clear();if(!CrossplayResumeTransfer::Valid(checkpoint) || !Inspect(wire,source))return false;
    frame.resize(Header);memcpy(frame.data(),Magic,8);
    memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayResumeTransfer::PutCheckpoint(frame.data()+40,checkpoint);
    CrossplayChat::Put(frame.data()+188,1);CrossplayChat::Put(frame.data()+192,static_cast<uint32_t>(wire.size()));
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                   uint32_t source,uint32_t local_source,const std::vector<unsigned char>& frame,
                   std::vector<unsigned char>& wire) {
    wire.clear();unsigned char expected[148]{};
    if(!CrossplayResumeTransfer::Valid(checkpoint) || local_source==UINT32_MAX
        || frame.size()<Header+25 || frame.size()>Header+MaximumWire)return false;
    CrossplayResumeTransfer::PutCheckpoint(expected,checkpoint);
    if(memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || memcmp(frame.data()+40,expected,sizeof(expected))
        || CrossplayChat::Word(frame.data()+188)!=1 || CrossplayChat::Word(frame.data()+192)!=frame.size()-Header)return false;
    std::vector<unsigned char> decoded(frame.begin()+Header,frame.end());
    if(!Inspect(decoded,source))return false;
    CrossplayChat::Put(decoded.data()+8,local_source);
    if(!Inspect(decoded,local_source))return false;
    wire=std::move(decoded);return true;
}
}
