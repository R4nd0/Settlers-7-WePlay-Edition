#include "crossplay_road_command_codec.h"
#include <cstdio>
#include <cstdlib>
namespace {void Check(bool v,const char* why){if(!v){fprintf(stderr,"FAIL %s\n",why);exit(1);}}}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayRoadCommandGuardOracle(uint32_t edition){return CrossplayNativeCodec::RoadCommandDetail::Methods(static_cast<Crossplay::Edition>(edition))?1u:0u;}
int main(){using namespace CrossplayNativeCodec::RoadCommandDetail;
    for(auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory})for(uint32_t player:{1u,2u}) {
        unsigned char outer[0x1c]{},nested[0x20]{};auto put=[](unsigned char* p,unsigned at,uint32_t v){CrossplayChat::Put(p+at,v);};
        put(outer,0,static_cast<uint32_t>(CrossplayNativeCodec::BuildCommandDetail::Pin(edition).event_vt));put(outer,4,CrossplayRoadCommand::Type);put(outer,8,42);outer[12]=1;
        put(outer,16,reinterpret_cast<uint32_t>(nested));put(outer,20,player);outer[24]=1;
        put(nested,0,static_cast<uint32_t>(RoadPin(edition).table));put(nested,4,CrossplayRoadCommand::PlacementType);nested[8]=static_cast<unsigned char>(player);
        put(nested,12,CrossplayRoadCommand::NormalStreetType);put(nested,16,0x472fc000);put(nested,20,0x47435000);put(nested,24,0x47300000);put(nested,28,0x47440000);
        Check(Source(edition,outer,42,player),"modeled original road event for both players");Check(!Source(edition,outer,43,player)&&!Source(edition,outer,42,3-player),"foreign source/player excluded");
        for(unsigned at:{0u,4u,8u,20u}){outer[at]^=0x80;Check(!Source(edition,outer,42,player),"outer table/type/source/slot mutation excluded");outer[at]^=0x80;}
        for(unsigned at:{0u,4u,8u,12u}){nested[at]^=0x80;Check(!Source(edition,outer,42,player),"nested table/type/player/street mutation excluded");nested[at]^=0x80;}
        for(unsigned at:{16u,20u,24u,28u}){const auto before=CrossplayChat::Word(nested+at);put(nested,at,0x7fc00000);Check(!Source(edition,outer,42,player),"nonfinite endpoint excluded");put(nested,at,before);}
        put(outer,16,0);Check(!Source(edition,outer,42,player),"missing road object excluded");
    }
    Check(!Methods(static_cast<Crossplay::Edition>(0)),"invalid edition excluded before native access");
    puts("PASS modeled native normal-road layout/source/player/geometry guards; no original native heap, reader, send or game executes.");return 0;
}
