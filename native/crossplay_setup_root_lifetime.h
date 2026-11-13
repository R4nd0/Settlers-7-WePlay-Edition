#pragma once
#include "crossplay_setup_root_capture.h"

// Original native ownership helpers. Call only after the current owner/thread
// and complete method guards have passed; no active root is replaced.
// Whole original-image fingerprints are checked before generating these bounded method pins.
namespace CrossplaySetupRootLifetime::Detail {
struct Region {uintptr_t at;size_t size;uint64_t hash;};
constexpr uintptr_t GoldConstructor=0x9cdfe0;
constexpr uintptr_t GoldDestructor=0x9ce110;
constexpr Region GoldRegions[]={
    {0x9cdfe0,0x130,0xf9cd3e1e61de7e57ull},
    {0x9ce110,0x130,0x978b600f37324554ull},
    {0x5203c0,0x140,0xeef280ff072a872bull},
    {0x9ce260,0xc0,0x93b61b633986b4bcull},
    {0x51f8c0,0x3d7,0x30f0a4ae07b4d501ull},
    {0x520500,0x114,0x15c6ecd8dccb9e1full},
};
constexpr uintptr_t UbisoftConstructor=0x5f4f20;
constexpr uintptr_t UbisoftDestructor=0x5f5040;
constexpr Region UbisoftRegions[]={
    {0x5f4f20,0x120,0x6956a2c1b3ac6416ull},
    {0x5f5040,0x140,0x674881e1942de750ull},
    {0x58e5d0,0x1b0,0xc637c7b0b9217ab1ull},
    {0x5f5180,0xc0,0xd41ae70e7fdcac2full},
    {0x58da50,0x332,0xdfbd1a7df04214dfull},
    {0x58e780,0x18b,0xa75ac97176a0638cull},
};
constexpr uintptr_t SteamConstructor=0x5f4a80;
constexpr uintptr_t SteamDestructor=0x5f4ba0;
constexpr Region SteamRegions[]={
    {0x5f4a80,0x117,0x004e442974507221ull},
    {0x5f4ba0,0x136,0xa02a93003ca755feull},
    {0x58e090,0x1a8,0x367ff61a58cec403ull},
    {0x5f4ce0,0xb5,0xb3a2903ff24250b3ull},
    {0x58d510,0x332,0xb062a8d0ebf1be09ull},
    {0x58e240,0x18b,0xc4aefd496304a81cull},
};
}

namespace CrossplaySetupRootLifetime::Detail {
struct Result {bool constructed=false,separate_rows=false,configuration_owned=false,destroyed=false,canaries=false;DWORD exception=0;};
inline bool Methods(Crossplay::Edition edition) {
    const Region* regions=nullptr;size_t count=0;
    if(edition==Crossplay::Edition::Gold){regions=GoldRegions;count=sizeof(GoldRegions)/sizeof(*GoldRegions);}
    else if(edition==Crossplay::Edition::UbisoftHistory){regions=UbisoftRegions;count=sizeof(UbisoftRegions)/sizeof(*UbisoftRegions);}
    else if(edition==Crossplay::Edition::SteamHistory){regions=SteamRegions;count=sizeof(SteamRegions)/sizeof(*SteamRegions); }
    else return false;
    for(size_t n=0;n<count;++n) {
        const auto& p=regions[n];const auto* data=reinterpret_cast<const unsigned char*>(p.at);
        if(!CrossplayNativeCodec::Detail::Readable(data,p.size))return false;
        uint64_t hash=14695981039346656037ull;
        for(size_t i=0;i<p.size;++i)hash=(hash^data[i])*1099511628211ull;
        if(hash!=p.hash)return false;
    }
    return true;
}
inline bool Construct(Crossplay::Edition edition,void* target,DWORD& exception) {
    __try {
        if(edition==Crossplay::Edition::Gold)reinterpret_cast<void* (__stdcall*)(void*)>(GoldConstructor)(target);
        else if(edition==Crossplay::Edition::UbisoftHistory)reinterpret_cast<void* (__thiscall*)(void*)>(UbisoftConstructor)(target);
        else if(edition==Crossplay::Edition::SteamHistory)reinterpret_cast<void* (__thiscall*)(void*)>(SteamConstructor)(target);
        else return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {exception=GetExceptionCode();return false;}
}
inline bool Destroy(Crossplay::Edition edition,void* target,DWORD& exception) {
    __try {
        if(edition==Crossplay::Edition::Gold)reinterpret_cast<void (__stdcall*)(void*)>(GoldDestructor)(target);
        else if(edition==Crossplay::Edition::UbisoftHistory)reinterpret_cast<void (__thiscall*)(void*)>(UbisoftDestructor)(target);
        else if(edition==Crossplay::Edition::SteamHistory)reinterpret_cast<void (__thiscall*)(void*)>(SteamDestructor)(target);
        else return false;
        return true;
    } __except(EXCEPTION_EXECUTE_HANDLER) {exception=GetExceptionCode();return false;}
}
}
