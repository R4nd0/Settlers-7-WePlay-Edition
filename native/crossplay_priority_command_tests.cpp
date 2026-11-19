#include "crossplay_priority_command.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL priority command line%u\n",__LINE__);return 1;}}while(0)
    Crossplay::Peer a{},b{};a.local_id[0]=1;a.remote_id[0]=2;b.local_id=a.remote_id;b.remote_id=a.local_id;
    std::vector<unsigned char> wire(CrossplayPriorityCommand::WireSize,0),frame,decoded;
    auto put=[&](size_t at,uint32_t value){CrossplayChat::Put(wire.data()+at,value);};
    put(0,CrossplayPriorityCommand::Class);put(4,CrossplayPriorityCommand::Type);put(8,42);put(13,CrossplayPriorityCommand::NestedClass);put(17,CrossplayPriorityCommand::PriorityType);
    put(22,65536);put(26,65537);
    for(uint32_t player=1;player<=8;++player)for(unsigned asynchronous:{0u,1u})for(unsigned sync:{0u,1u}){
        wire[12]=static_cast<unsigned char>(asynchronous);wire[21]=static_cast<unsigned char>(player);put(30,player);wire[34]=static_cast<unsigned char>(sync);
        REQUIRE(CrossplayPriorityCommand::Inspect(wire,42,player));REQUIRE(CrossplayPriorityCommand::Encode(a,1,wire,42,player,frame));
        REQUIRE(CrossplayPriorityCommand::Decode(b,0,frame,42,81,player,decoded));auto expected=wire;CrossplayChat::Put(expected.data()+8,81);REQUIRE(decoded==expected);
        REQUIRE(!CrossplayPriorityCommand::Decode(b,1,frame,42,81,player,decoded));REQUIRE(!CrossplayPriorityCommand::Decode(a,0,frame,42,81,player,decoded));
        REQUIRE(!CrossplayPriorityCommand::Decode(b,0,frame,42,81,player%8+1,decoded));
    }
    wire[21]=1;put(30,1);wire[12]=0;wire[34]=1;
    for(auto invalid:{0u,9u,UINT32_MAX}){
        auto outside=wire;outside[21]=static_cast<unsigned char>(invalid);CrossplayChat::Put(outside.data()+30,invalid);
        REQUIRE(!CrossplayPriorityCommand::Inspect(outside,42,invalid));
    }
    for(auto at:{0u,4u,8u,13u,17u,21u,30u}){auto bad=wire;bad[at]^=0x80;REQUIRE(!CrossplayPriorityCommand::Inspect(bad,42,1));}
    for(auto at:{12u,34u}){auto bad=wire;bad[at]=2;REQUIRE(!CrossplayPriorityCommand::Inspect(bad,42,1));}
    for(auto id:{0u,UINT32_MAX,65537u}){put(22,id);REQUIRE(!CrossplayPriorityCommand::Inspect(wire,42,1));}put(22,65536);
    put(26,0);REQUIRE(CrossplayPriorityCommand::Inspect(wire,42,1));put(26,UINT32_MAX);REQUIRE(!CrossplayPriorityCommand::Inspect(wire,42,1));put(26,65537);
    REQUIRE(!CrossplayPriorityCommand::Encode(a,0,wire,42,1,frame));REQUIRE(CrossplayPriorityCommand::Encode(a,1,wire,42,1,frame));
    for(size_t n=0;n<frame.size();++n){auto bad=frame;bad.resize(n);REQUIRE(!CrossplayPriorityCommand::Decode(b,0,bad,42,81,1,decoded));}
    auto extra=frame;extra.push_back(0);REQUIRE(!CrossplayPriorityCommand::Decode(b,0,extra,42,81,1,decoded));
    REQUIRE(!CrossplayPriorityCommand::Decode(b,UINT32_MAX,frame,42,81,1,decoded));
    printf("PASS %u priority command checks: original handles/player/flags preserved, CID projection only, namespace/order/size/type/invalid handle rejection. Synthetic transport only.\n",checks);return 0;
}
