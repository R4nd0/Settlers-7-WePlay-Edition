#include "crossplay_saved_ready.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL saved Ready line %u\n",__LINE__);return 1;}}while(0)
    namespace S=CrossplaySavedReady;
    Crossplay::Peer host{},guest{};host.local_id[0]=1;host.remote_id[0]=2;guest.local_id=host.remote_id;guest.remote_id=host.local_id;
    CrossplayResumeTransfer::Checkpoint cp{};cp.content[0]=1;cp.setup[0]=2;cp.file[0]=3;cp.neo[0]=4;
    cp.logic_turn=700;cp.replay_turn=701;cp.bytes=8193;cp.enabled=true;cp.features=19;
    std::vector<unsigned char> wire(CrossplayReady::WireSize),frame,decoded;
    CrossplayChat::Put(wire.data(),CrossplayReady::Class);CrossplayChat::Put(wire.data()+4,CrossplayReady::Type);
    CrossplayChat::Put(wire.data()+8,82);wire[12]=1;
    REQUIRE(S::EncodeRequest(guest,cp,1,82,wire,frame));
    REQUIRE(S::DecodeRequest(host,cp,0,82,42,frame,decoded));
    auto expected=wire;CrossplayChat::Put(expected.data()+8,42);REQUIRE(decoded==expected);
    REQUIRE(!S::DecodeRequest(host,cp,1,82,42,frame,decoded));
    REQUIRE(!S::DecodeRequest(host,cp,0,81,42,frame,decoded));
    REQUIRE(!S::DecodeRequest(guest,cp,0,82,42,frame,decoded));
    REQUIRE(!S::DecodeRequest(host,cp,0,82,UINT32_MAX,frame,decoded));
    auto changed=cp;changed.file[1]=9;REQUIRE(!S::DecodeRequest(host,changed,0,82,42,frame,decoded));
    for(size_t i=0;i<frame.size();++i){auto bad=frame;bad[i]^=0x80;REQUIRE(!S::DecodeRequest(host,cp,0,82,42,bad,decoded));REQUIRE(decoded.empty());}
    for(size_t size=0;size<frame.size();++size){auto bad=frame;bad.resize(size);REQUIRE(!S::DecodeRequest(host,cp,0,82,42,bad,decoded));}
    auto long_frame=frame;long_frame.push_back(0);REQUIRE(!S::DecodeRequest(host,cp,0,82,42,long_frame,decoded));
    for(const auto revision:{0u,UINT32_MAX})REQUIRE(!S::EncodeRequest(guest,cp,revision,82,wire,decoded));
    CrossplayReadyState::Snapshot state{1,1,1},observed{};
    REQUIRE(S::EncodeObserved(host,cp,state,frame));REQUIRE(S::DecodeObserved(guest,cp,0,1,frame,observed));
    REQUIRE(observed.revision==1 && observed.host==1 && observed.guest==1);
    REQUIRE(!S::DecodeObserved(guest,cp,1,1,frame,observed));
    REQUIRE(!S::DecodeObserved(guest,cp,0,0,frame,observed));
    REQUIRE(!S::DecodeObserved(host,cp,0,1,frame,observed));
    REQUIRE(!S::DecodeObserved(guest,changed,0,1,frame,observed));
    for(size_t i=0;i<frame.size();++i){auto bad=frame;bad[i]^=0x80;REQUIRE(!S::DecodeObserved(guest,cp,0,1,bad,observed));REQUIRE(!observed.revision);}
    for(size_t size=0;size<frame.size();++size){auto bad=frame;bad.resize(size);REQUIRE(!S::DecodeObserved(guest,cp,0,1,bad,observed));}
    // Original false/true states and successive toggles remain representable;
    // a Prepared receipt or an unrequested/replayed revision cannot stand in.
    state={2,1,0};REQUIRE(S::EncodeObserved(host,cp,state,frame));REQUIRE(S::DecodeObserved(guest,cp,1,2,frame,observed));
    REQUIRE(!observed.guest && observed.host==1);
    REQUIRE(!S::DecodeObserved(guest,cp,0,2,frame,observed));
    for(unsigned char a=0;a<3;++a)for(unsigned char b=0;b<3;++b){
        state={3,a,b};REQUIRE(S::EncodeObserved(host,cp,state,frame)==(a<=1 && b<=1));
    }
    using Kind=S::Kind;
    REQUIRE(S::EncodeRoomReady(host,cp,Kind::RoomObserved,14,frame));
    REQUIRE(S::DecodeRoomReady(guest,cp,Kind::RoomObserved,14,frame));
    REQUIRE(!S::DecodeObserved(guest,cp,0,1,frame,observed));
    REQUIRE(!S::DecodeRoomReady(host,cp,Kind::RoomObserved,14,frame));
    REQUIRE(!S::DecodeRoomReady(guest,changed,Kind::RoomObserved,14,frame));
    REQUIRE(!S::DecodeRoomReady(guest,cp,Kind::RoomApplied,14,frame));
    REQUIRE(!S::DecodeRoomReady(guest,cp,Kind::RoomObserved,6,frame));
    REQUIRE(S::EncodeRoomReady(guest,cp,Kind::RoomApplied,14,frame));
    REQUIRE(S::DecodeRoomReady(host,cp,Kind::RoomApplied,14,frame));
    for(unsigned invalid:{0u,2u,15u,512u})
        REQUIRE(!S::EncodeRoomReady(host,cp,Kind::RoomObserved,invalid,frame));
    printf("PASS %u checkpoint-bound saved Ready checks: exact original request/CID projection, observed state revisions, namespace/source/hash/replay/bounds; synthetic only, no native Ready supplied.\n",checks);
}
