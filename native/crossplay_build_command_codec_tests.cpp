#include "crossplay_build_command_codec.h"
#include <cstdio>
#include <cstdlib>
namespace {
void Check(bool value,const char* reason){if(!value){fprintf(stderr,"FAIL %s\n",reason);exit(1);}}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayBuildCommandGuardOracle(uint32_t edition) {
    return CrossplayNativeCodec::BuildCommandDetail::Methods(static_cast<Crossplay::Edition>(edition))?1u:0u;
}
int main() {
    using namespace CrossplayNativeCodec::BuildCommandDetail;
    for(auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}) {
        unsigned char outer[0x1c]{},nested[0x20]{};const auto p=LogicPin(edition);
        auto put=[](unsigned char* bytes,unsigned offset,uint32_t value){CrossplayChat::Put(bytes+offset,value);};
        put(outer,0,static_cast<uint32_t>(p.outer));put(outer,4,CrossplayBuildCommand::Type);put(outer,8,42);outer[12]=1;
        put(outer,16,reinterpret_cast<uint32_t>(nested));put(outer,20,1);outer[24]=1;
        put(nested,0,static_cast<uint32_t>(p.nested));put(nested,4,CrossplayBuildCommand::PlacementType);nested[8]=1;
        put(nested,12,0x472fc000);put(nested,16,0x47435000);put(nested,20,0x80000000);
        put(nested,24,CrossplayBuildCommand::StorehouseType);put(nested,28,CrossplayBuildCommand::BuildingCategory);
        Check(Source(edition,outer,42,1),"modeled native original storehouse layout");
        Check(!Source(edition,outer,43,1)&&!Source(edition,outer,42,2),"foreign connection and player excluded");
        Check(!Source(edition,outer,42,0)&&!Source(edition,outer,UINT32_MAX,1),"invalid namespace or slot excluded");
        for(unsigned offset:{0u,4u,8u,20u}){outer[offset]^=0x80;Check(!Source(edition,outer,42,1),"outer table/type/source/slot mutation excluded");outer[offset]^=0x80;}
        for(unsigned offset:{0u,4u,8u,24u,28u}){nested[offset]^=0x80;Check(!Source(edition,outer,42,1),"nested table/type/player/entity mutation excluded");nested[offset]^=0x80;}
        put(outer,16,0);Check(!Source(edition,outer,42,1),"missing nested object excluded");
    }
    Check(!Guard(LogicPin(static_cast<Crossplay::Edition>(0))),"unknown edition guard excluded");
    puts("PASS modeled native storehouse object/source/player/geometry layout checks; original serializers, heap, native send and game are not executed by this test.");return 0;
}
