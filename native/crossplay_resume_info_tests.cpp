#include "crossplay_resume_info.h"
#include "crossplay_resume_credit.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL saved-info line %u\n",__LINE__);return 1;}}while(0)
    namespace I=CrossplayResumeInfo;namespace C=CrossplayResumeCredit;
    Crossplay::Peer a{},b{};a.local_id[0]=1;a.remote_id[0]=2;b.local_id=a.remote_id;b.remote_id=a.local_id;
    CrossplayResumeTransfer::Checkpoint cp{};cp.content[0]=3;cp.setup[0]=4;cp.file[0]=5;cp.neo[0]=6;
    cp.logic_turn=300;cp.replay_turn=301;cp.bytes=8201;cp.features=19;cp.enabled=true;
    std::vector<unsigned char> wire(31),frame,decoded;CrossplayChat::Put(wire.data(),I::Class);CrossplayChat::Put(wire.data()+4,I::Type);
    CrossplayChat::Put(wire.data()+8,42);wire[12]=1;memcpy(wire.data()+13,"Str_",4);CrossplayChat::Put(wire.data()+17,6);
    memcpy(wire.data()+21,"s1009",6);CrossplayChat::Put(wire.data()+27,0x01020304);
    REQUIRE(I::Inspect(wire,42));REQUIRE(I::Encode(a,cp,42,wire,frame));REQUIRE(I::Decode(b,cp,42,81,frame,decoded));
    auto projected=wire;CrossplayChat::Put(projected.data()+8,81);REQUIRE(decoded==projected);
    for(size_t at=0;at<I::Header+21;++at){auto bad=frame;bad[at]^=0x80;REQUIRE(!I::Decode(b,cp,42,81,bad,decoded));REQUIRE(decoded.empty());}
    for(size_t size=0;size<frame.size();++size){auto bad=frame;bad.resize(size);REQUIRE(!I::Decode(b,cp,42,81,bad,decoded));}
    auto bad=frame;bad.push_back(0);REQUIRE(!I::Decode(b,cp,42,81,bad,decoded));
    REQUIRE(!I::Decode(a,cp,42,81,frame,decoded));auto other=cp;other.neo[1]=7;REQUIRE(!I::Decode(b,other,42,81,frame,decoded));
    bad=wire;bad[26]=1;REQUIRE(!I::Inspect(bad,42));bad=wire;bad[22]=0;REQUIRE(!I::Inspect(bad,42));
    // The original scalar is opaque and must survive unchanged, even at its
    // boundaries. Authentication of the body belongs to the channel MAC.
    for(uint32_t scalar:{0u,1u,UINT32_MAX}){CrossplayChat::Put(wire.data()+27,scalar);REQUIRE(I::Encode(a,cp,42,wire,frame));REQUIRE(I::Decode(b,cp,42,81,frame,decoded));REQUIRE(CrossplayChat::Word(decoded.data()+27)==scalar);}
    uint32_t sequence=0,bytes=0;REQUIRE(C::Encode(a,cp,8,8192,frame));REQUIRE(C::Decode(b,cp,frame,sequence,bytes));REQUIRE(sequence==8 && bytes==8192);
    for(size_t at=0;at<188;++at){auto changed=frame;changed[at]^=0x80;REQUIRE(!C::Decode(b,cp,changed,sequence,bytes));REQUIRE(!sequence && !bytes);}
    bad=frame;CrossplayChat::Put(bad.data()+188,0);REQUIRE(!C::Decode(b,cp,bad,sequence,bytes));
    bad=frame;CrossplayChat::Put(bad.data()+192,cp.bytes+1);REQUIRE(!C::Decode(b,cp,bad,sequence,bytes));
    REQUIRE(!C::Decode(a,cp,frame,sequence,bytes));
    printf("PASS %u saved-info/transport-credit checks; opaque native payload preserved; no native acknowledgement generated.\n",checks);return 0;
}
