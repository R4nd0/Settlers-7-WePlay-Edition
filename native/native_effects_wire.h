#pragma once
#include "material_shader_config_v2.h"
#include "material_shader_config_v3.h"
#include "material_shader_config_v4.h"
#include "material_shader_config_v5.h"

// Transport decoding only. The integrating host still validates feature intent,
// dimensions, paths and pinned resources before queueing a render-thread change.
namespace NativeEffectsWire {
struct Pending {
    DWORD version=0;
    MaterialShaderEffects::Config v1{};
    MaterialShaderConfigV2::Config v2{};
    MaterialShaderConfigV3::Config v3{};
    MaterialShaderConfigV4::Config v4{};
    MaterialShaderConfigV5::Config v5{};
    DWORD Flags() const noexcept {return version==1?v1.flags:version==2?v2.flags:version==3?v3.flags:version==4?v4.flags:version==5?v5.flags:0;}
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
    } else if(header[0]==sizeof(candidate.v3)&&header[1]==3) {
        read=0;
        if(!ReadProcessMemory(GetCurrentProcess(),opaque,&candidate.v3,sizeof(candidate.v3),&read)
            ||read!=sizeof(candidate.v3)||candidate.v3.size!=header[0]||candidate.v3.version!=header[1])return false;
        candidate.version=3;
    } else if(header[0]==sizeof(candidate.v4)&&header[1]==4) {
        read=0;
        if(!ReadProcessMemory(GetCurrentProcess(),opaque,&candidate.v4,sizeof(candidate.v4),&read)
            ||read!=sizeof(candidate.v4)||candidate.v4.size!=header[0]||candidate.v4.version!=header[1])return false;
        candidate.version=4;
    } else if(header[0]==sizeof(candidate.v5)&&header[1]==5) {
        read=0;
        if(!ReadProcessMemory(GetCurrentProcess(),opaque,&candidate.v5,sizeof(candidate.v5),&read)
            ||read!=sizeof(candidate.v5)||candidate.v5.size!=header[0]||candidate.v5.version!=header[1])return false;
        candidate.version=5;
    } else return false;
    *output=candidate;return true;
}
}
