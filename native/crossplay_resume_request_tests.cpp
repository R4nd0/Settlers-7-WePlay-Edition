#include "crossplay_resume_request.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL request line %u\n",__LINE__);return 1;}}while(0)
    namespace R=CrossplayResumeRequest;
    Crossplay::Peer a{},b{};a.local_id[0]=1;a.remote_id[0]=2;b.local_id=a.remote_id;b.remote_id=a.local_id;
    CrossplayResumeTransfer::Checkpoint checkpoint{};checkpoint.content[0]=3;checkpoint.setup[0]=4;checkpoint.file[0]=5;checkpoint.neo[0]=6;
    checkpoint.logic_turn=300;checkpoint.replay_turn=301;checkpoint.bytes=4097;checkpoint.features=19;checkpoint.enabled=true;
    std::vector<unsigned char> wire(13),frame,decoded;CrossplayChat::Put(wire.data(),R::Class);CrossplayChat::Put(wire.data()+4,R::Type);
    CrossplayChat::Put(wire.data()+8,42);wire[12]=1;
    REQUIRE(R::Inspect(wire,42));REQUIRE(R::Encode(a,checkpoint,42,wire,frame));
    REQUIRE(R::Decode(b,checkpoint,42,81,frame,decoded));auto projected=wire;CrossplayChat::Put(projected.data()+8,81);REQUIRE(decoded==projected);
    for(size_t offset=0;offset<frame.size();++offset){auto bad=frame;bad[offset]^=0x80;REQUIRE(!R::Decode(b,checkpoint,42,81,bad,decoded));REQUIRE(decoded.empty());}
    for(size_t length=0;length<frame.size();++length){auto bad=frame;bad.resize(length);REQUIRE(!R::Decode(b,checkpoint,42,81,bad,decoded));}
    auto bad=frame;bad.push_back(0);REQUIRE(!R::Decode(b,checkpoint,42,81,bad,decoded));
    REQUIRE(!R::Decode(a,checkpoint,42,81,frame,decoded));REQUIRE(!R::Decode(b,checkpoint,43,81,frame,decoded));
    REQUIRE(!R::Decode(b,checkpoint,42,UINT32_MAX,frame,decoded));
    auto changed=checkpoint;changed.neo[1]=1;REQUIRE(!R::Decode(b,changed,42,81,frame,decoded));
    printf("PASS %u native file-request checks: exact checkpoint/peer/type/CID binding, no Ready or completion fabrication. Synthetic transport only.\n",checks);
    return 0;
}
