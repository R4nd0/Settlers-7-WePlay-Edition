#include "crossplay_ai_profile.h"
#include <cstdio>
#include <cstdlib>
#include <algorithm>
namespace {
unsigned checks=0;
void Check(bool value,const char* reason){++checks;if(!value){std::fprintf(stderr,"FAIL %s\n",reason);std::exit(1);}}
std::vector<unsigned char> Event(uint32_t slot,uint32_t profile=11) {
    std::vector<unsigned char> bytes(CrossplayAIProfile::WireSize);
    CrossplayChat::Put(bytes.data(),CrossplayAIProfile::Class);
    CrossplayChat::Put(bytes.data()+4,CrossplayAIProfile::Type);
    CrossplayChat::Put(bytes.data()+8,42);bytes[12]=1;
    CrossplayChat::Put(bytes.data()+13,slot);CrossplayChat::Put(bytes.data()+17,profile);return bytes;
}
std::vector<unsigned char> Config() {
    std::vector<unsigned char> bytes(13);
    CrossplayChat::Put(bytes.data(),CrossplaySessionConfig::Class);
    CrossplayChat::Put(bytes.data()+4,CrossplaySessionConfig::Type);
    CrossplayChat::Put(bytes.data()+8,42);bytes[12]=1;
    const std::string map="Maps\\Skirmish\\s04_m06_Monhaim_Valley";
    bytes.insert(bytes.end(),{'S','t','r','_'});auto at=bytes.size();bytes.resize(at+4);
    CrossplayChat::Put(bytes.data()+at,static_cast<uint32_t>(map.size()+1));
    bytes.insert(bytes.end(),map.begin(),map.end());bytes.push_back(0);
    bytes.insert(bytes.end(),{'S','t','r','_',0,0,0,0});at=bytes.size();bytes.resize(at+4);
    CrossplayChat::Put(bytes.data()+at,8);
    for(unsigned i=0;i<8;++i) {
        at=bytes.size();bytes.resize(at+CrossplaySessionConfig::SlotBytes);
        bytes[at]=i<4?1u:0u;CrossplayChat::Put(bytes.data()+at+1,i<4?i+1:UINT32_MAX);
        CrossplayChat::Put(bytes.data()+at+6,i<2?(i?77u:42u):0u);bytes[at+10]=i<2?3u:i<4?4u:0u;
    }
    return bytes;
}
}
int main() {
    using namespace CrossplayAIProfile;
    Crossplay::Peer sender{},receiver{};sender.local_id.fill(0x11);sender.remote_id.fill(0x22);
    receiver=sender;std::swap(receiver.local_id,receiver.remote_id);
    Value value{};std::vector<unsigned char> frame,delivered;
    for(uint32_t slot:{3u,4u})for(uint32_t profile:{1u,11u,0x7fffffffu}) {
        const auto original=Event(slot,profile);
        Check(Wire(original,42,value) && value.slot==slot && value.profile==profile,"original bounded AI fields");
        Check(Frame(sender,slot-2,original,42,frame) && frame.size()==FrameSize,"ordered source frame");
        Check(Receive(receiver,slot-3,42,77,frame,delivered),"authenticated namespace projection");
        auto expected=original;CrossplayChat::Put(expected.data()+8,77);
        Check(delivered==expected,"only native host CID is translated");
        Check(!Receive(receiver,slot-2,42,77,frame,delivered) && delivered.empty(),"duplicate or later revision excluded");
    }
    for(uint32_t slot:{0u,1u,2u,5u,UINT32_MAX})Check(!Wire(Event(slot),42,value),"human/inactive slots excluded");
    for(uint32_t profile:{0u,0x80000000u,UINT32_MAX})Check(!Wire(Event(3,profile),42,value),"invalid profile scalar excluded");
    Check(!Wire(Event(3),UINT32_MAX,value) && value.slot==0 && value.profile==0,"invalid source clears output");
    for(size_t offset=0;offset<13;++offset) {
        auto bytes=Event(3);bytes[offset]^=0x80;
        Check(!Wire(bytes,42,value),"class/type/source/async mutations excluded");
    }
    for(size_t length=0;length<=WireSize+1;++length)if(length!=WireSize) {
        auto bytes=Event(3);bytes.resize(length);
        Check(!Wire(bytes,42,value),"truncation/trailing bytes excluded");
    }
    Check(Frame(sender,1,Event(3),42,frame),"baseline frame");
    for(size_t offset=0;offset<Header;++offset) {
        auto changed=frame;changed[offset]^=0x80;
        Check(!Receive(receiver,0,42,77,changed,delivered),"purpose/identity/revision mutation excluded");
    }
    for(size_t length=0;length<=FrameSize+1;++length)if(length!=FrameSize) {
        auto changed=frame;changed.resize(length);
        Check(!Receive(receiver,0,42,77,changed,delivered),"frame length excluded");
    }
    Check(!Frame(sender,2,Event(3),42,delivered) && delivered.empty(),"slot reordering excluded");
    Check(!Frame(sender,0,Event(3),42,delivered),"zero revision excluded");
    Check(!Frame(sender,3,Event(4),42,delivered),"third event excluded");
    Check(!Receive(receiver,0,43,77,frame,delivered),"foreign source CID excluded");
    Check(!Receive(receiver,0,42,UINT32_MAX,frame,delivered),"invalid projected CID excluded");
    Check(!Receive(receiver,2,42,77,frame,delivered),"completed interval excluded");
    const auto config=Config();CrossplaySessionConfig::Layout layout;
    Check(FreshConfig(config,{42,77},layout),"original four-slot AI snapshot policy");
    const auto slots=layout.slots_at;
    for(unsigned i=0;i<8;++i) {
        auto changed=config;changed[slots+i*CrossplaySessionConfig::SlotBytes+5]=1;
        Check(!FreshConfig(changed,{42,77},layout),"private slot snapshot excluded");
        if(i<4) {
            changed=config;changed[slots+i*CrossplaySessionConfig::SlotBytes]=0;
            Check(!FreshConfig(changed,{42,77},layout),"pending real readiness excluded");
        }
        changed=config;CrossplayChat::Put(changed.data()+slots+i*CrossplaySessionConfig::SlotBytes+1,99);
        Check(!FreshConfig(changed,{42,77},layout),"wrong native slot identity excluded");
    }
    for(unsigned i=2;i<8;++i) {
        auto changed=config;CrossplayChat::Put(changed.data()+slots+i*CrossplaySessionConfig::SlotBytes+6,42);
        Check(!FreshConfig(changed,{42,77},layout),"extra network human station excluded");
        changed=config;changed[slots+i*CrossplaySessionConfig::SlotBytes+10]=i<4?2u:4u;
        Check(!FreshConfig(changed,{42,77},layout),"closed AI or additional CPU excluded");
    }
    auto different=config;different[21]^=1;
    Check(!FreshConfig(different,{42,77},layout),"different map excluded");
    Check(!FreshConfig(config,{42,42},layout),"aliased humans excluded");
    Check(!FreshConfig(config,{43,77},layout),"foreign host excluded");
    std::printf("PASS %u AI-profile framing/ordering/namespace checks; no native game code executed.\n",checks);
}
