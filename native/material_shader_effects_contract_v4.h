// SPDX-License-Identifier: MIT
#pragma once
#include "material_shader_config_v4.h"
#include "material_shader_effects_contract_v2.h"
namespace NativeEffectsContractV4 {
using Config=MaterialShaderConfigV4::Config;
using Error=NativeEffectsContractV2::Error;using Layout=NativeEffectsContractV2::Layout;
inline constexpr const wchar_t* Recipes[3]={L"leaf-palette-A-v5",L"leaf-palette-B-v6",L"leaf-palette-C-v7"};
inline constexpr const wchar_t* Digests[3]={
    L"1b825358f46c39b04b1d540738f4be5e862bfe6627b8545ab092c471d259477c",
    L"284d5c84ee6700039ee818fe7f063b2b90536beac45e261a5b3e0cf8a6d1746a",
    L"b869fe375b76509f60a48d34bde83565d28c0416aca85f27c8a445045cb6a51f"};
inline bool SamePinnedConfig(const Config&a,const Config&b)noexcept{
    return a.size==4120&&a.version==4&&b.size==4120&&b.version==4
        &&!std::memcmp(&a,&b,8)
        &&!std::memcmp(reinterpret_cast<const unsigned char*>(&a)+12,reinterpret_cast<const unsigned char*>(&b)+12,8)
        &&!std::memcmp(reinterpret_cast<const unsigned char*>(&a)+24,reinterpret_cast<const unsigned char*>(&b)+24,sizeof(Config)-24);
}
inline Error Validate(const Config& e,const NativeGraphicsConfig::Version2& n,Layout layout,bool inspect_files=true)noexcept{
    NativeEffectsContractV2::LastErrorScope saved;try{
        using namespace NativeEffectsContractV2;
        if(e.size!=4120||e.version!=4||n.base.size!=sizeof(n)||n.base.version!=2)return Error::Header;
        if(Requested(n.flags)!=MaterialShaderEffects::Leaf||(n.flags&~NativeGraphicsConfig::KnownFlags)||(e.flags&~MaterialShaderEffects::Leaf)||e.selected_variant>2)return Error::Flags;
        const auto& b=n.base;
        if(b.output_width<320||b.output_height<180||b.render_width>3840||b.render_height>2160||b.render_height<200
            ||b.render_width<b.output_width||b.render_height<b.output_height
            ||static_cast<std::uint64_t>(b.render_width)*b.output_height!=static_cast<std::uint64_t>(b.render_height)*b.output_width
            ||e.render_width!=b.render_width||e.render_height!=b.render_height)return Error::Dimensions;
        std::wstring native_report,report;if(!Canonical(b.report_path,L".json",native_report)||!Canonical(e.report_path,L".private.json",report)||report.size()>=508)return Error::Path;
        const auto slash=native_report.find_last_of(L'\\');if(slash==std::wstring::npos)return Error::Path;const auto root=native_report.substr(0,slash);
        if(!Within(report,root))return Error::OutsideRoot;
        if(_wcsicmp(report.c_str(),(root+L"\\material-effects.private.json").c_str()))return Error::FixedPath;
        if(!_wcsicmp(report.c_str(),native_report.c_str()))return Error::ReportCollision;
        if(n.observer_report_path[0]){std::wstring observer;if(!Canonical(n.observer_report_path,L".json",observer))return Error::Path;
            if(!_wcsicmp(report.c_str(),observer.c_str()))return Error::ReportCollision;}
        std::wstring artifacts;
        if(layout==Layout::PrivateCase)artifacts=root+L"\\LocalData\\MaterialShaders\\";
        else if(layout==Layout::RecipientLocalData){const auto last=root.find_last_of(L'\\');if(last==std::wstring::npos||_wcsicmp(root.c_str()+last+1,L"LocalData"))return Error::Path;artifacts=root+L"\\MaterialShaders\\";}
        else return Error::Path;
        for(unsigned i=0;i<3;++i){std::wstring full;if(!Canonical(e.program_paths[i],L".ps.bin",full))return Error::Path;
            if(!Within(full,root))return Error::OutsideRoot;const auto expected=artifacts+Recipes[i]+L"\\"+Digests[i]+L".ps.bin";
            if(_wcsicmp(full.c_str(),expected.c_str()))return Error::FixedPath;
            if(inspect_files){const auto checked=Preflight(full,false);if(checked!=Error::None)return checked;}}
        if(inspect_files){const auto a=Preflight(native_report,true);if(a!=Error::None)return a;const auto c=Preflight(report,true);if(c!=Error::None)return c;}
        return Error::None;
    }catch(...){return Error::Internal;}
}
inline Error PrepareInitial(const Config&request,const NativeGraphicsConfig::Version2&native,Layout layout,Config&output,bool inspect_files=true)noexcept{
    const auto error=Validate(request,native,layout,inspect_files);if(error!=Error::None)return error;
    output=request;output.flags=MaterialShaderEffects::Leaf;return Error::None;
}
}
