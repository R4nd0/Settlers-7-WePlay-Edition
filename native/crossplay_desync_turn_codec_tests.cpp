#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_desync_turn_codec.h"
#include <stdio.h>
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayDesyncTurnMethodsOracle(uint32_t edition) {
    return CrossplayNativeCodec::DesyncTurnDetail::Methods(static_cast<Crossplay::Edition>(edition))?1u:0u;
}
int main() {
    using namespace CrossplayNativeCodec::DesyncTurnDetail;
    for(auto invalid:{0u,4u,UINT32_MAX})if(WePlayDesyncTurnMethodsOracle(invalid))return 1;
    CrossplayNative::ParticipantEvent unbound;
    for(auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}) {
        std::vector<unsigned char> wire{1};
        if(Encode(edition,unbound,0,0,42,0,nullptr,wire) || !wire.empty() || Enqueue(edition,unbound,0,0,42,0,wire))return 2;
    }
    puts("PASS unsupported/unowned native desync-turn calls rejected. No factory, sender or game executed.");return 0;
}
