#include "crossplay_chat.h"
#include <cstdio>
#include <cstdlib>
#include <string>

namespace {
unsigned checks=0;
void Check(bool value) {++checks;if(!value){std::fprintf(stderr,"Chat gate check %u failed\n",checks);std::exit(1);}}
void Word(std::vector<unsigned char>& b,uint32_t v) {const size_t n=b.size();b.resize(n+4);CrossplayChat::Put(b.data()+n,v);}
void String(std::vector<unsigned char>& b,const char* text) {
    b.insert(b.end(),{'S','t','r','_'});const size_t n=std::strlen(text);
    Word(b,n?static_cast<uint32_t>(n+1):0);if(n)b.insert(b.end(),text,text+n+1);
}
std::vector<unsigned char> Chat(uint32_t cid,const char* a="Player",const char* b="Hello") {
    std::vector<unsigned char> w;Word(w,CrossplayChat::Class);Word(w,CrossplayChat::Type);Word(w,cid);w.push_back(1);
    String(w,a);String(w,b);Word(w,0xff0000);Word(w,0);return w;
}
}
int main() {
    Crossplay::Peer peer{},other{};peer.local_id.fill(0x12);peer.remote_id.fill(0x34);
    other.local_id=peer.remote_id;other.remote_id=peer.local_id;
    for(uint32_t own:{0u,42u,1861362472u}) {
        const auto native=Chat(own);std::vector<unsigned char> frame,wire;
        Check(CrossplayChat::Encode(peer,native,own,frame));
        Check(CrossplayChat::Decode(other,frame,own,7,wire));
        auto expected=native;CrossplayChat::Put(expected.data()+8,7);Check(wire==expected);
        for(size_t n=0;n<native.size();++n) {
            const std::vector<unsigned char> short_wire(native.begin(),native.begin()+n);
            Check(!CrossplayChat::Validate(short_wire,own));
        }
        for(size_t n=0;n<frame.size();++n) {
            const std::vector<unsigned char> short_frame(frame.begin(),frame.begin()+n);
            wire={1};Check(!CrossplayChat::Decode(other,short_frame,own,7,wire) && wire.empty());
        }
        for(size_t at:{size_t(0),size_t(4),size_t(8),size_t(12),size_t(13),size_t(17),size_t(21+6)}) {
            auto bad=native;bad[at]^=0xff;Check(!CrossplayChat::Validate(bad,own));
        }
        for(size_t at:{size_t(0),size_t(6),size_t(7),size_t(8),size_t(24),size_t(40)}) {
            auto bad=frame;bad[at]^=1;Check(!CrossplayChat::Decode(other,bad,own,7,wire));
        }
        Check(!CrossplayChat::Decode(peer,frame,own,7,wire));
        Check(!CrossplayChat::Decode(other,frame,own+1,7,wire));
        Check(!CrossplayChat::Decode(other,frame,own,UINT32_MAX,wire));
        auto bad=native;bad.push_back(0);Check(!CrossplayChat::Validate(bad,own));
        bad=native;CrossplayChat::Put(bad.data()+bad.size()-4,4);Check(!CrossplayChat::Validate(bad,own));
        bad=native;CrossplayChat::Put(bad.data()+17,UINT32_MAX);Check(!CrossplayChat::Validate(bad,own));
    }
    Check(CrossplayChat::Validate(Chat(0,"",""),0));
    Check(!CrossplayChat::Validate(Chat(UINT32_MAX),UINT32_MAX));
    Check(!CrossplayChat::Validate(Chat(0,"A\nB"),0));
    const std::string maximum(1024,'x'),oversized(1025,'y');
    Check(CrossplayChat::Validate(Chat(42,maximum.c_str(),maximum.c_str()),42));
    Check(!CrossplayChat::Validate(Chat(42,oversized.c_str(),""),42));
    std::printf("PASS %u bounded lobby-chat framing and CID projection checks\n",checks);
}
