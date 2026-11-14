#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_desync_turn.h"
#include <stdio.h>
#include <stdlib.h>

namespace {
void Check(bool okay,const char* message){if(!okay){fprintf(stderr,"FAIL %s\n",message);exit(1);}}
std::vector<unsigned char> Wire(uint32_t turn,unsigned char asynchronous) {
    std::vector<unsigned char> value(CrossplayDesyncTurn::WireSize,0);
    CrossplayChat::Put(value.data(),CrossplayDesyncTurn::Class);CrossplayChat::Put(value.data()+4,CrossplayDesyncTurn::Type);
    CrossplayChat::Put(value.data()+8,42);value[12]=asynchronous;CrossplayChat::Put(value.data()+13,turn);return value;
}
}
int main() {
    using namespace CrossplayDesyncTurn;
    Crossplay::Peer host{},guest{};host.local_id[0]=1;host.remote_id[0]=2;guest.local_id=host.remote_id;guest.remote_id=host.local_id;
    for(auto turn:{0u,1u,17u,0x80000000u,UINT32_MAX})for(auto async:{0u,1u}) {
        const auto value=Wire(turn,static_cast<unsigned char>(async));std::vector<unsigned char> frame,decoded;
        Check(Encode(host,1,value,42,frame) && Decode(guest,0,frame,42,0,decoded),"original detection turn and native sentinel round trip");
        auto expected=value;CrossplayChat::Put(expected.data()+8,0);Check(decoded==expected,"only admitted header source projected; native turn unchanged");
        Check(!Decode(guest,1,frame,42,0,decoded) && decoded.empty(),"native control event replay excluded");
        for(size_t i=0;i<Header;++i){auto bad=frame;bad[i]^=1;Check(!Decode(guest,0,bad,42,0,decoded),"every frame header byte bound");}
        auto truncated=frame;truncated.pop_back();Check(!Decode(guest,0,truncated,42,0,decoded),"truncated native scalar excluded");
        auto trailing=frame;trailing.push_back(0);Check(!Decode(guest,0,trailing,42,0,decoded),"trailing native scalar bytes excluded");
    }
    std::vector<unsigned char> frame,decoded;const auto value=Wire(1,1);
    Check(!Encode(host,0,value,42,frame) && !Encode(host,MaxSequence+1,value,42,frame),"bounded nonzero sequence");
    Check(!Encode(host,1,Wire(0,2),42,frame),"invalid native async flag excluded");
    for(unsigned offset:{0u,4u,8u}){auto bad=value;bad[offset]^=1;Check(!Encode(host,1,bad,42,frame),"class, full native type and source guarded");}
    Check(Encode(host,MaxSequence,value,42,frame) && Decode(guest,MaxSequence-1,frame,42,0,decoded),"last diagnostic sequence accepted");
    Check(!Decode(guest,MaxSequence,frame,42,0,decoded),"sequence cannot wrap");
    Check(!Decode(guest,MaxSequence-1,frame,41,0,decoded) && !Decode(guest,MaxSequence-1,frame,42,UINT32_MAX,decoded),"source must remain admitted");
    puts("PASS native desync detection-turn bit/sentinel preservation, authenticated frame identity, replay, sequence, type, source and size bounds. No game executed.");return 0;
}
