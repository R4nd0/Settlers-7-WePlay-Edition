#pragma once
#include "crossplay_load_progress.h"
#include "crossplay_turn_batch.h"
#include "crossplay_desync_turn.h"
#include "crossplay_build_command.h"
#include "crossplay_road_command.h"
#include "crossplay_priority_command.h"
#include <array>
#include <deque>

// The authenticated channel supplies ordered frames. Keep one bounded inbox
// across native command classes: a later EOT must never overtake a build/road.
// Native ownership/readiness remains the dispatcher's independent obligation.
namespace CrossplayWorldEvents {
enum class Kind : unsigned {Unknown,Progress,Turn,Desync,Build,Road,Priority};
inline Kind Identify(const std::vector<unsigned char>& frame){
    if(frame.size()<8)return Kind::Unknown;
    const unsigned char* magic[]={CrossplayLoadProgress::Magic,CrossplayTurnBatch::Magic,
        CrossplayDesyncTurn::Magic,CrossplayBuildCommand::Magic,CrossplayRoadCommand::Magic,CrossplayPriorityCommand::Magic};
    for(unsigned i=0;i<6;++i)if(!memcmp(frame.data(),magic[i],8))return static_cast<Kind>(i+1);
    return Kind::Unknown;
}
inline bool Bounded(Kind kind,size_t size){
    switch(kind){
    case Kind::Progress:return size==CrossplayLoadProgress::FrameSize;
    case Kind::Turn:return size>=CrossplayTurnBatch::Header+CrossplayTurnBatch::BaseWire
        && size<=CrossplayTurnBatch::Header+CrossplayTurnBatch::MaxWire;
    case Kind::Desync:return size==CrossplayDesyncTurn::FrameSize;
    case Kind::Build:return size==CrossplayBuildCommand::FrameSize;
    case Kind::Road:return size==CrossplayRoadCommand::FrameSize;
    case Kind::Priority:return size==CrossplayPriorityCommand::FrameSize;
    default:return false;
    }
}
class Inbox {
    struct Entry {Kind kind;std::vector<unsigned char> frame;};
    std::deque<Entry> queue_;
    std::array<uint32_t,7> received_{};
public:
    static constexpr size_t Maximum=256;
    bool Empty()const{return queue_.empty();}
    size_t Size()const{return queue_.size();}
    uint32_t Received(Kind kind)const{
        const auto i=static_cast<unsigned>(kind);return i<received_.size()?received_[i]:0;
    }
    bool Push(const std::vector<unsigned char>& frame){
        const auto kind=Identify(frame);
        if(queue_.size()>=Maximum || !Bounded(kind,frame.size()))return false;
        queue_.push_back({kind,frame});return true;
    }
    template<class Dispatch>
    bool Deliver(const Crossplay::Peer& peer,uint32_t source,uint32_t local_source,uint32_t player,Dispatch dispatch){
        if(queue_.empty())return false;
        const auto& entry=queue_.front();const auto previous=Received(entry.kind);
        std::vector<unsigned char> wire;bool decoded=false;
        switch(entry.kind){
        case Kind::Progress:decoded=CrossplayLoadProgress::Decode(peer,previous,entry.frame,source,local_source,wire);break;
        case Kind::Turn:decoded=CrossplayTurnBatch::Decode(peer,previous,entry.frame,source,local_source,wire);break;
        case Kind::Desync:decoded=CrossplayDesyncTurn::Decode(peer,previous,entry.frame,source,local_source,wire);break;
        case Kind::Build:decoded=CrossplayBuildCommand::Decode(peer,previous,entry.frame,source,local_source,player,wire);break;
        case Kind::Road:decoded=CrossplayRoadCommand::Decode(peer,previous,entry.frame,source,local_source,player,wire);break;
        case Kind::Priority:decoded=CrossplayPriorityCommand::Decode(peer,previous,entry.frame,source,local_source,player,wire);break;
        default:return false;
        }
        if(!decoded || !dispatch(entry.kind,wire))return false;
        ++received_[static_cast<unsigned>(entry.kind)];queue_.pop_front();return true;
    }
};
}
