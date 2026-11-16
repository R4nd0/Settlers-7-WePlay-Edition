#include "crossplay_neo_selection.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool value){++checks;if(!value){std::printf("FAIL NEO selection check %u\n",checks);std::exit(1);}}
void Word(std::vector<unsigned char>& bytes,uint32_t value){const auto at=bytes.size();bytes.resize(at+4);CrossplayChat::Put(bytes.data()+at,value);}
void Text(std::vector<unsigned char>& bytes,const std::string& value){bytes.insert(bytes.end(),{'S','t','r','_'});Word(bytes,value.empty()?0u:static_cast<uint32_t>(value.size()+1));if(!value.empty()){bytes.insert(bytes.end(),value.begin(),value.end());bytes.push_back(0);}}
std::vector<unsigned char> Configuration(CrossplaySessionConfig::Connections ids,unsigned ready,bool ai){
    std::vector<unsigned char> bytes;Word(bytes,CrossplaySessionConfig::Class);Word(bytes,CrossplaySessionConfig::Type);Word(bytes,ids.host);bytes.push_back(1);
    Text(bytes,"Maps/Skirmish/Monhaim");Text(bytes,{});Word(bytes,8);
    for(unsigned i=0;i<8;++i){const bool human=i<2,computer=ai && i>=2 && i<4;
        bytes.push_back(human?static_cast<unsigned char>((ready>>i)&1):computer?1u:0u);Word(bytes,human||computer?i+1:UINT32_MAX);
        bytes.push_back(0);Word(bytes,human?(i?ids.guest:ids.host):0);bytes.push_back(human?((ready>>i)&1?4u:3u):computer?2u:0u);
    }return bytes;
}
}
int main(){
    using namespace CrossplayNeoSelection;
    const CrossplaySessionConfig::Connections source{42,0};
    for(unsigned flags=0;flags<8;++flags)for(const char* map_template:{"","Maps/Templates/One","\\Download\\Template"})for(bool ai:{false,true}){
        CrossplayBootstrap::Context choices{"Maps/Skirmish/Monhaim",false,flags,map_template,true};
        std::vector<unsigned char> canonical;auto original=Configuration(source,0,ai);const auto retained=original;
        Check(Canonical(original,source,choices,canonical));Check(original==retained);
        for(auto ids:{CrossplaySessionConfig::Connections{42,0},CrossplaySessionConfig::Connections{0,42},CrossplaySessionConfig::Connections{123,456}})for(unsigned ready=0;ready<4;++ready){
            std::vector<unsigned char> other;const auto actual=Configuration(ids,ready,ai);Check(Canonical(actual,ids,choices,other) && other==canonical);
        }
        auto changed=choices;changed.saved=true;std::vector<unsigned char> rejected;Check(!Canonical(original,source,changed,rejected) && rejected.empty());
        changed=choices;changed.map="Maps/Other";Check(!Canonical(original,source,changed,rejected));
        Check(!Canonical(original,{1,2},choices,rejected));
        CrossplaySessionConfig::Layout layout;Check(CrossplaySessionConfig::Inspect(original,source,layout));
        original[layout.slots_at+5]=1;Check(Canonical(original,source,choices,rejected) && rejected!=canonical);
        original=retained;original[layout.slots_at+10]=2;Check(!Canonical(original,source,choices,rejected));
        Check(Canonical(Configuration(source,0,!ai),source,choices,rejected) && rejected!=canonical);
        changed=choices;changed.flags^=1;Check(Canonical(retained,source,changed,rejected) && rejected!=canonical);
    }
    Crossplay::Peer sender;sender.local_id.fill(17);sender.remote_id.fill(34);auto receiver=sender;std::swap(receiver.local_id,receiver.remote_id);
    Value value;value.revision=1;value.content.fill(51);value.configuration.fill(68);
    for(bool enabled:{false,true}){
        value.enabled=enabled;std::vector<unsigned char> frame;Check(Encode(sender,value,frame));Value decoded;
        Check(Decode(receiver,frame,0,value.content,value.configuration,decoded) && decoded.enabled==enabled && decoded.revision==1);
        Check(!Decode(receiver,frame,1,value.content,value.configuration,decoded));
        Check(!Decode(receiver,frame,UINT32_MAX,value.content,value.configuration,decoded));
        Check(!Decode(sender,frame,0,value.content,value.configuration,decoded));
        auto wrong=value.content;wrong[0]^=1;Check(!Decode(receiver,frame,0,wrong,value.configuration,decoded));
        wrong=value.configuration;wrong[0]^=1;Check(!Decode(receiver,frame,0,value.content,wrong,decoded));
        for(size_t i=0;i<frame.size();++i){
            auto bad=frame;bad[i]^=1;
            // The source's boolean itself is allowed to change. Its authenticity
            // comes from the surrounding authenticated channel, not this codec.
            const bool expected=i==48;
            Check(Decode(receiver,bad,0,value.content,value.configuration,decoded)==expected);
        }
        for(size_t i=0;i<frame.size();++i)Check(!Decode(receiver,std::vector<unsigned char>(frame.begin(),frame.begin()+i),0,value.content,value.configuration,decoded));
        frame.push_back(0);Check(!Decode(receiver,frame,0,value.content,value.configuration,decoded));
        value.revision=0;Check(!Encode(sender,value,frame));value.revision=1;
    }
    std::printf("PASS %u fresh NEO selection context/namespace/revision/feature/bounds checks; no native Ready/world or saved-state authority.\n",checks);return 0;
}
