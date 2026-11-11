#pragma once
#include "crossplay_chat.h"
#include <string>

// Experimental two-human fresh lobby configuration only. Original ready,
// private and slot states are preserved. No save transfer, world start, content
// agreement or simulation permission is provided by this frame.
namespace CrossplaySessionConfig {
constexpr uint32_t Class=0xbcffb9e9,Type=7u|(25u<<16);
constexpr size_t Header=44,MaximumPath=255,Slots=8,SlotBytes=11,MaximumWire=13+2*(8+MaximumPath+1)+4+Slots*SlotBytes;
struct Connections {uint32_t host=UINT32_MAX,guest=UINT32_MAX;};
struct Layout {size_t slots_at=0;std::string map;};
inline bool Distinct(Connections ids) {return ids.host!=UINT32_MAX && ids.guest!=UINT32_MAX && ids.host!=ids.guest;}
inline bool SlotValid(size_t index,unsigned ready,unsigned privacy,uint32_t slot,uint32_t station,uint32_t state,
                      Connections ids,bool local_snapshot=false) {
    if(index>=Slots || ready>1 || privacy>1)return false;
    // Both original GetSlotByStation implementations compare this exact scalar
    // with a numeric routing CID. It is not a principal ID or a Storm GUID.
    if(index<2)return slot==index+1 && (state==3 || state==4)
        && (station==(index?ids.guest:ids.host) || (local_snapshot && !station));
    if(station)return false;
    if(state==2)return index<4 && ready==1 && slot==index+1;
    return (state==0 || state==1 || state==5) && !ready && slot==UINT32_MAX;
}
inline bool String(const std::vector<unsigned char>& bytes,size_t& at,std::string& text) {
    text.clear();
    if(at>bytes.size() || bytes.size()-at<8 || memcmp(bytes.data()+at,"Str_",4))return false;
    const auto count=CrossplayChat::Word(bytes.data()+at+4);at+=8;
    if(count==1 || count>MaximumPath+1 || count>bytes.size()-at)return false;
    if(count) {
        if(bytes[at+count-1])return false;
        for(size_t i=0;i+1<count;++i)if(bytes[at+i]<32 || bytes[at+i]>126)return false;
        text.assign(reinterpret_cast<const char*>(bytes.data()+at),count-1);
    }
    at+=count;return true;
}
inline bool RelativeMap(const std::string& path) {
    if(path.empty() || path.size()>MaximumPath || path[0]=='/' || path[0]=='\\' || path.find(':')!=std::string::npos)return false;
    size_t start=0;
    for(size_t i=0;i<=path.size();++i)if(i==path.size() || path[i]=='/' || path[i]=='\\') {
        if(i==start || (i-start==1 && path[start]=='.') || (i-start==2 && path[start]=='.' && path[start+1]=='.'))return false;
        start=i+1;
    }
    return true;
}
inline bool Inspect(const std::vector<unsigned char>& wire,Connections ids,Layout& result,bool local_snapshot=false) {
    result={};
    if(!Distinct(ids) || wire.size()<13 || wire.size()>MaximumWire
        || CrossplayChat::Word(wire.data())!=Class || CrossplayChat::Word(wire.data()+4)!=Type
        || CrossplayChat::Word(wire.data()+8)!=ids.host || wire[12]!=1)return false;
    size_t at=13;std::string map,save;
    if(!String(wire,at,map) || !RelativeMap(map) || !String(wire,at,save) || !save.empty()
        || wire.size()-at!=4+Slots*SlotBytes || CrossplayChat::Word(wire.data()+at)!=Slots)return false;
    at+=4;Layout layout;layout.map=std::move(map);layout.slots_at=at;
    for(size_t i=0;i<Slots;++i,at+=SlotBytes) {
        const auto ready=wire[at],private_slot=wire[at+5],state=wire[at+10];
        const uint32_t slot=CrossplayChat::Word(wire.data()+at+1),station=CrossplayChat::Word(wire.data()+at+6);
        if(!SlotValid(i,ready,private_slot,slot,station,state,ids,local_snapshot))return false;
    }
    result=std::move(layout);return true;
}
inline bool BindSource(const std::vector<unsigned char>& snapshot,Connections ids,std::vector<unsigned char>& wire) {
    wire.clear();Layout layout;
    if(!Inspect(snapshot,ids,layout,true))return false;
    auto projected=snapshot;
    CrossplayChat::Put(projected.data()+layout.slots_at+6,ids.host);
    CrossplayChat::Put(projected.data()+layout.slots_at+SlotBytes+6,ids.guest);
    if(!Inspect(projected,ids,layout))return false;
    wire=std::move(projected);return true;
}
inline bool Encode(const Crossplay::Peer& peer,const std::vector<unsigned char>& wire,Connections source,std::vector<unsigned char>& frame) {
    frame.clear();Layout layout;
    if(!Inspect(wire,source,layout))return false;
    const unsigned char magic[8]={'S','7','S','C','F','G',0,1};frame.assign(magic,magic+8);
    frame.insert(frame.end(),peer.local_id.begin(),peer.local_id.end());
    frame.insert(frame.end(),peer.remote_id.begin(),peer.remote_id.end());
    frame.resize(Header);CrossplayChat::Put(frame.data()+40,static_cast<uint32_t>(wire.size()));
    frame.insert(frame.end(),wire.begin(),wire.end());return true;
}
inline bool Decode(const Crossplay::Peer& peer,const std::vector<unsigned char>& frame,Connections source,Connections target,std::vector<unsigned char>& wire) {
    wire.clear();const unsigned char magic[8]={'S','7','S','C','F','G',0,1};
    if(!Distinct(target) || frame.size()<Header || frame.size()>Header+MaximumWire || memcmp(frame.data(),magic,8)
        || memcmp(frame.data()+8,peer.remote_id.data(),16) || memcmp(frame.data()+24,peer.local_id.data(),16)
        || CrossplayChat::Word(frame.data()+40)!=frame.size()-Header)return false;
    std::vector<unsigned char> decoded(frame.begin()+Header,frame.end());Layout layout;
    if(!Inspect(decoded,source,layout))return false;
    // Only three numeric routing fields change. Ready, privacy, network slots,
    // intermediate/AI/closed states and map bytes retain the original values.
    CrossplayChat::Put(decoded.data()+8,target.host);
    CrossplayChat::Put(decoded.data()+layout.slots_at+6,target.host);
    CrossplayChat::Put(decoded.data()+layout.slots_at+SlotBytes+6,target.guest);
    if(!Inspect(decoded,target,layout))return false;
    wire=std::move(decoded);return true;
}
}
