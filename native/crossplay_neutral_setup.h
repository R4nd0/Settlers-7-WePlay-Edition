#pragma once
#include "crossplay_session_config.h"
#include "crossplay_player_option.h"
// Original four-slot fresh-start neutral records. This is not an AI layout,
// world agreement or a substitute for observing original native consumption.
namespace CrossplayNeutralSetup {
inline constexpr unsigned char Magic[8]={'S','7','N','T','E','A','M',1};
inline bool ValidPeer(const Crossplay::Peer& peer){
    bool own=false,remote=false;
    for(auto byte:peer.local_id)own=own || byte!=0;
    for(auto byte:peer.remote_id)remote=remote || byte!=0;
    return own && remote && peer.local_id!=peer.remote_id;
}
inline bool Wire(const std::vector<unsigned char>& wire,uint32_t host,uint32_t& slot,uint32_t& value) {
    slot=value=0;
    if(host==UINT32_MAX || wire.size()!=18 || CrossplayChat::Word(wire.data())!=CrossplayPlayerOption::Team
        || CrossplayChat::Word(wire.data()+4)!=(7u|(30u<<16)) || CrossplayChat::Word(wire.data()+8)!=host || wire[12]!=1)return false;
    const uint32_t s=CrossplayChat::Word(wire.data()+13),v=wire[17];
    if(s<3 || s>4 || v<1 || v>4)return false;
    slot=s;value=v;return true;
}
inline bool Closed(const std::vector<unsigned char>& wire,CrossplaySessionConfig::Connections ids,const std::string& expected_map,CrossplaySessionConfig::Layout& layout) {
    if(!CrossplaySessionConfig::RelativeMap(expected_map) || !CrossplaySessionConfig::Inspect(wire,ids,layout) || layout.map!=expected_map)return false;
    for(size_t i=0;i<8;++i) {
        const auto* row=wire.data()+layout.slots_at+i*11;
        if(row[5])return false;
        if(i<2) {if(row[0]!=1 || (row[10]!=3 && row[10]!=4))return false;}
        else if(i<4) {if(row[10]!=2 || row[0]!=1 || CrossplayChat::Word(row+1)!=i+1)return false;}
        else if(row[10]!=0)return false;
    }
    return true;
}
inline bool MatchesSlots(const std::vector<unsigned char>& wire,CrossplaySessionConfig::Connections ids,const std::string& expected_map,const unsigned char* rows,size_t size) {
    CrossplaySessionConfig::Layout layout;
    if(!rows || size!=8*0x18 || !Closed(wire,ids,expected_map,layout))return false;
    for(size_t i=0;i<8;++i) {
        const auto* native=rows+i*0x18;const auto* expected=wire.data()+layout.slots_at+i*11;
        // Authenticated transport projects the two human routing CIDs. Owned
        // local caches observed in both real engines retain canonical zero,
        // explicitly allowed by SlotValid(local_snapshot=true). Roster guards
        // bind those humans separately; neutral/inactive stations stay exact.
        const auto station=CrossplayChat::Word(native+16),expected_station=CrossplayChat::Word(expected+6);
        if(native[4]!=expected[0] || native[12]!=expected[5] || CrossplayChat::Word(native)!=expected[10]
            || CrossplayChat::Word(native+8)!=CrossplayChat::Word(expected+1)
            || (station!=expected_station && !(i<2 && station==0)))return false;
    }
    return true;
}
inline bool Frame(const Crossplay::Peer& peer,uint32_t revision,const std::vector<unsigned char>& wire,uint32_t host,std::vector<unsigned char>& frame) {
    frame.clear();uint32_t slot=0,value=0;if(!ValidPeer(peer) || revision<1 || revision>2 || !Wire(wire,host,slot,value) || slot!=revision+2)return false;
    frame.assign(Magic,Magic+8);frame.insert(frame.end(),peer.local_id.begin(),peer.local_id.end());frame.insert(frame.end(),peer.remote_id.begin(),peer.remote_id.end());
    frame.resize(44);CrossplayChat::Put(frame.data()+40,revision);frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Receive(const Crossplay::Peer& peer,uint32_t previous,uint32_t source,uint32_t local,const std::vector<unsigned char>& frame,std::vector<unsigned char>& wire) {
    wire.clear();if(!ValidPeer(peer) || previous>=2 || source==UINT32_MAX || local==UINT32_MAX || frame.size()!=62 || memcmp(frame.data(),Magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)!=previous+1)return false;
    std::vector<unsigned char> value(frame.begin()+44,frame.end());uint32_t slot=0,team=0;
    if(!Wire(value,source,slot,team) || slot!=previous+3)return false;
    CrossplayChat::Put(value.data()+8,local);if(!Wire(value,local,slot,team))return false;wire=std::move(value);return true;
}
}
