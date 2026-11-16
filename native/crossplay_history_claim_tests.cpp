#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_history_claim.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool value){++checks;if(!value){std::fprintf(stderr,"History claim failed at %u\n",checks);std::exit(1);}}
CrossplayNative::LobbySnapshot Snapshot(){
    CrossplayNative::LobbySnapshot snapshot;snapshot.instance=0x123400;snapshot.local_slot=2;
    snapshot.members.push_back({{101,1,501,601,"Host",true,false},2});
    snapshot.members.push_back({{102,2,502,602,"Guest",false,true},2});
    return snapshot;
}
}
extern "C" __declspec(dllexport) int __cdecl ProductionHistoryClaimGuardOracle(unsigned edition){
    return CrossplayNative::HistoryClaimDetail::Methods(static_cast<Crossplay::Edition>(edition))?1:0;
}
int main(){
    using namespace CrossplayNative;
    using namespace HistoryClaimDetail;
    Pin pin;
    Check(!Layout(Crossplay::Edition::Gold,pin)&&!pin.manager_pointer);
    Check(!Layout(static_cast<Crossplay::Edition>(99),pin));
    Check(!Methods(Crossplay::Edition::Gold));
    for(auto edition:{Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}){
        Check(Layout(edition,pin)&&pin.manager_pointer&&pin.manager_type&&pin.castle_type&&pin.prepare&&pin.claim&&pin.set_id&&pin.set_name);
        auto snapshot=Snapshot();Participant participant;
        Check(ParticipantForClaim(snapshot,102,502,participant)&&participant.connection==102&&participant.principal==502&&participant.name=="Guest");
        for(uint32_t own:{0u,101u,103u})Check(!ParticipantForClaim(snapshot,own,502,participant)&&participant.name.empty());
        Check(!ParticipantForClaim(snapshot,102,0,participant));
        Check(!ParticipantForClaim(snapshot,102,501,participant));
        auto changed=snapshot;changed.instance=0;Check(!ParticipantForClaim(changed,102,502,participant));
        changed=snapshot;changed.host=true;Check(!ParticipantForClaim(changed,102,502,participant));
        changed=snapshot;changed.local_slot=1;Check(!ParticipantForClaim(changed,102,502,participant));
        changed=snapshot;changed.members.pop_back();Check(!ParticipantForClaim(changed,102,502,participant));
        changed=snapshot;changed.members.push_back(changed.members[0]);Check(!ParticipantForClaim(changed,102,502,participant));
        for(unsigned member:{0u,1u}){
            changed=snapshot;changed.members[member].status=1;Check(!ParticipantForClaim(changed,102,502,participant));
            changed=snapshot;changed.members[member].player.connection=UINT32_MAX;Check(!ParticipantForClaim(changed,102,502,participant));
            changed=snapshot;changed.members[member].player.principal=UINT32_MAX;Check(!ParticipantForClaim(changed,102,member?UINT32_MAX:502,participant));
            changed=snapshot;changed.members[member].player.name.clear();Check(!ParticipantForClaim(changed,102,502,participant));
            changed=snapshot;changed.members[member].player.name.assign(129,'x');Check(!ParticipantForClaim(changed,102,502,participant));
            changed=snapshot;changed.members[member].player.name=std::string("G\0uest",6);Check(!ParticipantForClaim(changed,102,502,participant));
            changed=snapshot;changed.members[member].player.slot=3;Check(!ParticipantForClaim(changed,102,502,participant));
            changed=snapshot;changed.members[member].player.host=!changed.members[member].player.host;Check(!ParticipantForClaim(changed,102,502,participant));
            changed=snapshot;changed.members[member].player.local=!changed.members[member].player.local;Check(!ParticipantForClaim(changed,102,502,participant));
        }
        changed=snapshot;changed.members[0].player.connection=102;Check(!ParticipantForClaim(changed,102,502,participant));
        // Original History sessions can own CID zero. Preserve that exact
        // routing identity instead of borrowing Gold's nonzero-principal rule.
        changed=snapshot;changed.members[1].player.connection=0;
        Check(ParticipantForClaim(changed,0,502,participant)&&participant.connection==0&&participant.slot==2);
        Check(!ParticipantForClaim(changed,102,502,participant));
        Check(!ParticipantForClaim(changed,UINT32_MAX,502,participant));
        Check(!ParticipantForClaim(changed,0,501,participant));
        changed.members[0].player.connection=0;Check(!ParticipantForClaim(changed,0,502,participant));
        changed=snapshot;changed.members[0].player.connection=0;
        Check(ParticipantForClaim(changed,102,502,participant)&&participant.connection==102);
        changed.members[1].player.connection=0;Check(!ParticipantForClaim(changed,0,502,participant));
        changed=snapshot;changed.members[0].player.principal=502;Check(!ParticipantForClaim(changed,102,502,participant));
        changed=snapshot;changed.members[1].player.principal=0;Check(ParticipantForClaim(changed,102,0,participant)&&participant.slot==2);
        changed.members[0].player.principal=2;Check(!ParticipantForClaim(changed,102,0,participant));
        changed=snapshot;changed.members[0].player.principal=0;Check(ParticipantForClaim(changed,102,502,participant));
        Interval a{0x10000,0x20000,0x30000,0x40000,0};Check(Same(a,a));
        for(auto field:{&Interval::session,&Interval::manager,&Interval::configuration,&Interval::rows}){
            auto b=a;b.*field=0;Check(!Same(a,b));Check(!Same(b,b));
            b=a;(b.*field)+=4;Check(!Same(a,b));
        }
        auto b=a;b.mode=1;Check(!Same(a,b));a.mode=1;Check(Same(a,b));
        HistoryGuestClaim claim;Check(!claim.Queued());
    }
    std::printf("PASS %u History claim identity and interval checks. Native methods not executed.\n",checks);
}
