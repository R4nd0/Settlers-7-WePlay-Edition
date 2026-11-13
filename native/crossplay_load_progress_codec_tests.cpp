#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include "crossplay_load_progress_codec.h"
#include <stdio.h>
#include <string.h>
extern "C" __declspec(dllexport) uint32_t __cdecl WePlayLoadProgressMethodsOracle(uint32_t edition) {
    return CrossplayNativeCodec::LoadProgressDetail::Methods(static_cast<Crossplay::Edition>(edition))?1u:0u;
}
int main() {
    using namespace CrossplayNativeCodec::LoadProgressDetail;
    for(auto invalid:{0u,4u,UINT32_MAX})if(WePlayLoadProgressMethodsOracle(invalid))return 1;
    CrossplayNative::ParticipantEvent unbound;
    CrossplayNative::LobbySnapshot snapshot;
    if(unbound.CaptureLobby(snapshot) || unbound.CaptureStartedSession(snapshot) || snapshot.instance || !snapshot.members.empty())return 4;
    for(auto edition:{Crossplay::Edition::Gold,Crossplay::Edition::UbisoftHistory,Crossplay::Edition::SteamHistory}) {
        std::vector<unsigned char> wire{1};
        if(Encode(edition,unbound,0,0,42,0,nullptr,wire) || !wire.empty() || Enqueue(edition,unbound,0,0,42,0,wire))return 2;
    }
    alignas(16) unsigned char before[512],after[512];uint32_t changed=0x3f80;
    __asm {fxsave before}
    {
        FpScope preserve;
        __asm {fninit
               fldz
               pxor xmm0,xmm0
               pcmpeqd xmm1,xmm1
               ldmxcsr changed}
    }
    __asm {fxsave after}
    if(memcmp(before,after,6) || memcmp(before+24,after+24,8) || memcmp(before+32,after+32,256))return 3;
    puts("PASS unsupported/unowned native load calls rejected and x87/SSE/MXCSR preserved. No game executed.");return 0;
}
