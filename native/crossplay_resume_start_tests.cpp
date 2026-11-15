#include "crossplay_resume_start.h"
#include "crossplay_game_start.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL resume start line %u\n",__LINE__);return 1;}}while(0)
    namespace R=CrossplayResumeStart;
    Crossplay::Peer a{},b{};a.local_id[0]=1;a.remote_id[0]=2;b.local_id=a.remote_id;b.remote_id=a.local_id;
    CrossplayResumeTransfer::Checkpoint checkpoint{};checkpoint.content[0]=3;checkpoint.setup[0]=4;checkpoint.file[0]=5;checkpoint.neo[0]=6;
    checkpoint.logic_turn=300;checkpoint.replay_turn=301;checkpoint.bytes=4097;checkpoint.features=19;checkpoint.enabled=true;
    for(const std::string name:{"s0","s1009","s4294967295"}){
        std::vector<unsigned char> wire(21,0),frame,decoded;
        CrossplayChat::Put(wire.data(),R::Class);CrossplayChat::Put(wire.data()+4,R::Type);
        CrossplayChat::Put(wire.data()+8,42);wire[12]=1;memcpy(wire.data()+13,"Str_",4);
        CrossplayChat::Put(wire.data()+17,0);
        REQUIRE(R::Inspect(wire,42,name));REQUIRE(CrossplayGameStart::Inspect(wire,42));
        REQUIRE(!R::Inspect(wire,42,""));
        REQUIRE(R::Encode(a,checkpoint,42,name,wire,frame));REQUIRE(R::Decode(b,checkpoint,42,81,name,frame,decoded));
        auto expected=wire;CrossplayChat::Put(expected.data()+8,81);REQUIRE(decoded==expected);
        for(size_t offset=0;offset<frame.size();++offset){
            auto bad=frame;bad[offset]^=0x80;REQUIRE(!R::Decode(b,checkpoint,42,81,name,bad,decoded));REQUIRE(decoded.empty());
        }
        for(size_t length=0;length<frame.size();++length){
            auto bad=frame;bad.resize(length);REQUIRE(!R::Decode(b,checkpoint,42,81,name,bad,decoded));
        }
        auto extra=frame;extra.push_back(0);REQUIRE(!R::Decode(b,checkpoint,42,81,name,extra,decoded));
        REQUIRE(!R::Decode(a,checkpoint,42,81,name,frame,decoded));
        REQUIRE(!R::Decode(b,checkpoint,43,81,name,frame,decoded));
        REQUIRE(!R::Decode(b,checkpoint,42,UINT32_MAX,name,frame,decoded));
        REQUIRE(!R::Decode(b,checkpoint,42,81,"s1000",frame,decoded));
        auto nonempty=wire;CrossplayChat::Put(nonempty.data()+17,1);nonempty.push_back('s');
        REQUIRE(!R::Inspect(nonempty,42,name));
        auto changed=checkpoint;changed.logic_turn++;changed.replay_turn++;
        REQUIRE(!R::Decode(b,changed,42,81,name,frame,decoded));
    }
    for(const std::string name:{"","s","S1009","s1009.s7s","../s1009","s1/2","s1\\2","s 1009","s12345678901"})REQUIRE(!R::SlotName(name));
    printf("PASS %u saved-context start checks: distinct checkpoint/cache-token namespace, actual empty original field/CID projection; fresh guard unchanged. Synthetic transport only, native saved admission still required.\n",checks);return 0;
}
