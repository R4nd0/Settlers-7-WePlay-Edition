#include "crossplay_ready.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
namespace {
unsigned checks=0;
void Check(bool value,const char* why){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
std::vector<unsigned char> Wire(uint32_t source) {
    std::vector<unsigned char> result(CrossplayReady::WireSize);
    CrossplayChat::Put(result.data(),CrossplayReady::Class);CrossplayChat::Put(result.data()+4,CrossplayReady::Type);
    CrossplayChat::Put(result.data()+8,source);result[12]=1;return result;
}
}
int main() {
    Crossplay::Peer sender{},receiver{};sender.local_id.fill(0x23);sender.remote_id.fill(0x56);
    receiver.local_id=sender.remote_id;receiver.remote_id=sender.local_id;
    for(auto guest:{0u,1u,42u,0x7fffffffu}) {
        const auto original=Wire(guest);std::vector<unsigned char> frame,decoded;
        Check(CrossplayReady::Encode(sender,original,guest,frame),"encode genuine request shape including zero CID");
        Check(CrossplayReady::Decode(receiver,frame,guest,17,decoded),"decode reciprocal ownership");
        auto expected=original;CrossplayChat::Put(expected.data()+8,17);
        Check(decoded==expected,"only sender CID changes; no requested or achieved Ready state");
        Check(!CrossplayReady::Decode(sender,frame,guest,17,decoded) && decoded.empty(),"wrong direction excluded");
        for(size_t i=0;i<frame.size();++i) {
            auto truncated=frame;truncated.resize(i);
            Check(!CrossplayReady::Decode(receiver,truncated,guest,17,decoded) && decoded.empty(),"all truncations excluded before factory");
        }
        for(size_t i=0;i<frame.size();++i) {
            if(i>=CrossplayReady::Header+8 && i<CrossplayReady::Header+12)continue;
            auto corrupted=frame;corrupted[i]^=0x80;
            Check(!CrossplayReady::Decode(receiver,corrupted,guest,17,decoded),"identity/header/type/slot/boolean corruption excluded");
        }
        for(unsigned state=0;state<2;++state) {
            auto native=original;native.resize(CrossplayReady::NativeStateSize);
            CrossplayChat::Put(native.data()+13,2);native[17]=static_cast<unsigned char>(state);
            Check(CrossplayReady::NativeState(native,guest),"Gold native state shape retains explicit slot and boolean");
            Check(!CrossplayReady::Encode(sender,native,guest,frame),"a state result cannot masquerade as a guest intent");
            for(uint32_t slot:{0u,1u,3u,4u,UINT32_MAX}) {
                CrossplayChat::Put(native.data()+13,slot);
                Check(!CrossplayReady::NativeState(native,guest),"Gold receiver cannot target another native slot");
            }
        }
        Check(CrossplayReady::Encode(sender,original,guest,frame),"fresh frame after rejections");
        frame.push_back(0);Check(!CrossplayReady::Decode(receiver,frame,guest,17,decoded),"trailing bytes excluded");
        Check(!CrossplayReady::Encode(sender,original,UINT32_MAX,frame) && frame.empty(),"sentinel source excluded");
        Check(!CrossplayReady::Decode(receiver,frame,guest,UINT32_MAX,decoded),"sentinel target excluded");
    }
    for(unsigned value=2;value<256;++value) {
        auto native=Wire(0);native.resize(CrossplayReady::NativeStateSize);CrossplayChat::Put(native.data()+13,2);native[17]=static_cast<unsigned char>(value);
        Check(!CrossplayReady::NativeState(native,0),"nonboolean Gold native ReadyState excluded");
    }
    std::printf("PASS %u native Ready request framing checks; no engine, readiness result or world start\n",checks);
}
