#include "crossplay_bootstrap.h"
#include <cstdio>
int main(){
    unsigned checks=0;
#define CHECK(x) do{++checks;if(!(x)){printf("FAIL bootstrap line %u\n",__LINE__);return 1;}}while(0)
    Crossplay::Peer host,guest;host.local_id[0]=1;host.remote_id[0]=2;guest.local_id=host.remote_id;guest.remote_id=host.local_id;
    for(bool saved:{false,true})for(uint32_t flags=0;flags<8;++flags)
        for(const std::string map:{"Maps\\Skirmish\\SelectedMap", "Maps/Skirmish/OtherSelectedMap"})
            for(const std::string map_template:{"", "SelectedTemplate", "\\Download\\Template", "\\Download\\Template\\SelectedVariant"}){
        CrossplayBootstrap::Context source{map,saved,flags,map_template,true},decoded;std::vector<unsigned char> frame;
        CHECK(CrossplayBootstrap::Encode(host,source,frame));CHECK(CrossplayBootstrap::Decode(guest,frame,decoded));
        CHECK(decoded.map==map&&decoded.saved==saved&&decoded.flags==flags&&decoded.map_template==map_template&&decoded.choices_observed);
        for(size_t at=0;at<frame.size();++at){
            auto changed=frame;changed[at]^=0x80;
            CHECK(!CrossplayBootstrap::Decode(guest,changed,decoded));
            CHECK(!decoded.choices_observed&&decoded.map.empty()&&decoded.map_template.empty());
        }
        CHECK(!CrossplayBootstrap::Decode(host,frame,decoded));
        for(size_t n:{size_t(0),size_t(7),size_t(47),size_t(55),frame.size()-1}){auto cut=frame;cut.resize(n);CHECK(!CrossplayBootstrap::Decode(guest,cut,decoded));}
        auto trailing=frame;trailing.push_back('x');CHECK(!CrossplayBootstrap::Decode(guest,trailing,decoded));
        auto old=frame;old[7]=1;CHECK(!CrossplayBootstrap::Decode(guest,old,decoded));
        auto invalid=frame;CrossplayChat::Put(invalid.data()+40,2);CHECK(!CrossplayBootstrap::Decode(guest,invalid,decoded));
        invalid=frame;CrossplayChat::Put(invalid.data()+48,8);CHECK(!CrossplayBootstrap::Decode(guest,invalid,decoded));
        invalid=frame;CrossplayChat::Put(invalid.data()+44,UINT32_MAX);CHECK(!CrossplayBootstrap::Decode(guest,invalid,decoded));
        invalid=frame;CrossplayChat::Put(invalid.data()+52,UINT32_MAX);CHECK(!CrossplayBootstrap::Decode(guest,invalid,decoded));
    }
    for(const std::string map:{"", "../Map", "Maps/../Map", "/absolute", "Z:/Map", "Maps//Map", "Maps/./Map", "Maps/Map/", "Maps\\Map\n"}){
        std::vector<unsigned char> frame{1,2,3};CHECK(!CrossplayBootstrap::Encode(host,{map,false,0,"",true},frame)&&frame.empty());
    }
    for(const std::string map_template:{"../Template", "Templates/../Map", "/absolute", "Z:/Template", "Templates//Map", "Templates/./Map",
        "Template/", "Template\n", "\\Download\\Other", "\\Download\\Template\\..\\Outside", "\\\\server\\Template"}){
        std::vector<unsigned char> frame{1,2,3};CHECK(!CrossplayBootstrap::Encode(host,{"Maps/Map",false,0,map_template,true},frame)&&frame.empty());
    }
    std::vector<unsigned char> frame;CrossplayBootstrap::Context decoded;
    CHECK(CrossplayBootstrap::Encode(host,{std::string(255,'a'),true,7,std::string(255,'b'),true},frame));
    CHECK(CrossplayBootstrap::Decode(guest,frame,decoded)&&decoded.flags==7&&decoded.map_template.size()==255);
    CHECK(!CrossplayBootstrap::Encode(host,{std::string(256,'a'),false,0,"",true},frame));
    CHECK(!CrossplayBootstrap::Encode(host,{"Maps/Map",false,0,std::string(256,'a'),true},frame));
    CHECK(!CrossplayBootstrap::Encode(host,{"Maps/Map",false},frame));
    CHECK(!CrossplayBootstrap::Encode(host,{"Maps/Map",false,8,"",true},frame));
    host.remote_id=host.local_id;CHECK(!CrossplayBootstrap::Encode(host,{"Maps/Map",false,0,"",true},frame));
    printf("PASS %u host bootstrap-v2 checks: actual map/saved-kind/host flags/template, exact peer and bounds; no default choices, native readiness or world authority.\n",checks);
}
