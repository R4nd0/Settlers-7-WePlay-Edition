#pragma once
#include "crossplay_resume_transfer.h"
#include "crossplay_ready.h"
#include "crossplay_ready_state.h"

// Saved-file Ready is separate from fresh setup and from adapter Prepared.
// Request bytes must originate in the guest's actual native Ready action;
// observed state is sent only after its original Gold consumer completes.
namespace CrossplaySavedReady {
inline constexpr unsigned char Magic[8]={'S','7','R','S','R','D','Y',1};
constexpr size_t Header=CrossplayResumeTransfer::ContextHeader;
enum class Kind:uint32_t {Request=1,Observed=2,RoomObserved=3,RoomApplied=4};
inline bool Prefix(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& cp,
                   uint32_t revision,Kind kind,std::vector<unsigned char>& frame){
    frame.clear();if(!CrossplayResumeTransfer::Valid(cp) || !revision || revision==UINT32_MAX)return false;
    frame.resize(Header);memcpy(frame.data(),Magic,8);memcpy(frame.data()+8,peer.local_id.data(),16);memcpy(frame.data()+24,peer.remote_id.data(),16);
    CrossplayResumeTransfer::PutCheckpoint(frame.data()+40,cp);CrossplayChat::Put(frame.data()+188,revision);
    CrossplayChat::Put(frame.data()+192,static_cast<uint32_t>(kind));return true;
}
inline bool Context(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& cp,
                    uint32_t revision,Kind kind,const std::vector<unsigned char>& frame,size_t payload){
    if(!CrossplayResumeTransfer::Valid(cp) || !revision || revision==UINT32_MAX || frame.size()!=Header+payload)return false;
    unsigned char expected[148]{};CrossplayResumeTransfer::PutCheckpoint(expected,cp);
    return !memcmp(frame.data(),Magic,8) && !memcmp(frame.data()+8,peer.remote_id.data(),16)
        && !memcmp(frame.data()+24,peer.local_id.data(),16) && !memcmp(frame.data()+40,expected,sizeof(expected))
        && CrossplayChat::Word(frame.data()+188)==revision && CrossplayChat::Word(frame.data()+192)==static_cast<uint32_t>(kind);
}
inline bool EncodeRequest(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& cp,
                          uint32_t revision,uint32_t guest,const std::vector<unsigned char>& wire,std::vector<unsigned char>& frame){
    frame.clear();if(!CrossplayReady::Validate(wire,guest) || !Prefix(peer,cp,revision,Kind::Request,frame))return false;
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool DecodeRequest(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& cp,
                          uint32_t previous,uint32_t source,uint32_t guest,const std::vector<unsigned char>& frame,std::vector<unsigned char>& wire){
    wire.clear();if(previous>=UINT32_MAX-1 || guest==UINT32_MAX
        || !Context(peer,cp,previous+1,Kind::Request,frame,CrossplayReady::WireSize))return false;
    std::vector<unsigned char> value(frame.begin()+Header,frame.end());
    if(!CrossplayReady::Validate(value,source))return false;CrossplayChat::Put(value.data()+8,guest);
    if(!CrossplayReady::Validate(value,guest))return false;wire=std::move(value);return true;
}
inline bool EncodeObserved(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& cp,
                           const CrossplayReadyState::Snapshot& state,std::vector<unsigned char>& frame){
    frame.clear();if(!CrossplayReadyState::Valid(state) || !Prefix(peer,cp,state.revision,Kind::Observed,frame))return false;
    frame.push_back(state.host);frame.push_back(state.guest);return true;
}
inline bool DecodeObserved(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& cp,
                           uint32_t previous,uint32_t requested,const std::vector<unsigned char>& frame,CrossplayReadyState::Snapshot& state){
    state={};if(previous>=UINT32_MAX-1 || previous>=requested || !Context(peer,cp,previous+1,Kind::Observed,frame,2))return false;
    CrossplayReadyState::Snapshot value{previous+1,frame[Header],frame[Header+1]};
    if(!CrossplayReadyState::Valid(value))return false;state=value;return true;
}
inline bool RoomSeatMask(unsigned seats){return !(seats&~0x1feu) && (seats&(seats-1));}
inline bool EncodeRoomReady(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& cp,
                            Kind kind,unsigned seats,std::vector<unsigned char>& frame){
    frame.clear();
    if((kind!=Kind::RoomObserved && kind!=Kind::RoomApplied) || !RoomSeatMask(seats)
        || !Prefix(peer,cp,1,kind,frame))return false;
    frame.resize(Header+4);
    CrossplayChat::Put(frame.data()+Header,seats);return true;
}
inline bool DecodeRoomReady(const Crossplay::Peer& peer,const CrossplayResumeTransfer::Checkpoint& cp,Kind kind,
                            unsigned seats,const std::vector<unsigned char>& frame){
    return (kind==Kind::RoomObserved || kind==Kind::RoomApplied) && RoomSeatMask(seats)
        && Context(peer,cp,1,kind,frame,4) && CrossplayChat::Word(frame.data()+Header)==seats;
}
}
