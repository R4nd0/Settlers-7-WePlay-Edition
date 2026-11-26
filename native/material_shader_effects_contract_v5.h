// SPDX-License-Identifier: MIT
#pragma once
#include "material_shader_config_v5.h"
#include "material_shader_effects_contract_v2.h"
#include "material_shader_effects_contract_v4.h"
namespace NativeEffectsContractV5 {
using Config=MaterialShaderConfigV5::Config;using Layout=MaterialShaderConfigV5::Layout;using Error=NativeEffectsContractV2::Error;
using Program=NativeEffectsContractV2::Program;
inline constexpr std::array<Program,7> Programs{{
    {NativeEffectsContractV4::Recipes[0],NativeEffectsContractV4::Digests[0],L".ps.bin",MaterialShaderEffects::Leaf},
    NativeEffectsContractV2::MacroPrograms[1],NativeEffectsContractV2::MacroPrograms[2],
    NativeEffectsContractV2::MacroPrograms[3],NativeEffectsContractV2::MacroPrograms[4],
    {NativeEffectsContractV4::Recipes[1],NativeEffectsContractV4::Digests[1],L".ps.bin",MaterialShaderEffects::Leaf},
    {NativeEffectsContractV4::Recipes[2],NativeEffectsContractV4::Digests[2],L".ps.bin",MaterialShaderEffects::Leaf}
}};
inline constexpr std::array<DWORD,7> ProgramBytes{{1372,1836,3596,2020,2020,1372,1372}};
struct Resolved {wchar_t program_paths[7][512]{},report_path[512]{};};
inline bool SamePinnedConfig(const Config&a,const Config&b)noexcept{
    return a.size==24&&a.version==5&&b.size==24&&b.version==5&&!std::memcmp(&a,&b,8)
        &&!std::memcmp(reinterpret_cast<const unsigned char*>(&a)+12,reinterpret_cast<const unsigned char*>(&b)+12,8);
}
// Root/layout are supplied by the already admitted host, never the24B payload.
inline Error Resolve(const wchar_t* trusted_native_report,Layout layout,Resolved&output,bool inspect_files=true)noexcept{
    NativeEffectsContractV2::LastErrorScope saved;try{
        using namespace NativeEffectsContractV2;
        const auto length=trusted_native_report?wcsnlen_s(trusted_native_report,512):0;if(!length||length>=512)return Error::Path;
        wchar_t input[512]{};std::memcpy(input,trusted_native_report,length*sizeof(wchar_t));std::wstring native;
        if(!Canonical(input,L".json",native))return Error::Path;const auto slash=native.find_last_of(L'\\');if(slash==std::wstring::npos)return Error::Path;
        const auto root=native.substr(0,slash);std::wstring artifacts;
        if(layout==MaterialShaderConfigV5::Layout::PrivateCase)artifacts=root+L"\\LocalData\\MaterialShaders\\";
        else if(layout==MaterialShaderConfigV5::Layout::RecipientLocalData){const auto parent=root.find_last_of(L'\\');
            if(parent==std::wstring::npos||_wcsicmp(root.c_str()+parent+1,L"LocalData"))return Error::Path;
            artifacts=root+L"\\MaterialShaders\\";}
        else return Error::Path;
        Resolved resolved{};const auto report=root+L"\\material-effects.private.json";
        if(report.size()>=508||!_wcsicmp(report.c_str(),native.c_str()))return Error::ReportCollision;
        wcscpy_s(resolved.report_path,report.c_str());std::wstring checked;
        if(!Canonical(resolved.report_path,L".private.json",checked))return Error::Path;
        if(inspect_files){const auto a=Preflight(native,true),b=Preflight(report,true);if(a!=Error::None)return a;if(b!=Error::None)return b;}
        for(unsigned i=0;i<7;++i){const auto path=artifacts+Programs[i].directory+L"\\"+Programs[i].digest+Programs[i].suffix;
            if(path.size()>=512)return Error::Path;wcscpy_s(resolved.program_paths[i],path.c_str());
            if(!Canonical(resolved.program_paths[i],Programs[i].suffix,checked)||!Within(checked,root))return Error::Path;
            if(inspect_files){const auto error=Preflight(checked,false);if(error!=Error::None)return error;}}
        output=resolved;return Error::None;
    }catch(...){return Error::Internal;}
}
inline Error Validate(const Config&c,const NativeGraphicsConfig::Version2&n,Layout layout,bool inspect_files=true)noexcept{
    NativeEffectsContractV2::LastErrorScope saved;
    if(c.size!=24||c.version!=5||n.base.size!=sizeof(n)||n.base.version!=2)return Error::Header;
    if((c.flags&~MaterialShaderConfigV5::Allowed)||c.selected_variant>2
        ||NativeEffectsContractV2::Requested(n.flags)!=MaterialShaderConfigV5::Allowed||(n.flags&~NativeGraphicsConfig::KnownFlags))return Error::Flags;
    const auto&b=n.base;
    if(!MaterialShaderConfigV5::Validate(c)||c.render_width!=b.render_width||c.render_height!=b.render_height
        ||b.output_width<320||b.output_height<180||b.render_width>3840||b.render_height>2160||b.render_width<b.output_width||b.render_height<b.output_height
        ||static_cast<std::uint64_t>(b.render_width)*b.output_height!=static_cast<std::uint64_t>(b.render_height)*b.output_width)return Error::Dimensions;
    Resolved resolved{};const auto error=Resolve(b.report_path,layout,resolved,inspect_files);if(error!=Error::None)return error;
    if(n.observer_report_path[0]){std::wstring observer;if(!NativeEffectsContractV2::Canonical(n.observer_report_path,L".json",observer))return Error::Path;
        if(!_wcsicmp(observer.c_str(),resolved.report_path))return Error::ReportCollision;}
    return Error::None;
}
inline Error PrepareInitial(const Config&request,const NativeGraphicsConfig::Version2&native,Layout layout,Config&output,bool inspect_files=true)noexcept{
    const auto error=Validate(request,native,layout,inspect_files);if(error!=Error::None)return error;output=request;return Error::None;
}
}
