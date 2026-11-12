#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include "crossplay_session_map_observer.h"
#include <cstdio>

// These early exclusions must not touch the deliberately unmapped addresses.
// Positive cache transitions are separately verified in real isolated clients.
int main() {
    using namespace CrossplayNativeCodec;
    CrossplayNative::ParticipantEvent binding;
    PendingHistorySessionMap pending;
    if(ObserveHistorySessionMap(binding,pending)!=ReadyObservation::Invalid)return 1;
    pending={Crossplay::Edition::UbisoftHistory,{1,2},1,1,GetCurrentThreadId(),GetTickCount64(),"Maps/Test"};
    const auto valid_metadata=pending;
    pending.owner=0;
    if(ObserveHistorySessionMap(binding,pending)!=ReadyObservation::Invalid)return 2;
    pending=valid_metadata;pending.started=GetTickCount64()-10001;
    if(ObserveHistorySessionMap(binding,pending)!=ReadyObservation::Invalid)return 3;
    pending=valid_metadata;pending.edition=Crossplay::Edition::Gold;
    if(ObserveHistorySessionMap(binding,pending)!=ReadyObservation::Invalid)return 4;
    pending=valid_metadata;pending.expected_map="../Other";
    if(ObserveHistorySessionMap(binding,pending)!=ReadyObservation::Invalid)return 5;
    const std::vector<unsigned char> empty;
    pending=valid_metadata;
    if(EnqueueHistorySessionMap(Crossplay::Edition::Gold,binding,{1,2},empty,pending)
        || pending.session || pending.cache || pending.owner || !pending.expected_map.empty())return 6;
    std::puts("PASS session-map observer excludes missing ownership, foreign thread, expired interval, wrong edition and invalid map before native reads.");
}
