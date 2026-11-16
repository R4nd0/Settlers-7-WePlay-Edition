#include "crossplay_bootstrap.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL bootstrap line %u\n",__LINE__);return 1;}}while(0)
    Crossplay::Peer host,guest;host.local_id[0]=1;host.remote_id[0]=2;guest.local_id=host.remote_id;guest.remote_id=host.local_id;
    for(bool saved:{false,true})for(const std::string map:{"Maps\\Skirmish\\SelectedMap", "Maps/Skirmish/OtherSelectedMap"}){
        CrossplayBootstrap::Context source{map,saved},decoded;std::vector<unsigned char> frame;
        CHECK(CrossplayBootstrap::Encode(host,source,frame));CHECK(CrossplayBootstrap::Decode(guest,frame,decoded));
        CHECK(decoded.map==map&&decoded.saved==saved);
        for(size_t at=0;at<frame.size();++at){
            auto changed=frame;changed[at]^=0x80;
            if(at<CrossplayBootstrap::Header)CHECK(!CrossplayBootstrap::Decode(guest,changed,decoded));
        }
        CHECK(!CrossplayBootstrap::Decode(host,frame,decoded));
        for(size_t n:{size_t(0),size_t(7),size_t(47),frame.size()-1}){auto cut=frame;cut.resize(n);CHECK(!CrossplayBootstrap::Decode(guest,cut,decoded));}
        auto trailing=frame;trailing.push_back('x');CHECK(!CrossplayBootstrap::Decode(guest,trailing,decoded));
    }
    for(const std::string map:{"", "../Map", "Maps/../Map", "/absolute", "Z:/Map", "Maps//Map", "Maps/./Map", "Maps/Map/", "Maps\\Map\n"}){
        std::vector<unsigned char> frame{1,2,3};CHECK(!CrossplayBootstrap::Encode(host,{map,false},frame)&&frame.empty());
    }
    std::vector<unsigned char> frame;CrossplayBootstrap::Context decoded;
    CHECK(CrossplayBootstrap::Encode(host,{std::string(255,'a'),false},frame));CHECK(CrossplayBootstrap::Decode(guest,frame,decoded));
    CHECK(!CrossplayBootstrap::Encode(host,{std::string(256,'a'),false},frame));
    host.remote_id=host.local_id;CHECK(!CrossplayBootstrap::Encode(host,{"Maps/Map",false},frame));
    printf("PASS %u host bootstrap checks: actual relative map/saved-kind, exact peer/shape/length binding; no native readiness or world authority.\n",checks);
}
