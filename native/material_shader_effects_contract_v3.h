// SPDX-License-Identifier: MIT
// Isolated recipient-generated Leaf PS profile; old V1/V2 contracts unchanged.
#pragma once
#include "material_shader_config_v3.h"
#include "material_shader_effects_contract_v2.h"
#include <utility>

namespace NativeEffectsContractV3 {
using Config=MaterialShaderConfigV3::Config;
using Error=NativeEffectsContractV2::Error;
using Layout=NativeEffectsContractV2::Layout;
inline constexpr wchar_t Recipe[]=L"leaf-wrapped-diffuse-v4";
inline constexpr wchar_t Digest[]=L"284d5c84ee6700039ee818fe7f063b2b90536beac45e261a5b3e0cf8a6d1746a";
inline bool SamePinnedConfig(const Config& a,const Config& b) noexcept {
    constexpr size_t offset=offsetof(Config,flags),after=offset+sizeof(DWORD);
    return a.size==7204&&a.version==3&&b.size==7204&&b.version==3&&!std::memcmp(&a,&b,offset)
        &&!std::memcmp(reinterpret_cast<const unsigned char*>(&a)+after,reinterpret_cast<const unsigned char*>(&b)+after,sizeof(Config)-after);
}
inline Error Validate(const Config& effects,const NativeGraphicsConfig::Version2& native,Layout layout,bool inspect_files=true) noexcept {
    NativeEffectsContractV2::LastErrorScope saved;
    try {
        using namespace NativeEffectsContractV2;
        if(effects.size!=7204||effects.version!=3||native.base.size!=sizeof(native)||native.base.version!=2)return Error::Header;
        if(Requested(native.flags)!=MaterialShaderEffects::Leaf||(native.flags&~NativeGraphicsConfig::KnownFlags)
            ||(effects.flags&~MaterialShaderEffects::Leaf)||!MaterialShaderConfigV3::UnusedPaths(effects))return Error::Flags;
        const auto& base=native.base;
        if(base.output_width<320||base.output_height<180||base.render_height<200||base.render_width>3840||base.render_height>2160
            ||base.render_width<base.output_width||base.render_height<base.output_height
            ||static_cast<std::uint64_t>(base.render_width)*base.output_height!=static_cast<std::uint64_t>(base.render_height)*base.output_width
            ||effects.render_width!=base.render_width||effects.render_height!=base.render_height)return Error::Dimensions;
        if(!MaterialShaderConfigV3::Parameters(effects))return Error::Parameters;
        std::wstring native_report,report,program;
        if(!Canonical(base.report_path,L".json",native_report)||!Canonical(effects.report_path,L".private.json",report)||report.size()>=508
            ||!Canonical(effects.leaf_pixel_path,L".ps.bin",program))return Error::Path;
        const auto slash=native_report.find_last_of(L'\\');if(slash==std::wstring::npos)return Error::Path;
        const auto root=native_report.substr(0,slash);
        if(!Within(report,root)||!Within(program,root))return Error::OutsideRoot;
        if(_wcsicmp(report.c_str(),(root+L"\\material-effects.private.json").c_str()))return Error::FixedPath;
        if(!_wcsicmp(report.c_str(),native_report.c_str()))return Error::ReportCollision;
        if(native.observer_report_path[0]){std::wstring observer;if(!Canonical(native.observer_report_path,L".json",observer))return Error::Path;
            if(!_wcsicmp(report.c_str(),observer.c_str()))return Error::ReportCollision;}
        std::wstring artifacts;
        if(layout==Layout::PrivateCase)artifacts=root+L"\\LocalData\\MaterialShaders\\";
        else if(layout==Layout::RecipientLocalData){const auto last=root.find_last_of(L'\\');
            if(last==std::wstring::npos||_wcsicmp(root.c_str()+last+1,L"LocalData"))return Error::Path;
            artifacts=root+L"\\MaterialShaders\\";}
        else return Error::Path;
        const std::wstring expected=artifacts+Recipe+L"\\"+Digest+L".ps.bin";
        if(_wcsicmp(program.c_str(),expected.c_str()))return Error::FixedPath;
        if(inspect_files){for(const auto& entry:std::array<std::pair<std::wstring,bool>,3>{{{program,false},{native_report,true},{report,true}}}){
            const auto checked=Preflight(entry.first,entry.second);if(checked!=Error::None)return checked;}}
        return Error::None;
    }catch(...){return Error::Internal;}
}
inline Error PrepareInitial(const Config& request,const NativeGraphicsConfig::Version2& native,Layout layout,Config& output,bool inspect_files=true) noexcept {
    const auto error=Validate(request,native,layout,inspect_files);if(error!=Error::None)return error;
    output=request;output.flags=MaterialShaderEffects::Leaf;return Error::None;
}
}
