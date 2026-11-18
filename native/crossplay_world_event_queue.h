#pragma once
#include "crossplay_load_progress.h"
#include "crossplay_turn_batch.h"
#include "crossplay_desync_turn.h"
#include "crossplay_build_command.h"
#include "crossplay_road_command.h"
#include "crossplay_priority_command.h"
#include "crossplay_admission.h"
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
    struct Entry {Kind kind;unsigned char source_slot;std::vector<unsigned char> frame;};
    std::deque<Entry> queue_;
    // Stream zero is the legacy pair. Group counters stay separate per
    // authenticated sender, while all frames share one ordered queue.
    std::array<std::array<uint32_t,7>,Crossplay::RoomSnapshot::MaximumMembers+1> received_{};
    bool PushFrame(unsigned char source_slot,const std::vector<unsigned char>& frame) {
        const auto kind=Identify(frame);
        if(queue_.size()>=Maximum || !Bounded(kind,frame.size())
            || (!queue_.empty() && (queue_.front().source_slot==0)!=(source_slot==0)))return false;
        queue_.push_back({kind,source_slot,frame});return true;
    }
    template<class Dispatch>
    bool DeliverFront(const Crossplay::Peer& peer,uint32_t source,uint32_t local_source,uint32_t player,Dispatch dispatch){
        const auto& entry=queue_.front();
        auto& counter=received_[entry.source_slot][static_cast<unsigned>(entry.kind)];
        const auto previous=counter;
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
        ++counter;queue_.pop_front();return true;
    }
    template<class Resolve,class Dispatch>
    bool DeliverRoomBound(Resolve resolve,Dispatch dispatch){
        if(queue_.empty() || !queue_.front().source_slot)return false;
        const auto source_slot=queue_.front().source_slot;
        CrossplayNative::RoomRoute route;
        if(!resolve(source_slot,route) || route.local)return false;
        return DeliverFront(route.peer,route.source_cid,route.receiver_cid,route.player_slot,
            [&](Kind kind,const auto& wire){return dispatch(source_slot,kind,wire);});
    }
public:
    static constexpr size_t Maximum=256;
    bool Empty()const{return queue_.empty();}
    size_t Size()const{return queue_.size();}
    uint32_t Received(Kind kind,unsigned char source_slot=0)const{
        const auto i=static_cast<unsigned>(kind);
        return source_slot<received_.size() && i<received_[source_slot].size()?received_[source_slot][i]:0;
    }
    bool Push(const std::vector<unsigned char>& frame){return PushFrame(0,frame);}
    bool PushRoom(const Crossplay::RoomEvent& event){
        return event.source_slot && event.source_slot<=Crossplay::RoomSnapshot::MaximumMembers
            && PushFrame(event.source_slot,event.bytes);
    }
    template<class Dispatch>
    bool Deliver(const Crossplay::Peer& peer,uint32_t source,uint32_t local_source,uint32_t player,Dispatch dispatch){
        return !queue_.empty() && !queue_.front().source_slot
            && DeliverFront(peer,source,local_source,player,dispatch);
    }
    template<class Dispatch>
    bool DeliverRoom(const CrossplayNative::RoomLobbyAdmission& admission,Dispatch dispatch){
        return DeliverRoomBound([&](unsigned char slot,CrossplayNative::RoomRoute& route){
            return admission.Resolve(slot,true,route);
        },dispatch);
    }
#ifdef S7_TEST_WORLD_QUEUE
    template<class Resolve,class Dispatch>
    bool TestDeliverRoom(Resolve resolve,Dispatch dispatch){return DeliverRoomBound(resolve,dispatch);}
#endif
};
}
