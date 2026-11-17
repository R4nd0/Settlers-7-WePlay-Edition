#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_peer_castle_preparation.h"
#include <cstdio>
#include <cstdlib>
namespace {
unsigned checks=0;
void Check(bool value){++checks;if(!value){printf("FAIL peer castle preparation %u\n",checks);std::exit(1);}}
}
int main(){
    using namespace CrossplayNative;using namespace PeerCastlePreparationDetail;
    View a{1,2,0,1,0x5c4,0x574};Check(Same(a,a));
    auto b=a;b.session=0;Check(!Same(a,b));b=a;++b.rows;Check(!Same(a,b));b=a;++b.mode;Check(!Same(a,b));
    b=a;++b.slot;Check(!Same(a,b));b=a;++b.stride;Check(!Same(a,b));b=a;++b.castle_at;Check(!Same(a,b));
    Participant p;p.connection=3;p.principal=4;p.station=5;p.slot=1;p.host=true;p.name="ActualPeer";Check(SamePeer(p,p));
    auto q=p;++q.connection;Check(!SamePeer(p,q));q=p;++q.principal;Check(!SamePeer(p,q));q=p;++q.station;Check(!SamePeer(p,q));q=p;q.name="Other";Check(!SamePeer(p,q));
    ParticipantEvent binding;LobbyAdmission admission;View view;Participant peer;PeerCastlePreparation receipt;
    SetLastError(1981);Check(!Context(Crossplay::Edition::UbisoftHistory,binding,admission,view,peer));
    Check(!receipt.Begin(Crossplay::Edition::Gold,binding,admission));Check(!receipt.Begin(Crossplay::Edition::Gold,binding,admission));
    Check(!receipt.Consumed(nullptr,nullptr));Check(receipt.Observe(binding,admission)==PeerCastleObservation::Invalid);Check(GetLastError()==1981);
    printf("PASS %u peer castle source/owned interval/one-shot/exclusion checks; authored only, no original consumer or world qualified.\n",checks);return 0;
}
