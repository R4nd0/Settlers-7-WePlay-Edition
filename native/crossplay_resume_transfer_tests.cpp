#include "crossplay_resume_transfer.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL resume transfer line %u\n",__LINE__);return 1;}}while(0)
    namespace R=CrossplayResumeTransfer;
    Crossplay::Peer a{},b{};a.local_id[0]=1;a.remote_id[0]=2;b.local_id=a.remote_id;b.remote_id=a.local_id;
    R::Checkpoint checkpoint{};checkpoint.content[0]=3;checkpoint.setup[0]=4;checkpoint.file[0]=5;checkpoint.neo[0]=6;
    checkpoint.logic_turn=300;checkpoint.replay_turn=301;checkpoint.bytes=16385;checkpoint.features=19;checkpoint.enabled=true;
    auto make=[&](uint32_t length){
        std::vector<unsigned char> wire(R::NativeHeader+length,0);
        CrossplayChat::Put(wire.data(),R::Class);CrossplayChat::Put(wire.data()+4,R::Type);
        CrossplayChat::Put(wire.data()+8,42);wire[12]=1;CrossplayChat::Put(wire.data()+13,length);
        CrossplayChat::Put(wire.data()+17,checkpoint.bytes);CrossplayChat::Put(wire.data()+21,0x12345678);
        for(uint32_t i=0;i<length;++i)wire[R::NativeHeader+i]=static_cast<unsigned char>(i*17+3);
        return wire;
    };
    auto project=[&](std::vector<unsigned char> wire){CrossplayChat::Put(wire.data()+8,81);return wire;};
    std::vector<unsigned char> frame,decoded;REQUIRE(R::Valid(checkpoint));
    for(uint32_t length:{0u,1u,17u,1024u,8192u}){
        auto wire=make(length);REQUIRE(R::Inspect(wire,42,checkpoint.bytes));REQUIRE(R::Encode(a,checkpoint,1,42,wire,frame));
        REQUIRE(R::Decode(b,checkpoint,1,42,81,frame,decoded));REQUIRE(decoded==project(wire));
        REQUIRE(!R::Decode(a,checkpoint,1,42,81,frame,decoded));REQUIRE(decoded.empty());REQUIRE(!R::Decode(b,checkpoint,2,42,81,frame,decoded));
        REQUIRE(!R::Decode(b,checkpoint,1,43,81,frame,decoded));REQUIRE(!R::Decode(b,checkpoint,1,42,UINT32_MAX,frame,decoded));
        REQUIRE(!R::Decode(b,checkpoint,1,42,81,frame,decoded,1));
    }
    auto wire=make(8192);REQUIRE(R::Encode(a,checkpoint,1,42,wire,frame));
    for(size_t offset=0;offset<R::Header;++offset){auto bad=frame;bad[offset]^=0x80;REQUIRE(!R::Decode(b,checkpoint,1,42,81,bad,decoded));}
    for(size_t length=0;length<frame.size();++length){auto bad=frame;bad.resize(length);REQUIRE(!R::Decode(b,checkpoint,1,42,81,bad,decoded));}
    for(size_t offset:{0u,4u,8u,12u,13u,17u}){auto bad=wire;bad[offset]^=0x80;REQUIRE(!R::Inspect(bad,42,checkpoint.bytes));}
    auto extra=frame;extra.push_back(0);REQUIRE(!R::Decode(b,checkpoint,1,42,81,extra,decoded));
    auto invalid=checkpoint;invalid.replay_turn++;REQUIRE(!R::Valid(invalid));invalid=checkpoint;invalid.logic_turn=UINT32_MAX;invalid.replay_turn=0;REQUIRE(!R::Valid(invalid));
    for(uint32_t bytes:{0u,79u,R::MaximumFileBytes+1}){invalid=checkpoint;invalid.bytes=bytes;REQUIRE(!R::Valid(invalid));}
    for(uint32_t features:{0u,1u,3u,17u,0x80000013u}){invalid=checkpoint;invalid.features=features;REQUIRE(!R::Valid(invalid));}
    invalid=checkpoint;invalid.file={};REQUIRE(!R::Valid(invalid));invalid=checkpoint;invalid.neo={};REQUIRE(!R::Valid(invalid));
    REQUIRE(!R::Encode(a,checkpoint,0,42,wire,frame));REQUIRE(frame.empty());REQUIRE(!R::Inspect(make(8193),42,checkpoint.bytes));
    auto wrong_type=make(17);CrossplayChat::Put(wrong_type.data()+4,0x24);REQUIRE(!R::Inspect(wrong_type,42,checkpoint.bytes));
    R::Receiver receiver;REQUIRE(!receiver.Accept(b,42,81,frame,decoded));REQUIRE(receiver.Bind(checkpoint));REQUIRE(!receiver.Bind(checkpoint));
    uint32_t sequence=0,received=0;
    for(uint32_t length:{8192u,8192u,1u}){
        auto part=make(length);REQUIRE(R::Encode(a,checkpoint,++sequence,42,part,frame,received));REQUIRE(receiver.Accept(b,42,81,frame,decoded));
        REQUIRE(decoded==project(part));received+=length;REQUIRE(receiver.ReceivedBytes()==received);REQUIRE(!receiver.Failed());
    }
    REQUIRE(received==checkpoint.bytes);auto end=make(0);REQUIRE(R::Encode(a,checkpoint,++sequence,42,end,frame,received));
    REQUIRE(receiver.Accept(b,42,81,frame,decoded));REQUIRE(decoded==project(end));REQUIRE(!receiver.Accept(b,42,81,frame,decoded));REQUIRE(receiver.Failed());
    R::Receiver duplicate;REQUIRE(duplicate.Bind(checkpoint));REQUIRE(R::Encode(a,checkpoint,1,42,make(17),frame));REQUIRE(duplicate.Accept(b,42,81,frame,decoded));
    REQUIRE(!duplicate.Accept(b,42,81,frame,decoded));REQUIRE(duplicate.ReceivedBytes()==17);
    R::Receiver zero;REQUIRE(zero.Bind(checkpoint));REQUIRE(R::Encode(a,checkpoint,1,42,make(0),frame));REQUIRE(!zero.Accept(b,42,81,frame,decoded));
    R::Receiver checksum;REQUIRE(checksum.Bind(checkpoint));REQUIRE(R::Encode(a,checkpoint,1,42,make(17),frame));REQUIRE(checksum.Accept(b,42,81,frame,decoded));
    auto changed=make(17);CrossplayChat::Put(changed.data()+21,0x76543210);REQUIRE(R::Encode(a,checkpoint,2,42,changed,frame,17));REQUIRE(!checksum.Accept(b,42,81,frame,decoded));
    R::Receiver offset;REQUIRE(offset.Bind(checkpoint));REQUIRE(R::Encode(a,checkpoint,1,42,make(17),frame,42));REQUIRE(!offset.Accept(b,42,81,frame,decoded));
    REQUIRE(!R::Encode(a,checkpoint,1,42,make(17),frame,checkpoint.bytes-16));
    printf("PASS %u resume-file checks: checkpoint/peer/order/explicit frame offset binding, native CID projection only, original checksum/payload preserved; synthetic transport only.\n",checks);
    return 0;
}
