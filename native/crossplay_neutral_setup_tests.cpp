#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_neutral_setup_codec.h"
#include <cstdio>
#include <cstdlib>
#include <array>
namespace {
namespace N=CrossplayNeutralSetup;
namespace C=CrossplaySessionConfig;
unsigned checks=0;
void Check(bool value){++checks;if(!value){std::printf("FAIL neutral setup %u\n",checks);std::exit(1);}}
void Word(std::vector<unsigned char>& bytes,uint32_t value){const auto at=bytes.size();bytes.resize(at+4);CrossplayChat::Put(bytes.data()+at,value);}
void String(std::vector<unsigned char>& bytes,const std::string& value){
    bytes.insert(bytes.end(),{'S','t','r','_'});Word(bytes,value.empty()?0u:static_cast<uint32_t>(value.size()+1));
    if(!value.empty()){bytes.insert(bytes.end(),value.begin(),value.end());bytes.push_back(0);}
}
std::vector<unsigned char> Configuration(C::Connections ids,const std::string& map){
    std::vector<unsigned char> bytes;Word(bytes,C::Class);Word(bytes,C::Type);Word(bytes,ids.host);bytes.push_back(1);
    String(bytes,map);String(bytes,{});Word(bytes,8);
    for(unsigned i=0;i<8;++i){
        bytes.push_back(i<4?1u:0u);Word(bytes,i<4?i+1:UINT32_MAX);bytes.push_back(0);
        Word(bytes,i<2?(i?ids.guest:ids.host):0);bytes.push_back(i<2?4u:i<4?2u:0u);
    }
    return bytes;
}
std::vector<unsigned char> Team(uint32_t host,uint32_t slot,uint32_t team){
    std::vector<unsigned char> bytes;Word(bytes,CrossplayPlayerOption::Team);Word(bytes,7u|(30u<<16));Word(bytes,host);
    bytes.push_back(1);Word(bytes,slot);bytes.push_back(static_cast<unsigned char>(team));return bytes;
}
}
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayNeutralTeamMethodsOracle(uint32_t value){
    const auto edition=static_cast<Crossplay::Edition>(value);CrossplayNativeCodec::PlayerOptionDetail::Spec pin{};
    return CrossplayNativeCodec::PlayerOptionDetail::Layout(edition,CrossplayPlayerOption::Team,pin)
        && CrossplayNativeCodec::PlayerOptionDetail::Methods(edition,pin)?1u:0u;
}
int main(){
    const C::Connections ids{42,0};C::Layout layout;
    for(const std::string map:{"Maps/Skirmish/Monhaim","Maps/Multiplayer/FourPlayers"}){
        const auto config=Configuration(ids,map);Check(N::Closed(config,ids,map,layout));
        Check(!N::Closed(config,ids,map+"Other",layout));Check(!N::Closed(config,ids,"",layout));
        Check(N::Closed(config,ids,map,layout));const auto offset=layout.slots_at;
        std::array<unsigned char,8*0x18> rows{};
        for(unsigned i=0;i<8;++i){
            const auto* source=config.data()+offset+i*C::SlotBytes;auto* row=rows.data()+i*0x18;
            CrossplayChat::Put(row,source[10]);row[4]=source[0];CrossplayChat::Put(row+8,CrossplayChat::Word(source+1));
            row[12]=source[5];CrossplayChat::Put(row+16,CrossplayChat::Word(source+6));
        }
        const auto baseline=rows;
        Check(N::MatchesSlots(config,ids,map,rows.data(),rows.size()));
        CrossplayChat::Put(rows.data()+16,0);Check(N::MatchesSlots(config,ids,map,rows.data(),rows.size()));rows=baseline;
        Check(!N::MatchesSlots(config,ids,map,nullptr,rows.size()));
        for(size_t size=0;size<rows.size();++size)Check(!N::MatchesSlots(config,ids,map,rows.data(),size));
        for(size_t i=0;i<8;++i)for(size_t field:{0u,4u,8u,12u,16u}){
            rows=baseline;rows[i*0x18+field]^=0x80;
            Check(!N::MatchesSlots(config,ids,map,rows.data(),rows.size()));
        }
        for(size_t i=0;i<config.size();++i)Check(!N::Closed(std::vector<unsigned char>(config.begin(),config.begin()+i),ids,map,layout));
        for(unsigned i=0;i<8;++i)for(size_t field:{0u,1u,5u,6u,10u}){
            auto changed=config;changed[offset+i*C::SlotBytes+field]^=0x80;
            Check(!N::Closed(changed,ids,map,layout));
        }
        auto changed=config;changed[offset+2*C::SlotBytes+10]=1;Check(!N::Closed(changed,ids,map,layout));
        changed=config;changed[offset+2*C::SlotBytes+10]=5;Check(!N::Closed(changed,ids,map,layout));
    }
    Crossplay::Peer host;host.local_id.fill(0x11);host.remote_id.fill(0x22);auto guest=host;std::swap(guest.local_id,guest.remote_id);
    for(uint32_t slot:{3u,4u})for(uint32_t team=1;team<=4;++team){
        const auto wire=Team(ids.host,slot,team);std::vector<unsigned char> frame,decoded;uint32_t parsed_slot=0,parsed_team=0;
        Check(N::Wire(wire,ids.host,parsed_slot,parsed_team) && parsed_slot==slot && parsed_team==team);
        Check(N::Frame(host,slot-2,wire,ids.host,frame));const auto original_frame=frame;
        Check(N::Receive(guest,slot-3,ids.host,1,frame,decoded));
        auto expected=wire;CrossplayChat::Put(expected.data()+8,1);Check(decoded==expected);
        Check(!N::Receive(host,slot-3,ids.host,1,frame,decoded) && decoded.empty());
        Check(!N::Receive(guest,slot-2,ids.host,1,frame,decoded));
        Check(!N::Receive(guest,slot-3,ids.host+1,1,frame,decoded));
        Check(!N::Frame(host,slot==3?2u:1u,wire,ids.host,frame));frame=original_frame;
        for(size_t i=0;i<44;++i){auto changed=frame;changed[i]^=0x80;Check(!N::Receive(guest,slot-3,ids.host,1,changed,decoded));}
        for(size_t i=0;i<frame.size();++i)Check(!N::Receive(guest,slot-3,ids.host,1,std::vector<unsigned char>(frame.begin(),frame.begin()+i),decoded));
        auto changed=frame;changed.push_back(0);Check(!N::Receive(guest,slot-3,ids.host,1,changed,decoded));
        for(size_t i=0;i<13;++i){changed=wire;changed[i]^=0x80;Check(!N::Wire(changed,ids.host,parsed_slot,parsed_team));}
    }
    for(uint32_t slot:{0u,1u,2u,5u,UINT32_MAX}){uint32_t s=0,t=0;Check(!N::Wire(Team(42,slot,1),42,s,t));}
    for(uint32_t team:{0u,5u,255u}){uint32_t s=0,t=0;Check(!N::Wire(Team(42,3,team),42,s,t));}
    std::vector<unsigned char> frame;const auto wire=Team(42,3,1);
    for(unsigned kind=0;kind<3;++kind){auto bad=host;if(kind==0)bad.local_id={};if(kind==1)bad.remote_id={};if(kind==2)bad.local_id=bad.remote_id;Check(!N::Frame(bad,1,wire,42,frame));}
    std::printf("PASS %u neutral setup/map/cache/order/peer/frame exclusions; no original native method or world started.\n",checks);
}
