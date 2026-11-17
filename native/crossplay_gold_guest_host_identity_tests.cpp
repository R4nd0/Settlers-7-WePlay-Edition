#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_gold_guest_host_identity.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool value){++checks;if(!value){printf("FAIL Gold guest host identity %u\n",checks);std::exit(1);}}
}
extern "C" __declspec(dllexport) int __cdecl ProductionGoldGuestHostIdentityGuardOracle(){return CrossplayNative::GoldGuestIdentityDetail::Methods()?1:0;}
int main(){
    using namespace CrossplayNative;using namespace GoldGuestHostIdentityDetail;
    std::vector<unsigned char> rows(25*0x5c4,0);auto before=rows;
    for(size_t offset:{size_t(0x5c4+0x18),size_t(0x5c4+0x1c),size_t(0x5c4+0x2b)}){rows[offset]^=1;Check(Preserved(before.data(),rows.data(),rows.size()));}
    for(size_t offset:{size_t(0x18),size_t(2*0x5c4+0x18),size_t(0x5c4+0x2c),size_t(0x5c4+0x5c),size_t(0x5c4+0x60),size_t(0x5c4+0x64),size_t(0x5c4+0x68),size_t(0x5c4+0x71),size_t(0x5c4+0x574)}){
        rows[offset]^=1;Check(!Preserved(before.data(),rows.data(),rows.size()));rows[offset]^=1;
    }
    Check(!Preserved(nullptr,rows.data(),rows.size()));Check(!Preserved(before.data(),rows.data(),rows.size()-1));
    Participant p;p.principal=0;Check(Identity(p)==1);p.principal=72;Check(Identity(p)==72);
    LobbySnapshot lobby;Check(Host(lobby)==nullptr);p.slot=1;p.host=true;p.local=false;lobby.members={{p,2}};Check(Host(lobby)!=nullptr);
    lobby.members[0].player.local=true;Check(Host(lobby)==nullptr);
    ParticipantEvent binding;LobbyAdmission admission;GoldGuestRowClaim claim;GoldGuestIdentity own;GoldGuestHostIdentity identity;
    SetLastError(1980);Check(!identity.Begin(binding,admission,claim,own));Check(!identity.Applied(binding,admission,claim,own));Check(GetLastError()==1980);
    Participant remote;remote.name="stale";CrossplayCastleSetup::Value castle;castle.serialized_type="stale";uint32_t extra=77;
    Check(!admission.CapturePeerSetup(remote,castle,extra));Check(remote.name.empty() && castle.serialized_type.empty() && extra==0);Check(GetLastError()==1980);
    printf("PASS %u Gold guest host-identity scope/preservation and bound peer-source checks; authored only, no native peer or world admitted.\n",checks);return 0;
}
