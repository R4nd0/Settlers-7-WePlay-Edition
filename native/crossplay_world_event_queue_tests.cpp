#define S7_TEST_WORLD_QUEUE
#include "crossplay_world_event_queue.h"
#include <cstdio>
#include <cstdlib>
namespace {
using namespace CrossplayWorldEvents;
unsigned checks=0;
void Check(bool okay){++checks;if(!okay){printf("FAIL ordered world delivery check %u\n",checks);exit(1);}}
Crossplay::Peer sender{},receiver{};
std::vector<unsigned char> Frame(Kind kind,uint32_t sequence,uint32_t player=1){
    std::vector<unsigned char> wire,frame;
    auto put=[&](size_t offset,uint32_t value){CrossplayChat::Put(wire.data()+offset,value);};
    if(kind==Kind::Turn){
        wire.resize(CrossplayTurnBatch::BaseWire+CrossplayTurnBatch::ItemWire);
        put(0,CrossplayTurnBatch::Class);put(4,CrossplayTurnBatch::Type);put(8,42);put(13,player);put(17,1);
        put(21,CrossplayTurnBatch::NestedType);put(25,42);
        for(unsigned i=0;i<5;++i)put(30+i*4,0x80000000u+sequence*5+i);
        Check(CrossplayTurnBatch::Encode(sender,sequence,wire,42,frame));
    }else if(kind==Kind::Progress || kind==Kind::Desync){
        wire.resize(17);put(8,42);put(13,kind==Kind::Progress?0x42c80000u:UINT32_MAX);
        if(kind==Kind::Progress){put(0,CrossplayLoadProgress::Class);put(4,CrossplayLoadProgress::Type);
            Check(CrossplayLoadProgress::Encode(sender,sequence,wire,42,frame));}
        else{put(0,CrossplayDesyncTurn::Class);put(4,CrossplayDesyncTurn::Type);
            Check(CrossplayDesyncTurn::Encode(sender,sequence,wire,42,frame));}
    }else{
        wire.resize(kind==Kind::Priority?35:47);put(0,CrossplayBuildCommand::Class);put(4,CrossplayBuildCommand::Type);put(8,42);wire[21]=static_cast<unsigned char>(player);
        if(kind==Kind::Build){
            put(13,CrossplayBuildCommand::NestedClass);put(17,CrossplayBuildCommand::PlacementType);
            put(22,0x472fc000);put(26,0x47435000);put(30,0x80000000);put(34,260);put(38,14);put(42,player);
            Check(CrossplayBuildCommand::Encode(sender,sequence,wire,42,player,frame));
        }else if(kind==Kind::Road){
            put(13,CrossplayRoadCommand::NestedClass);put(17,CrossplayRoadCommand::PlacementType);put(22,1);
            for(unsigned at:{26u,30u,34u,38u})put(at,0x47200000u+at);put(42,player);
            Check(CrossplayRoadCommand::Encode(sender,sequence,wire,42,player,frame));
        }else{
            put(13,CrossplayPriorityCommand::NestedClass);put(17,CrossplayPriorityCommand::PriorityType);put(22,65536);put(26,65537);put(30,player);
            Check(CrossplayPriorityCommand::Encode(sender,sequence,wire,42,player,frame));
        }
    }
    return frame;
}
}
int main(){
    sender.local_id[0]=1;sender.remote_id[0]=2;receiver.local_id=sender.remote_id;receiver.remote_id=sender.local_id;
    for(uint32_t player:{1u,2u}){
        Inbox inbox;std::array<uint32_t,7> sequences{};
        const Kind order[]={Kind::Progress,Kind::Turn,Kind::Build,Kind::Road,Kind::Turn,Kind::Priority,Kind::Build,Kind::Turn,Kind::Desync};
        std::vector<std::vector<unsigned char>> originals;
        for(auto kind:order){auto frame=Frame(kind,++sequences[static_cast<unsigned>(kind)],player);originals.push_back(frame);Check(inbox.Push(frame));}
        size_t delivered=0;
        auto dispatch=[&](Kind kind,const std::vector<unsigned char>& wire){
            Check(kind==order[delivered]);auto expected=originals[delivered];expected.erase(expected.begin(),expected.begin()+48);
            CrossplayChat::Put(expected.data()+8,81);
            if(kind==Kind::Turn)CrossplayChat::Put(expected.data()+CrossplayTurnBatch::BaseWire+4,81);
            Check(wire==expected);++delivered;return true;
        };
        // Native rejection cannot acknowledge the head or let its EOT overtake it.
        Check(!inbox.Deliver(receiver,42,81,player,[](Kind,const auto&){return false;}));
        Check(inbox.Size()==9 && inbox.Received(Kind::Progress)==0);
        for(unsigned budget=0;budget<3;++budget)Check(inbox.Deliver(receiver,42,81,player,dispatch));
        Check(delivered==3 && inbox.Received(Kind::Turn)==1 && inbox.Received(Kind::Build)==1);
        while(!inbox.Empty())Check(inbox.Deliver(receiver,42,81,player,dispatch));
        Check(delivered==9 && inbox.Received(Kind::Turn)==3 && inbox.Received(Kind::Build)==2);
        Check(!inbox.Deliver(receiver,42,81,player,dispatch));
        // Replay blocks later valid EOTs; no native dispatch is attempted.
        Check(inbox.Push(originals[2]));Check(inbox.Push(Frame(Kind::Turn,4,player)));
        Check(!inbox.Deliver(receiver,42,81,player,[](Kind,const auto&){Check(false);return true;}));Check(inbox.Size()==2);
    }
    for(auto kind:{Kind::Progress,Kind::Turn,Kind::Desync,Kind::Build,Kind::Road,Kind::Priority}){
        const auto frame=Frame(kind,1);
        for(size_t length=0;length<frame.size();++length){
            Inbox inbox;auto truncated=frame;truncated.resize(length);
            // Turn sizes also require their original count/schema decoder.
            if(inbox.Push(truncated))Check(!inbox.Deliver(receiver,42,81,1,[](Kind,const auto&){Check(false);return true;}));
        }
        Inbox overflow;for(size_t i=0;i<Inbox::Maximum;++i)Check(overflow.Push(frame));Check(!overflow.Push(frame));
        for(size_t offset:{8u,24u,40u,44u}){
            Inbox inbox;auto forged=frame;forged[offset]^=1;Check(inbox.Push(forged));
            Check(!inbox.Deliver(receiver,42,81,1,[](Kind,const auto&){Check(false);return true;}));Check(inbox.Received(kind)==0);
        }
    }
    for(auto kind:{Kind::Build,Kind::Road,Kind::Priority}){
        Inbox inbox;Check(inbox.Push(Frame(kind,1,2)));Check(inbox.Push(Frame(Kind::Turn,1)));
        Check(!inbox.Deliver(receiver,42,81,1,[](Kind,const auto&){Check(false);return true;}));Check(inbox.Size()==2);
    }
    Inbox unknown;Check(!unknown.Push(std::vector<unsigned char>(95,0)));
    {
        Inbox room;
        const auto first=Frame(Kind::Build,1,1);
        sender.local_id[0]=3;
        const auto guest_turn=Frame(Kind::Turn,1,3);
        const auto guest_next=Frame(Kind::Turn,2,3);
        sender.local_id[0]=1;
        const auto host_turn=Frame(Kind::Turn,1,1);
        Check(!room.PushRoom({0,first}));
        Check(!room.PushRoom({9,first}));
        Check(room.PushRoom({1,first}));
        Check(!room.Push(host_turn));
        Check(!room.Deliver(receiver,42,81,1,[](Kind,const auto&){return true;}));
        Check(room.PushRoom({3,guest_turn}));
        Check(room.PushRoom({1,host_turn}));
        Check(room.PushRoom({3,guest_next}));

        auto resolve=[&](unsigned char slot,CrossplayNative::RoomRoute& route){
            if(slot!=1 && slot!=3)return false;
            route.player_slot=slot;
            route.peer=receiver;
            route.peer.remote_id[0]=slot;
            route.source_cid=42; // two original processes can use the same outgoing CID
            route.receiver_cid=80u+slot;
            return true;
        };
        const unsigned char slots[]={1,3,1,3};
        const Kind kinds[]={Kind::Build,Kind::Turn,Kind::Turn,Kind::Turn};
        size_t delivered=0;
        auto dispatch=[&](unsigned char slot,Kind kind,const std::vector<unsigned char>& wire){
            Check(delivered<4 && slot==slots[delivered] && kind==kinds[delivered]);
            Check(CrossplayChat::Word(wire.data()+8)==80u+slot);
            if(kind==Kind::Turn)Check(CrossplayChat::Word(wire.data()+25)==80u+slot);
            ++delivered;return true;
        };
        Check(!room.TestDeliverRoom(resolve,[](unsigned char,Kind,const auto&){return false;}));
        Check(room.Size()==4 && !room.Received(Kind::Build,1));
        while(!room.Empty())Check(room.TestDeliverRoom(resolve,dispatch));
        Check(delivered==4 && room.Received(Kind::Turn,1)==1 && room.Received(Kind::Turn,3)==2);
        Check(room.PushRoom({1,guest_next})); // a valid guest frame must not execute under the host's CID
        Check(!room.TestDeliverRoom(resolve,dispatch));
        Check(room.Size()==1 && room.Received(Kind::Turn,1)==1);
    }
    printf("PASS %u ordered world-event checks: commands/EOT order, exact opaque scalars, both players, rejected native delivery, replay/identity/schema and bounded backlog. Synthetic only.\n",checks);return 0;
}
