// SPDX-License-Identifier: MIT
#pragma once
#include "water_shader_config_palette.h"
#include <cstring>
#include <cwchar>
#include <string>
namespace WaterShaderPaletteContract {
inline constexpr const char* Digests[3]={
    "a8a591b346911622bfc9899fa28cbb022a42152df41ed822ef3cfff7c254e2f4",
    "0bf632f2862eb635c25131e575b4c2d5c35ba9fa73cb3aa01591f9afaecd543c",
    "5d027025eb77c23d902c8ccdbc4cddde70064f387e0c34bd4039ee1cf52c6c70"};
inline constexpr const wchar_t* Recipes[3]={L"river-depth-fresnel-v2-balanced",L"river-palette-clear-bank-v3",L"river-palette-calm-gloss-v4"};
inline bool Validate(const WaterShaderEffects::ConfigPalette& c)noexcept{
    try{
    if(c.size!=3096||c.version!=2||c.enabled>1||c.selected_variant>2||c.render_width<800||c.render_height<600||c.render_width>3840||c.render_height>2160)return false;
    for(unsigned i=0;i<3;++i){const auto length=wcsnlen_s(c.program_paths[i],512);if(length<3||length>=512)return false;
        for(size_t j=length;j<512;++j)if(c.program_paths[i][j])return false;
        const auto* p=c.program_paths[i];if(!((p[0]>=L'A'&&p[0]<=L'Z')||(p[0]>=L'a'&&p[0]<=L'z'))||p[1]!=L':'||p[2]!=L'\\')return false;
        for(size_t j=3;j<length;++j)if(p[j]==L':'||p[j]==L'/'||p[j]<32)return false;
        wchar_t full[512]{};const DWORD count=GetFullPathNameW(p,512,full,nullptr);if(!count||count>=512||_wcsicmp(full,p))return false;
        std::wstring ending=L"\\MaterialShaders\\";ending+=Recipes[i];ending+=L"\\";
        for(unsigned j=0;j<64;++j)ending+=static_cast<wchar_t>(Digests[i][j]);ending+=L".ps.bin";
        if(length<=ending.size()||_wcsicmp(p+length-ending.size(),ending.c_str()))return false;
    }
    return true;
    }catch(...){return false;}
}
inline bool SamePinnedConfig(const WaterShaderEffects::ConfigPalette&a,const WaterShaderEffects::ConfigPalette&b)noexcept{
    return a.size==3096&&b.size==3096&&a.version==2&&b.version==2&&!std::memcmp(&a,&b,16)
        &&!std::memcmp(reinterpret_cast<const unsigned char*>(&a)+24,reinterpret_cast<const unsigned char*>(&b)+24,sizeof(a)-24);
}
}
