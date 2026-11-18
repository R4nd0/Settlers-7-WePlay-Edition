// Exercise the complete admission protocol over loopback with modeled game
// callbacks. The compile/link against the real original backend is checked
// separately in crossplay_admission_tests.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "crossplay_admission.h"
#include <algorithm>
#include <cstdio>
#include <cstdlib>
#include <cstring>
#include <string>

namespace {
using namespace Crossplay;
using namespace CrossplayNative;
void Check(bool value,const char* reason) {
    if(!value){std::fprintf(stderr,"Room admission failed: %s\n",reason);std::exit(1);}
}
template<size_t N> std::array<unsigned char,N> ReadHex(const std::string& key) {
    char buffer[N*2+1]{};
    Check(GetEnvironmentVariableA(key.c_str(),buffer,sizeof(buffer))==N*2,"test launch field is available");
    std::array<unsigned char,N> bytes{};
    for(size_t i=0;i<N;++i) {
        char pair[]={buffer[2*i],buffer[2*i+1],0};
        bytes[i]=static_cast<unsigned char>(std::strtoul(pair,nullptr,16));
    }
    SetEnvironmentVariableA(key.c_str(),nullptr);
    return bytes;
}
struct QueuedMember {
    Participant player;
    uint32_t status=0;
};
struct Model {
    LobbySnapshot lobby;
    std::vector<QueuedMember> queued;
    bool started=false;
};
std::array<Model,RoomSnapshot::MaximumMembers> models;
std::array<Edition,RoomSnapshot::MaximumMembers> editions;
size_t next_binding=0;

void Consume(Model& model) {
    for(const auto& event:model.queued) {
        auto member=std::find_if(model.lobby.members.begin(),model.lobby.members.end(),[&](const auto& current){
            return current.player.connection==event.player.connection;
        });
        if(event.status==1) {
            Check(member==model.lobby.members.end(),"duplicate native add");
            model.lobby.members.push_back({event.player,event.status});
        } else {
            Check(member!=model.lobby.members.end(),"confirmation without added member");
            *member={event.player,event.status};
            if(event.player.local) {
                model.lobby.host=event.player.host;
                model.lobby.local_slot=event.player.slot;
            }
        }
    }
    model.queued.clear();
}
}

namespace CrossplayNative {
struct ParticipantEvent::Impl {
    Model* model=nullptr;
    QueuedMember constructed;
};
ParticipantEvent::ParticipantEvent():impl(new Impl){}
ParticipantEvent::~ParticipantEvent()=default;
bool ParticipantEvent::Bind(Edition edition){
    if(impl->model || next_binding>=models.size() || edition!=editions[next_binding])return false;
    impl->model=&models[next_binding++];
    return true;
}
bool ParticipantEvent::CaptureLobby(LobbySnapshot& result)const{
    if(!impl->model || impl->model->started)return false;
    result=impl->model->lobby;return true;
}
bool ParticipantEvent::CaptureStartedSession(LobbySnapshot& result)const{
    if(!impl->model || !impl->model->started)return false;
    result=impl->model->lobby;return true;
}
bool ParticipantEvent::CaptureCastleSetup(const void* castle,CrossplayCastleSetup::Value& result)const{
    if(!impl->model || !castle)return false;
    result=*static_cast<const CrossplayCastleSetup::Value*>(castle);return true;
}
bool ParticipantEvent::Construct(Change change,const Participant& member,const void*,uint32_t,bool){
    if(!impl->model || impl->constructed.status || change!=Change::Added)return false;
    impl->constructed={member,1};return true;
}
bool ParticipantEvent::ConstructConfirmedSetup(const Participant& member,const CrossplayCastleSetup::Value&,uint32_t,bool){
    if(!impl->model || impl->constructed.status)return false;
    impl->constructed={member,2};return true;
}
bool ParticipantEvent::EnqueueLocal(){
    if(!impl->model || !impl->constructed.status)return false;
    impl->model->queued.push_back(impl->constructed);return true;
}
bool ParticipantEvent::Reset(){impl->constructed={};return true;}
}

int main(int argc,char** argv) {
    Check(argc==5 && !std::strcmp(argv[4],"room"),"fixture command line");
    const size_t count=std::strlen(argv[2]);
    const auto port=std::strtoul(argv[1],nullptr,10);
    const auto host=std::strtoul(argv[3],nullptr,10);
    Check(count>=2 && count<=models.size() && host>=1 && host<=count && port>0 && port<=65535,"room launch bounds");
    const auto session=ReadHex<16>("WEPLAY_CROSSPLAY_TEST_SESSION");
    std::array<Channel,RoomSnapshot::MaximumMembers> channels;
    std::array<RoomLobbyAdmission,RoomSnapshot::MaximumMembers> admissions;
    std::array<unsigned char,RoomSnapshot::MaximumMembers> slots{};
    unsigned char next_slot=2;
    for(size_t i=0;i<count;++i) {
        const auto number=std::to_string(i+1);
        RoomConfig config{};
        config.edition=editions[i]=static_cast<Edition>(argv[2][i]-'0');
        config.port=static_cast<uint16_t>(port);
        config.slot=static_cast<unsigned char>(i+1);
        config.host_slot=static_cast<unsigned char>(host);
        config.member_count=static_cast<unsigned char>(count);
        config.generation=7;
        config.session=session;
        config.key=ReadHex<32>("WEPLAY_CROSSPLAY_ROOM_KEY_"+number);
        config.executable=ReadHex<32>("WEPLAY_CROSSPLAY_ROOM_EXE_"+number);
        config.content.fill(0x51);
        config.native_identity.fill(config.edition==Edition::Gold?0:static_cast<unsigned char>(42+i));
        config.native_identity[0]=static_cast<unsigned char>(42+i);
        const uint32_t cid=config.edition==Edition::Gold?static_cast<uint32_t>(42+i):0;
        models[i].lobby={1+i,1,true,{{{cid,1,100u+static_cast<uint32_t>(i),cid,"Member"+number,true,true},2}}};
        slots[i]=i+1==host?1:next_slot++;
        Check(channels[i].StartRoom(config),"native channel starts");
    }
    const auto deadline=GetTickCount64()+10000;
    while(GetTickCount64()<deadline) {
        size_t connected=0;
        for(size_t i=0;i<count;++i){
            Check(channels[i].Poll(),"room handshake");
            if(channels[i].GetRoom())++connected;
        }
        if(connected==count)break;
        Sleep(1);
    }
    const CrossplayCastleSetup::Value castle{{"woodland",{{"Keep",UINT32_MAX,0}}},"Default"};
    for(size_t i=0;i<count;++i)
        Check(admissions[i].Begin(channels[i],static_cast<unsigned char>(i+1),slots,&castle,0),"begin in local lobby");
    unsigned ticks=0;
    while(GetTickCount64()<deadline) {
        size_t bound=0;
        for(size_t i=0;i<count;++i) {
            // Leave one client's native roster pending for several pumps.
            if(i!=count-1 || ticks>30)Consume(models[i]);
            Check(admissions[i].Poll(),"admission poll");
            if(admissions[i].GetState()==AdmissionState::RosterBound)++bound;
        }
        if(ticks<=30)Check(!bound,"cannot complete before last native readback");
        if(bound==count)break;
        ++ticks;
        Sleep(1);
    }
    for(size_t i=0;i<count;++i) {
        Check(admissions[i].GetState()==AdmissionState::RosterBound,"all readbacks acknowledged");
        unsigned known=0;
        for(unsigned char j=1;j<=count;++j) {
            RoomRoute route;
            Check(admissions[i].Resolve(j,false,route),"resolve route from original-shaped roster");
            Check(route.host==(j==host) && route.local==(j==i+1) && route.player_slot==slots[j-1],"roles and map slots");
            Check(!admissions[i].Resolve(j,true,route),"lobby not a started world");
            ++known;
        }
        Check(known==count,"every player has a route");
        CrossplaySessionConfig::RoomConnections config;
        Check(admissions[i].CaptureConfigurationRoutes(static_cast<unsigned char>(count),config),
            "capture map routing from the complete original-shaped roster");
        RoomRoute host_route;
        Check(admissions[i].Resolve(static_cast<unsigned char>(host),false,host_route)
            && config.host==host_route.receiver_cid && config.stations[0]==config.host,"configuration follows the chosen host");
        Check(!admissions[i].CaptureConfigurationRoutes(static_cast<unsigned char>(count-1),config),
            "human outside the map limit rejects the configuration");
        models[i].started=true;
        RoomRoute route;
        Check(admissions[i].Resolve(static_cast<unsigned char>(host),true,route),"separate started phase requires native readback");
        models[i].lobby.members.pop_back();
        Check(!admissions[i].Resolve(static_cast<unsigned char>(host),true,route),"departed player invalidates every world route");
    }
    puts("PASS complete WPX2 room admission protocol with modeled game callbacks.");
}
