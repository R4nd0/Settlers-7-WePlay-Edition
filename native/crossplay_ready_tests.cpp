#include "crossplay_ready.h"
#include "crossplay_ready_state.h"
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
    for(unsigned host=0;host<2;++host)for(unsigned guest=0;guest<2;++guest) {
        CrossplayReadyState::Snapshot state{1,static_cast<unsigned char>(host),static_cast<unsigned char>(guest)},decoded;
        std::vector<unsigned char> frame;
        Check(CrossplayReadyState::Encode(sender,state,frame),"encode observed two-human Ready snapshot");
        Check(CrossplayReadyState::Decode(receiver,frame,0,decoded) && decoded.revision==1 && decoded.host==host && decoded.guest==guest,"snapshot preserves actual bits");
        Check(!CrossplayReadyState::Decode(sender,frame,0,decoded),"snapshot requires reciprocal identities");
        Check(!CrossplayReadyState::Decode(receiver,frame,1,decoded) && !decoded.revision,"duplicate snapshot excluded");
        Check(!CrossplayReadyState::Decode(receiver,frame,UINT32_MAX,decoded),"revision cannot wrap");
        for(size_t i=0;i<frame.size();++i) {
            auto damaged=frame;damaged[i]^=0x80;
            Check(!CrossplayReadyState::Decode(receiver,damaged,0,decoded) && !decoded.revision,"all noncanonical fields excluded");
            damaged=frame;damaged.resize(i);
            Check(!CrossplayReadyState::Decode(receiver,damaged,0,decoded),"all truncated snapshots excluded");
        }
        frame.push_back(0);Check(!CrossplayReadyState::Decode(receiver,frame,0,decoded),"trailing snapshot bytes excluded");
        state.revision=3;Check(CrossplayReadyState::Encode(sender,state,frame) && !CrossplayReadyState::Decode(receiver,frame,1,decoded),"missing revision fails closed");
        state.revision=2;Check(CrossplayReadyState::Encode(sender,state,frame) && CrossplayReadyState::Decode(receiver,frame,1,decoded),"next revision allowed");
        state.revision=0;Check(!CrossplayReadyState::Encode(sender,state,frame) && frame.empty(),"zero revision excluded");
        for(unsigned value=2;value<256;++value) {
            state={1,static_cast<unsigned char>(value),0};Check(!CrossplayReadyState::Encode(sender,state,frame),"host boolean strict");
            state={1,0,static_cast<unsigned char>(value)};Check(!CrossplayReadyState::Encode(sender,state,frame),"guest boolean strict");
        }
    }
    std::printf("PASS %u Ready intent and observed-state framing checks; no engine or world start\n",checks);
}
