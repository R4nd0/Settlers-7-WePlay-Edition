#include "crossplay_build_command.h"
#include <cstdio>
#include <cstdlib>
namespace {
void Check(bool value,const char* reason){if(!value){fprintf(stderr,"FAIL %s\n",reason);exit(1);}}
std::vector<unsigned char> Wire(uint32_t player) {
    using namespace CrossplayBuildCommand;std::vector<unsigned char> wire(WireSize);
    const std::pair<size_t,uint32_t> fields[]={{0,Class},{4,Type},{8,42},{13,NestedClass},{17,PlacementType},
        {22,0x472fc000},{26,0x47435000},{30,0x80000000},{34,StorehouseType},{38,BuildingCategory},{42,player}};
    for(const auto& field:fields)CrossplayChat::Put(wire.data()+field.first,field.second);
    wire[12]=1;wire[21]=static_cast<unsigned char>(player);wire[46]=1;return wire;
}
}
int main() {
    using namespace CrossplayBuildCommand;Crossplay::Peer a{},b{};a.local_id[0]=1;a.remote_id[0]=2;b.local_id=a.remote_id;b.remote_id=a.local_id;
    std::vector<unsigned char> frame,decoded;
    for(uint32_t player:{1u,2u}) {
        const auto original=Wire(player);Check(Encode(a,1,original,42,player,frame),"original placement frame");
        Check(Decode(b,0,frame,42,7,player,decoded),"opposite edition source namespace");
        auto expected=original;CrossplayChat::Put(expected.data()+8,7);Check(decoded==expected,"only outer connection changes; all command/player/position/source-slot/flags preserved");
        Check(!Decode(b,1,frame,42,7,player,decoded)&&decoded.empty(),"replay excluded");
        Check(!Decode(b,UINT32_MAX,frame,42,7,player,decoded),"sequence cannot wrap");
        Check(!Decode(b,0,frame,43,7,player,decoded),"foreign original source excluded");
        Check(!Decode(b,0,frame,42,7,3-player,decoded),"foreign player and source slot excluded");
        for(size_t length=0;length<frame.size();++length) {
            const std::vector<unsigned char> short_frame(frame.begin(),frame.begin()+length);
            Check(!Decode(b,0,short_frame,42,7,player,decoded),"all truncations rejected");
        }
        auto extra=frame;extra.push_back(0);Check(!Decode(b,0,extra,42,7,player,decoded),"trailing data excluded");
        for(size_t offset:{0u,8u,24u,40u,44u,Header,Header+4,Header+13,Header+17,Header+21,Header+34,Header+38,Header+42}) {
            auto changed=frame;changed[offset]^=0x80;Check(!Decode(b,0,changed,42,7,player,decoded),"identity/type/subtype/player/category/sequence mutation excluded");
        }
        for(size_t offset:{12u,46u}){auto changed=original;changed[offset]=2;Check(!Inspect(changed,42,player),"non-boolean native flags excluded");}
        for(size_t offset:{22u,26u,30u})for(uint32_t bits:{0x7f800000u,0xff800000u,0x7fc00000u,0x7f7fffffu}) {
            auto changed=original;CrossplayChat::Put(changed.data()+offset,bits);Check(!Inspect(changed,42,player),"nonfinite or out-of-range geometry excluded");
        }
        Check(!Encode(a,0,original,42,player,frame)&&frame.empty(),"zero sequence excluded");
    }
    puts("PASS bounded storehouse-command framing: both players, opaque scalar preservation, source namespaces, replay/overflow, every truncation, foreign identities/types/players, malformed geometry and flags. No native command or game executed.");return 0;
}
