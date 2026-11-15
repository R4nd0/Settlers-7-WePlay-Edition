#pragma once
#include "crossplay_resume_transfer.h"

// Original CNetworkEvent, subtype 0x23: a guest asks its host for the selected
// native file. This request is not an acknowledgement or a Ready notification.
namespace CrossplayResumeRequest {
constexpr uint32_t Class=0x433d4ea9u,Type=7u|(0x23u<<16);
constexpr size_t NativeBytes=13,Header=CrossplayResumeTransfer::ContextHeader,FrameBytes=Header+NativeBytes;
constexpr unsigned char Magic[8]={'S','7','R','R','E','Q',0,1};
inline bool Inspect(const std::vector<unsigned char>& wire,uint32_t guest) {
    return guest!=UINT32_MAX && wire.size()==NativeBytes && CrossplayChat::Word(wire.data())==Class
        && CrossplayChat::Word(wire.data()+4)==Type && CrossplayChat::Word(wire.data()+8)==guest && wire[12]==1;
}
inline bool Encode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,uint32_t guest,
                   const std::vector<unsigned char>& wire,std::vector<unsigned char>& frame) {
    frame.clear();if(!CrossplayResumeTransfer::Valid(checkpoint) || !Inspect(wire,guest))return false;
    frame.resize(Header);memcpy(frame.data(),Magic,8);
    memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayResumeTransfer::PutCheckpoint(frame.data()+40,checkpoint);
    CrossplayChat::Put(frame.data()+188,1);CrossplayChat::Put(frame.data()+192,NativeBytes);
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                   uint32_t source_guest,uint32_t local_guest,const std::vector<unsigned char>& frame,
                   std::vector<unsigned char>& wire) {
    wire.clear();unsigned char expected[148]{};
    if(!CrossplayResumeTransfer::Valid(checkpoint) || local_guest==UINT32_MAX || frame.size()!=FrameBytes)return false;
    CrossplayResumeTransfer::PutCheckpoint(expected,checkpoint);
    if(memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || memcmp(frame.data()+40,expected,sizeof(expected))
        || CrossplayChat::Word(frame.data()+188)!=1 || CrossplayChat::Word(frame.data()+192)!=NativeBytes)return false;
    std::vector<unsigned char> decoded(frame.begin()+Header,frame.end());
    if(!Inspect(decoded,source_guest))return false;
    CrossplayChat::Put(decoded.data()+8,local_guest);
    if(!Inspect(decoded,local_guest))return false;
    wire=std::move(decoded);return true;
}
}
