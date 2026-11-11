#include "crossplay_session_config.h"
#include "crossplay_connection.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>

namespace {
unsigned checks=0;
void Check(bool value,const char* why) {++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",why);std::exit(1);}}
void Word(std::vector<unsigned char>& bytes,uint32_t value) {const auto at=bytes.size();bytes.resize(at+4);CrossplayChat::Put(bytes.data()+at,value);}
void String(std::vector<unsigned char>& bytes,const std::string& value) {
    bytes.insert(bytes.end(),{'S','t','r','_'});Word(bytes,value.empty()?0u:static_cast<uint32_t>(value.size()+1));
    if(!value.empty()){bytes.insert(bytes.end(),value.begin(),value.end());bytes.push_back(0);}
}
std::vector<unsigned char> Config(CrossplaySessionConfig::Connections ids,unsigned flags=0,const std::string& map="Maps/Skirmish/Monhaim") {
    std::vector<unsigned char> bytes;Word(bytes,CrossplaySessionConfig::Class);Word(bytes,CrossplaySessionConfig::Type);Word(bytes,ids.host);bytes.push_back(1);
    String(bytes,map);String(bytes,{});Word(bytes,8);
    for(unsigned i=0;i<8;++i) {
        bytes.push_back(i<2?static_cast<unsigned char>((flags>>i)&1):0);Word(bytes,i<2?i+1:UINT32_MAX);
        bytes.push_back(i<2?static_cast<unsigned char>((flags>>(i+2))&1):0);Word(bytes,i<2?(i?ids.guest:ids.host):0);bytes.push_back(i<2?4u:0u);
    }
    return bytes;
}
Crossplay::Peer Reverse(Crossplay::Peer p) {std::swap(p.local_id,p.remote_id);return p;}
std::vector<unsigned char> NativeOracle(bool zero_station=false) {
    // Original Gold/Ubisoft/Steam writer and reader visitors all produced this
    // synthetic fixtures. The second fixture represents a local snapshot whose
    // two routing Station values have not yet acquired common-backend CIDs.
    const char* hex=zero_station?
        "e9b9ffbc070019002a000000015374725f160000004d6170732f536b69726d6973682f4d6f6e6861696d005374725f0000000008000000"
        "0101000000000000000004000200000000000000000301030000000000000000020104000000000000000002"
        "00ffffffff00000000000500ffffffff00000000000500ffffffff00000000000500ffffffff000000000005":
        "e9b9ffbc070019002a000000015374725f160000004d6170732f536b69726d6973682f4d6f6e6861696d005374725f0000000008000000"
        "0101000000002a000000040002000000000000000004"
        "00ffffffff00000000000000ffffffff00000000000000ffffffff000000000000"
        "00ffffffff00000000000000ffffffff00000000000000ffffffff000000000000";
    auto digit=[](char c){return static_cast<unsigned char>(c<='9'?c-'0':c-'a'+10);};
    std::vector<unsigned char> bytes;
    for(size_t i=0;i+1<std::strlen(hex);i+=2)bytes.push_back(static_cast<unsigned char>((digit(hex[i])<<4)|digit(hex[i+1])));
    return bytes;
}
}
int main() {
    using namespace CrossplaySessionConfig;
    Crossplay::Peer peer;peer.local_id.fill(17);peer.remote_id.fill(34);const auto reverse=Reverse(peer);
    auto native_oracle=NativeOracle();Layout native_layout;
    Check(Inspect(native_oracle,{42,0},native_layout),"canonical native numeric Station fixture matches its bound routing CIDs");
    const auto native_supported=NativeOracle(true);
    std::vector<unsigned char> native_frame,native_target,canonical;
    Check(!Inspect(native_supported,{42,0},native_layout) && BindSource(native_supported,{42,0},canonical),"local snapshot gets explicit admitted numeric routing projection before transport");
    Check(Encode(peer,canonical,{42,0},native_frame) && Decode(reverse,native_frame,{42,0},{1,0},native_target),"independent original native fixture passes scoped namespace transport");
    Check(Inspect(native_target,{1,0},native_layout),"target fixture routing CIDs accepted");
    for(size_t i=12;i<native_supported.size();++i) {
        const bool routing=(i>=native_layout.slots_at+6 && i<native_layout.slots_at+10)
            || (i>=native_layout.slots_at+SlotBytes+6 && i<native_layout.slots_at+SlotBytes+10);
        if(!routing)Check(native_supported[i]==native_target[i],"all native fixture states/readiness/privacy/slots/map retained");
    }
    for(Connections source: {Connections{42,0},Connections{0,42},Connections{1,0x12345678}})for(unsigned flags=0;flags<16;++flags) {
        const auto original=Config(source,flags);Layout layout;Check(Inspect(original,source,layout),"two humans and six empty native slots accepted");
        Check(layout.map=="Maps/Skirmish/Monhaim","native map copied exactly");
        std::vector<unsigned char> frame,translated;Check(Encode(peer,original,source,frame),"source scoped config frame");
        const Connections target{123,0};Check(Decode(reverse,frame,source,target,translated),"reciprocal scoped config decode");
        Check(Inspect(translated,target,layout),"translated numeric routing fields form an owned target config");
        for(size_t i=0;i<original.size();++i) {
            const bool routing=(i>=8 && i<12) || (i>=layout.slots_at+6 && i<layout.slots_at+10)
                || (i>=layout.slots_at+SlotBytes+6 && i<layout.slots_at+SlotBytes+10);
            if(!routing)Check(original[i]==translated[i],"ready/private/state/slot/map bytes never altered");
        }
        std::vector<unsigned char> again,restored;
        Check(Encode(reverse,translated,target,again) && Decode(peer,again,target,source,restored) && restored==original,"roundtrip namespace translation preserves original bytes");
    }
    const Connections source{42,0},target{1,0};const auto wire=Config(source);Layout layout;Check(Inspect(wire,source,layout),"base fixture accepted");
    for(size_t i=0;i<wire.size();++i)Check(!Inspect(std::vector<unsigned char>(wire.begin(),wire.begin()+i),source,layout),"every truncated payload rejected before native reader");
    auto changed=wire;changed.push_back(0);Check(!Inspect(changed,source,layout),"trailing payload rejected");
    for(size_t i=0;i<13;++i) {changed=wire;changed[i]^=0x80;Check(!Inspect(changed,source,layout),"class/type/source/asynchronous guard");}
    Check(Inspect(wire,source,layout),"restore layout");const auto first=layout.slots_at,second=layout.slots_at+SlotBytes;
    for(size_t at:{first+1,first+6,first+10,second+1,second+6,second+10,second+SlotBytes,wire.size()-1}) {
        changed=wire;changed[at]^=0x80;Check(!Inspect(changed,source,layout),"foreign routing/slot/state/inactive records rejected");
    }
    changed=wire;CrossplayChat::Put(changed.data()+first-4,UINT32_MAX);Check(!Inspect(changed,source,layout),"unbounded vector rejected");
    for(const std::string map: {"", "/absolute", "C:/absolute", "../map", "a/../b", "a/./b", "a//b", "a/", "\\absolute", "a\\..\\b"})
        Check(!Inspect(Config(source,0,map),source,layout),"nonrelative/ambiguous map path rejected");
    Check(Inspect(Config(source,0,std::string(MaximumPath,'x')),source,layout),"exact map length boundary");
    Check(!Inspect(Config(source,0,std::string(MaximumPath+1,'x')),source,layout),"oversize map rejected");
    changed=wire;changed[21]=0;Check(!Inspect(changed,source,layout),"embedded map null rejected");
    std::vector<unsigned char> frame,decoded;Check(Encode(peer,wire,source,frame),"base frame");
    for(size_t i=0;i<Header;++i) {
        changed=frame;changed[i]^=0x80;decoded={1,2,3};Check(!Decode(reverse,changed,source,target,decoded) && decoded.empty(),"changed ownership/version/length clears output");
    }
    for(size_t i=0;i<frame.size();++i)Check(!Decode(reverse,std::vector<unsigned char>(frame.begin(),frame.begin()+i),source,target,decoded),"truncated frames rejected");
    Check(!Decode(peer,frame,source,target,decoded),"reflected frame rejected");
    Check(!Decode(reverse,frame,{43,42},target,decoded),"wrong source host CID rejected");
    for(uint32_t state:{3u,4u}) {
        changed=wire;changed[first+10]=static_cast<unsigned char>(state);changed[second+10]=static_cast<unsigned char>(state);
        Check(Inspect(changed,source,layout),"actual intermediate/human slot states retained");
    }
    for(uint32_t state:{0u,1u,5u}) {
        changed=wire;changed[second+SlotBytes+10]=static_cast<unsigned char>(state);
        Check(Inspect(changed,source,layout),"native unused/open/closed states retained");
    }
    changed=wire;changed[second+SlotBytes]=1;CrossplayChat::Put(changed.data()+second+SlotBytes+1,3);changed[second+SlotBytes+10]=2;
    Check(Inspect(changed,source,layout),"native AI network slot and real readiness retained");
    for(Connections invalid:{Connections{0,0},Connections{UINT32_MAX,0},Connections{1,UINT32_MAX}}) {
        Check(!Encode(peer,wire,invalid,decoded) && decoded.empty(),"invalid source IDs excluded");
        Check(!Decode(reverse,frame,source,invalid,decoded) && decoded.empty(),"invalid target IDs excluded");
    }
    Crossplay::Id projection{};CrossplayChat::Put(projection.data(),77);uint32_t projected=0;
    Check(CrossplayNative::ProjectConnection(Crossplay::Edition::Gold,42,projection,projected) && projected==77,"Gold uses independent numeric alias");
    for(auto edition:{Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory})for(uint32_t own:{0u,1u,42u})
        Check(CrossplayNative::ProjectConnection(edition,own,projection,projected) && projected==(own?0u:1u),"History numeric CID is independent of GUID bytes");
    projection[4]=1;Check(!CrossplayNative::ProjectConnection(Crossplay::Edition::Gold,42,projection,projected) && projected==UINT32_MAX,"GUID cannot be cast to Gold numeric CID");
    projection.fill(0);Check(!CrossplayNative::ProjectConnection(Crossplay::Edition::Gold,42,projection,projected),"zero Gold alias excluded");
    Check(!CrossplayNative::ProjectConnection(static_cast<Crossplay::Edition>(99),42,projection,projected),"unknown edition excluded");
    Check(!CrossplayNative::ProjectConnection(Crossplay::Edition::SteamHistory,UINT32_MAX,projection,projected),"sentinel receiver CID excluded");
    std::printf("PASS %u bounded session-config framing/namespace checks; no game code executed\n",checks);
}
