#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_gold_guest_ready.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;void Check(bool value){++checks;if(!value){printf("FAIL Gold guest Ready check %u\n",checks);std::exit(1);}}
}
extern "C" __declspec(dllexport) uint32_t __cdecl ProductionGoldGuestReadyGuardOracle(uint32_t edition){
    return edition==1 && CrossplayGoldGuestReady::Methods()?1u:0u;
}
int main(){
    using namespace CrossplayGoldGuestReady;
    for(uint32_t slot=0;slot<4;++slot)for(uint32_t source:{0u,11u,UINT32_MAX})for(unsigned value=0;value<3;++value){
        std::vector<unsigned char> wire(1,0xff);const bool expected=slot>=1 && slot<=2 && source && source!=UINT32_MAX && value<=1;
        Check(StateWire(slot,source,value,wire)==expected);
        if(!expected){Check(wire.empty());continue;}
        Check(wire.size()==18 && CrossplayChat::Word(wire.data())==CrossplayReady::Class
            && CrossplayChat::Word(wire.data()+4)==CrossplayReady::Type && CrossplayChat::Word(wire.data()+8)==source
            && wire[12]==1 && CrossplayChat::Word(wire.data()+13)==slot && wire[17]==value);
        Check(CrossplayReady::Validate(std::vector<unsigned char>(wire.begin(),wire.begin()+13),source));
    }
    CrossplayNative::ParticipantEvent binding;CrossplayNative::LobbyAdmission admission;
    CrossplayNative::GoldGuestRowClaim claim;CrossplayNative::GoldGuestIdentity identity;
    std::vector<unsigned char> intent(1,1);unsigned char requested=1;
    SetLastError(1965);Check(!CaptureIntent(binding,admission,claim,identity,{11,22},nullptr,intent,requested));
    Check(intent.empty() && !requested && GetLastError()==1965);
    Pending pending;Check(!Apply(binding,admission,claim,identity,{11,22},{1,1,1},pending));Check(!pending.owner && GetLastError()==1965);
    Check(Observe(binding,admission,claim,identity,{11,22},pending)==CrossplayNativeCodec::ReadyObservation::Invalid);
    Check(GetLastError()==1965);
    printf("PASS %u Gold guest Ready slot/wire/owner/scope checks; authored only, no native Ready or world qualified.\n",checks);return 0;
}
