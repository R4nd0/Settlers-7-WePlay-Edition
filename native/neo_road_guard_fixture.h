#pragma once

// Seed the exact guarded instructions in the tests' synthetic image.
inline void SetUpNeoRoadQueryGuards() {
    auto* sector=Native<unsigned char*>(0x9a0cd0);
    memcpy(sector,NeoRoadSectorEntryGuard,sizeof(NeoRoadSectorEntryGuard));
    memcpy(sector+0x38,NeoRoadSectorGridGuard,sizeof(NeoRoadSectorGridGuard));
    memcpy(sector+0x68,NeoRoadSectorCallbackGuard,sizeof(NeoRoadSectorCallbackGuard));
    memcpy(Native<void*>(0x6ff010),steam_history?NeoRoadAccessSteamGuard:NeoRoadAccessUbiGuard,
        sizeof(NeoRoadAccessUbiGuard));
}
