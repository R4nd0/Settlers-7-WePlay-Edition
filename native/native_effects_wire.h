#pragma once
#include "material_shader_config_v2.h"

// Transport decoding only. The integrating host still validates feature intent,
// dimensions, paths and pinned resources before queueing a render-thread change.
namespace NativeEffectsWire {
struct Pending {
    DWORD version=0;
    MaterialShaderEffects::Config v1{};
    MaterialShaderConfigV2::Config v2{};
    DWORD Flags() const noexcept {return version==1?v1.flags:version==2?v2.flags:0;}
};

inline bool Decode(const void* opaque,Pending* output) noexcept {
    if(!opaque||!output)return false;
    DWORD header[2]{};SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),opaque,header,sizeof(header),&read)||read!=sizeof(header))return false;
    Pending candidate{};
    if(header[0]==sizeof(candidate.v1)&&header[1]==1) {
        read=0;
        if(!ReadProcessMemory(GetCurrentProcess(),opaque,&candidate.v1,sizeof(candidate.v1),&read)
            ||read!=sizeof(candidate.v1)||candidate.v1.size!=header[0]||candidate.v1.version!=header[1])return false;
        candidate.version=1;
    } else if(header[0]==sizeof(candidate.v2)&&header[1]==2) {
        read=0;
        if(!ReadProcessMemory(GetCurrentProcess(),opaque,&candidate.v2,sizeof(candidate.v2),&read)
            ||read!=sizeof(candidate.v2)||candidate.v2.size!=header[0]||candidate.v2.version!=header[1])return false;
        candidate.version=2;
    } else return false;
    *output=candidate;return true;
}
}
