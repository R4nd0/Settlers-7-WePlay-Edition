#pragma once
#include "crossplay_chat.h"
#include "crossplay_session_config.h"

// Opt-in two-human/two-AI lobby primitive. This frame grants no preparation,
// Ready, world-start, simulation or save permission. The caller must bind the
// exact four-slot setup and authenticate the channel before using it.
namespace CrossplayAIProfile {
constexpr uint32_t Class=0x1056e082,Type=7u|(57u<<16);
constexpr size_t WireSize=21,Header=44,FrameSize=Header+WireSize;
inline constexpr unsigned char Magic[8]={'S','7','A','I','P','R','F',1};
struct Value {uint32_t slot=0,profile=0;};
inline bool FreshConfig(const std::vector<unsigned char>& wire,CrossplaySessionConfig::Connections ids,
                        CrossplaySessionConfig::Layout& output) {
    output={};
    if(!CrossplaySessionConfig::Distinct(ids) || wire.size()<13 || wire.size()>CrossplaySessionConfig::MaximumWire
        || CrossplayChat::Word(wire.data())!=CrossplaySessionConfig::Class
        || CrossplayChat::Word(wire.data()+4)!=CrossplaySessionConfig::Type
        || CrossplayChat::Word(wire.data()+8)!=ids.host || wire[12]!=1)return false;
    size_t at=13;std::string map,save;
    if(!CrossplaySessionConfig::String(wire,at,map) || map!="Maps\\Skirmish\\s04_m06_Monhaim_Valley"
        || !CrossplaySessionConfig::String(wire,at,save) || !save.empty()
        || wire.size()-at!=4+8*CrossplaySessionConfig::SlotBytes || CrossplayChat::Word(wire.data()+at)!=8)return false;
    at+=4;CrossplaySessionConfig::Layout result;result.map=std::move(map);result.slots_at=at;
    for(unsigned i=0;i<8;++i,at+=CrossplaySessionConfig::SlotBytes) {
        const auto ready=wire[at],privacy=wire[at+5],state=wire[at+10];
        const auto slot=CrossplayChat::Word(wire.data()+at+1),station=CrossplayChat::Word(wire.data()+at+6);
        if(privacy)return false;
        if(i<2) {
            if(ready!=1 || !CrossplaySessionConfig::SlotValid(i,ready,privacy,slot,station,state,ids))return false;
        } else if(i<4) {
            if(ready!=1 || state!=4 || slot!=i+1 || station)return false;
        } else if(ready || (state!=0 && state!=5) || slot!=UINT32_MAX || station)return false;
    }
    output=std::move(result);return true;
}
inline bool Wire(const std::vector<unsigned char>& wire,uint32_t host,Value& output) {
    output={};
    if(host==UINT32_MAX || wire.size()!=WireSize || CrossplayChat::Word(wire.data())!=Class
        || CrossplayChat::Word(wire.data()+4)!=Type || CrossplayChat::Word(wire.data()+8)!=host
        || wire[12]!=1)return false;
    const Value value{CrossplayChat::Word(wire.data()+13),CrossplayChat::Word(wire.data()+17)};
    if(value.slot<3 || value.slot>4 || !value.profile || value.profile>0x7fffffffu)return false;
    output=value;return true;
}
inline bool Frame(const Crossplay::Peer& peer,uint32_t revision,const std::vector<unsigned char>& wire,
                  uint32_t host,std::vector<unsigned char>& frame) {
    frame.clear();Value value{};
    if(revision<1 || revision>2 || !Wire(wire,host,value) || value.slot!=revision+2)return false;
    frame.assign(Magic,Magic+8);
    frame.insert(frame.end(),peer.local_id.begin(),peer.local_id.end());
    frame.insert(frame.end(),peer.remote_id.begin(),peer.remote_id.end());
    frame.resize(Header);CrossplayChat::Put(frame.data()+40,revision);
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Receive(const Crossplay::Peer& peer,uint32_t previous,uint32_t source,uint32_t local,
                    const std::vector<unsigned char>& frame,std::vector<unsigned char>& wire) {
    wire.clear();
    if(previous>=2 || source==UINT32_MAX || local==UINT32_MAX || frame.size()!=FrameSize
        || memcmp(frame.data(),Magic,8) || memcmp(frame.data()+8,peer.remote_id.data(),16)
        || memcmp(frame.data()+24,peer.local_id.data(),16) || CrossplayChat::Word(frame.data()+40)!=previous+1)return false;
    std::vector<unsigned char> projected(frame.begin()+Header,frame.end());Value value{};
    if(!Wire(projected,source,value) || value.slot!=previous+3)return false;
    // Translate only the authenticated sender's native routing CID. Original
    // slot, profile, event type and asynchronous flag remain byte-exact.
    CrossplayChat::Put(projected.data()+8,local);
    if(!Wire(projected,local,value))return false;
    wire=std::move(projected);return true;
}
}
