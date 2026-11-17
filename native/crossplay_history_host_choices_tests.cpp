#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_history_host_choices.h"
#include <cstdio>
#include <cstdlib>
namespace {unsigned checks=0;void Check(bool value){++checks;if(!value){printf("FAIL History host choices check %u\n",checks);std::exit(1);}}}
int main(){
    using namespace CrossplayHistoryHostChoices;
    for(const std::string save:{"","s0","s1000","s4294967295"})Check(SaveToken(save));
    for(const std::string save:std::vector<std::string>{"Savegames/MPSave","../s1000","s-1","s1.s7s","C:/s1","s","S1",std::string("s1\0",3)})Check(!SaveToken(save));
    CrossplayNative::Participant expected{0,1,0,0,"SteamHost",true,true};
    CrossplayNative::LobbySnapshot lobby{0x20000,1,true,{{expected,2}}};
    Check(Local(lobby,0x20000,expected));
    auto changed=lobby;changed.local_slot=2;Check(!Local(changed,0x20000,expected));
    changed=lobby;changed.host=false;Check(!Local(changed,0x20000,expected));
    changed=lobby;changed.members[0].status=1;Check(!Local(changed,0x20000,expected));
    changed=lobby;changed.members[0].player.local=false;Check(!Local(changed,0x20000,expected));
    changed=lobby;changed.members[0].player.host=false;Check(!Local(changed,0x20000,expected));
    changed=lobby;changed.members[0].player.slot=2;Check(!Local(changed,0x20000,expected));
    changed=lobby;changed.members[0].player.name="Other";Check(!Local(changed,0x20000,expected));
    changed=lobby;changed.members[0].player.principal=99;Check(!Local(changed,0x20000,expected));
    auto invalid=expected;invalid.principal=UINT32_MAX;changed=lobby;changed.members[0].player=invalid;Check(!Local(changed,0x20000,invalid));
    invalid=expected;invalid.name.clear();changed=lobby;changed.members[0].player=invalid;Check(!Local(changed,0x20000,invalid));
    changed=lobby;changed.members[0].player.connection=99;Check(!Local(changed,0x20000,expected));
    changed=lobby;changed.members.push_back(changed.members[0]);Check(!Local(changed,0x20000,expected));
    Check(!Local(lobby,0,expected));Check(!Local(lobby,0x20001,expected));
    Value a{{1,2,3,4,5,"Maps/Actual","",7},"s1000"},b=a;Check(Same(a,b));
    b.save="s1001";Check(!Same(a,b));b=a;b.choices.map="Maps/Other";Check(!Same(a,b));
    CrossplayNative::ParticipantEvent binding;CrossplayBootstrap::Context output;
    for(const auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}){
        output.map="old";Check(Capture(edition,binding,GetCurrentThreadId(),0x20000,expected,output)==Observation::Invalid && output.map.empty());
        Value value;Check(Snapshot(edition,UINT32_MAX,value)==Observation::Invalid && !value.choices.cache);
    }
    printf("PASS %u History host observation guards; no original host or reverse world started.\n",checks);
}
