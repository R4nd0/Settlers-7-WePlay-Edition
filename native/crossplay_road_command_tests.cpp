#include "crossplay_road_command.h"
#include <cstdio>
#include <cstdlib>
namespace {
void Check(bool v,const char* why){if(!v){fprintf(stderr,"FAIL %s\n",why);exit(1);}}
std::vector<unsigned char> Wire(uint32_t player){using namespace CrossplayRoadCommand;std::vector<unsigned char> wire(WireSize);
    const std::pair<size_t,uint32_t> words[]={{0,Class},{4,Type},{8,42},{13,NestedClass},{17,PlacementType},{22,NormalStreetType},
        {26,0x472fc000},{30,0x47435000},{34,0x47300000},{38,0x47440000},{42,player}};
    for(const auto& v:words)CrossplayChat::Put(wire.data()+v.first,v.second);wire[12]=1;wire[21]=static_cast<unsigned char>(player);wire[46]=1;return wire;}
}
int main(){using namespace CrossplayRoadCommand;Crossplay::Peer a{},b{};a.local_id[0]=1;a.remote_id[0]=2;b.local_id=a.remote_id;b.remote_id=a.local_id;
    std::vector<unsigned char> frame,wire;
    for(uint32_t player:{1u,2u}){const auto original=Wire(player);Check(Encode(a,1,original,42,player,frame),"normal native street frame");
        Check(Decode(b,0,frame,42,7,player,wire),"opposite native namespace");auto expected=original;CrossplayChat::Put(expected.data()+8,7);Check(wire==expected,"only outer CID changes; road endpoints and all opaque fields preserved");
        Check(!Decode(b,1,frame,42,7,player,wire)&&wire.empty(),"replay excluded");Check(!Decode(b,UINT32_MAX,frame,42,7,player,wire),"sequence wrap excluded");
        Check(!Decode(b,0,frame,43,7,player,wire)&&!Decode(b,0,frame,42,7,3-player,wire),"foreign source/player excluded");
        for(size_t n=0;n<frame.size();++n)Check(!Decode(b,0,std::vector<unsigned char>(frame.begin(),frame.begin()+n),42,7,player,wire),"every truncation excluded");
        auto extra=frame;extra.push_back(0);Check(!Decode(b,0,extra,42,7,player,wire),"trailing bytes excluded");
        for(size_t at:{0u,8u,24u,40u,44u,Header,Header+4,Header+13,Header+17,Header+21,Header+22,Header+42}){auto changed=frame;changed[at]^=0x80;Check(!Decode(b,0,changed,42,7,player,wire),"identity/type/player/street/source-slot mutations excluded");}
        for(size_t at:{26u,30u,34u,38u})for(uint32_t bits:{0x7f800000u,0xff800000u,0x7fc00000u,0x7f7fffffu}){auto changed=original;CrossplayChat::Put(changed.data()+at,bits);Check(!Inspect(changed,42,player),"malformed or unbounded endpoints excluded");}
        for(size_t at:{12u,46u}){auto changed=original;changed[at]=2;Check(!Inspect(changed,42,player),"invalid boolean excluded");}
    }
    puts("PASS bounded normal-street framing for both players, exact endpoint/flag preservation, namespace remap, replay/overflow and malformed/truncated input rejection. No native command or game executed.");return 0;
}
