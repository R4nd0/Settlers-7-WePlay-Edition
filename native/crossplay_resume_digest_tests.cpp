#include "crossplay_resume_digest.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define REQUIRE(x) do{++checks;if(!(x)){printf("FAIL saved-file hash line %u\n",__LINE__);return 1;}}while(0)
    namespace R=CrossplayResumeTransfer;
    R::Checkpoint cp{};cp.content[0]=3;cp.setup[0]=4;cp.neo[0]=6;
    const unsigned char expected[]={0xcc,0xae,0xc9,0xb6,0xfd,0xc8,0x0e,0xc4,0xa1,0x07,0x24,0x08,0x94,0x87,0x7d,0x69,0xfd,0x75,0x9b,0x50,0x2d,0x28,0x85,0xa4,0x06,0xe8,0xf0,0x18,0x97,0xa9,0xc1,0x1e};
    memcpy(cp.file.data(),expected,32);cp.logic_turn=300;cp.replay_turn=301;cp.bytes=8201;cp.features=19;cp.enabled=true;
    auto packet=[&](uint32_t offset,uint32_t length){
        std::vector<unsigned char> wire(R::NativeHeader+length);CrossplayChat::Put(wire.data(),R::Class);CrossplayChat::Put(wire.data()+4,R::Type);
        CrossplayChat::Put(wire.data()+8,42);wire[12]=1;CrossplayChat::Put(wire.data()+13,length);CrossplayChat::Put(wire.data()+17,cp.bytes);
        CrossplayChat::Put(wire.data()+21,0x12345678);for(uint32_t i=0;i<length;++i)wire[R::NativeHeader+i]=static_cast<unsigned char>((offset+i)*17+3);return wire;
    };
    const auto first=packet(0,8192),last=packet(8192,9),end=packet(8201,0);
    R::FileDigest digest;REQUIRE(!digest.Add(first,42));REQUIRE(digest.Bind(cp));REQUIRE(!digest.Bind(cp));
    REQUIRE(digest.Add(first,42));REQUIRE(!digest.Verified());REQUIRE(digest.ReceivedBytes()==8192);
    REQUIRE(digest.Add(last,42));REQUIRE(digest.Verified());REQUIRE(digest.ReceivedBytes()==8201);REQUIRE(digest.Add(end,42));
    REQUIRE(!digest.Add(last,42));REQUIRE(!digest.Verified());
    R::FileDigest corrupt;REQUIRE(corrupt.Bind(cp));auto changed=first;changed[R::NativeHeader+4096]^=1;
    REQUIRE(corrupt.Add(changed,42));REQUIRE(!corrupt.Verified());REQUIRE(!corrupt.Add(last,42));REQUIRE(!corrupt.Verified());REQUIRE(!corrupt.Add(end,42));
    R::FileDigest early;REQUIRE(early.Bind(cp));REQUIRE(!early.Add(end,42));REQUIRE(!early.Add(first,42));
    R::FileDigest wrong;auto altered=cp;altered.file[31]^=1;REQUIRE(wrong.Bind(altered));REQUIRE(wrong.Add(first,42));REQUIRE(!wrong.Add(last,42));REQUIRE(!wrong.Verified());
    // Changing native routing metadata does not change the world's digest.
    R::FileDigest projected;REQUIRE(projected.Bind(cp));auto a=first,b=last;CrossplayChat::Put(a.data()+8,81);CrossplayChat::Put(b.data()+8,81);
    REQUIRE(projected.Add(a,81));REQUIRE(projected.Add(b,81));REQUIRE(projected.Verified());
    printf("PASS %u incremental native SHA-256 checks; final corrupt data excluded before native completion.\n",checks);return 0;
}
