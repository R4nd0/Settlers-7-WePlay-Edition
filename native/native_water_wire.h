#pragma once
#include "water_shader_effects.h"
#include "water_shader_config_palette.h"

// Private integration draft: typed transport only. The host must retain its
// admitted process/intent/dimension/legacy-overlap checks and post-Present queue.
namespace NativeWaterWire {
struct Pending {
    DWORD version=0;
    WaterShaderEffects::Config v1{};
    WaterShaderEffects::ConfigPalette v2{};
    DWORD Enabled() const noexcept{return version==1?v1.enabled:version==2?v2.enabled:0;}
    DWORD Width() const noexcept{return version==1?v1.render_width:version==2?v2.render_width:0;}
    DWORD Height() const noexcept{return version==1?v1.render_height:version==2?v2.render_height:0;}
};
static_assert(sizeof(WaterShaderEffects::Config)==1076,"Historical WaterProgram wire unchanged");
static_assert(sizeof(WaterShaderEffects::ConfigPalette)==3096,"Independent water palette wire");
inline bool Decode(const void* opaque,Pending* output) noexcept{
    if(!opaque||!output)return false;DWORD header[2]{};SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),opaque,header,sizeof(header),&read)||read!=sizeof(header))return false;
    Pending candidate{};
    if(header[0]==sizeof(candidate.v1)&&header[1]==1){
        read=0;if(!ReadProcessMemory(GetCurrentProcess(),opaque,&candidate.v1,sizeof(candidate.v1),&read)
            ||read!=sizeof(candidate.v1)||candidate.v1.size!=header[0]||candidate.v1.version!=1||candidate.v1.enabled>1)return false;
        candidate.version=1;
    }else if(header[0]==sizeof(candidate.v2)&&header[1]==2){
        read=0;if(!ReadProcessMemory(GetCurrentProcess(),opaque,&candidate.v2,sizeof(candidate.v2),&read)
            ||read!=sizeof(candidate.v2)||candidate.v2.size!=header[0]||candidate.v2.version!=2
            ||candidate.v2.enabled>1||candidate.v2.selected_variant>2)return false;
        candidate.version=2;
    }else return false;
    *output=candidate;return true;
}
}
