#pragma once
#include "crossplay_resume_transfer.h"

// Transport flow control only. This frame is never passed to the game and
// cannot stand in for its native file acknowledgement, Ready, or start event.
namespace CrossplayResumeCredit {
constexpr size_t FrameBytes=CrossplayResumeTransfer::ContextHeader;
constexpr unsigned char Magic[8]={'S','7','R','C','R','D',0,1};
inline bool Encode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                   uint32_t sequence,uint32_t bytes,std::vector<unsigned char>& frame) {
    frame.clear();if(!CrossplayResumeTransfer::Valid(checkpoint) || !sequence || bytes>checkpoint.bytes)return false;
    frame.resize(FrameBytes);memcpy(frame.data(),Magic,8);
    memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayResumeTransfer::PutCheckpoint(frame.data()+40,checkpoint);
    CrossplayChat::Put(frame.data()+188,sequence);CrossplayChat::Put(frame.data()+192,bytes);return true;
}
inline bool Decode(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& checkpoint,
                   const std::vector<unsigned char>& frame,uint32_t& sequence,uint32_t& bytes) {
    sequence=bytes=0;unsigned char expected[148]{};
    if(!CrossplayResumeTransfer::Valid(checkpoint) || frame.size()!=FrameBytes)return false;
    CrossplayResumeTransfer::PutCheckpoint(expected,checkpoint);
    if(memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || memcmp(frame.data()+40,expected,sizeof(expected)))return false;
    const auto next=CrossplayChat::Word(frame.data()+188),amount=CrossplayChat::Word(frame.data()+192);
    if(!next || amount>checkpoint.bytes)return false;
    sequence=next;bytes=amount;return true;
}
}
