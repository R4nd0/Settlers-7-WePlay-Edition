// Render-only distance presets shared by the guarded Gold and History operands.
#pragma once
#include <array>

namespace visual_distance {
struct Tuning {
    float lod, shadow_span, shadow_ratio, shadow_end, shadow_far_end;
};

constexpr Tuning AtScale(float scale) {
    const float end=12800.0f*scale;
    return {scale/300.0f,end-100.0f,end/100.0f,end,51200.0f*scale};
}

constexpr std::array<Tuning,4> Steps={AtScale(1.5f),AtScale(2.0f),AtScale(2.5f),AtScale(3.0f)};

inline bool ReadStep(const wchar_t* name, unsigned& step) {
    wchar_t value[3]{};
    SetLastError(ERROR_SUCCESS);
    const DWORD length=GetEnvironmentVariableW(name,value,3);
    if(!length&&GetLastError()==ERROR_ENVVAR_NOT_FOUND){step=0;return true;}
    if(length!=1||value[0]<L'0'||value[0]>L'4')return false;
    step=static_cast<unsigned>(value[0]-L'0');
    return true;
}
}
