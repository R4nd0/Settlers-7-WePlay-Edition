// SPDX-License-Identifier: MIT
// Policy only; transport decoding and renderer/executable admission stay in host.
#pragma once
#include "material_shader_config_v2.h"
#include "supersampling_hook_config.h"
#include <algorithm>
#include <array>
#include <string>

namespace NativeEffectsContractV2 {
using Config=MaterialShaderConfigV2::Config;
constexpr DWORD Allowed=MaterialShaderEffects::Terrain|MaterialShaderEffects::Leaf;
enum class Layout {RecipientLocalData,PrivateCase};
enum class Error {None,Header,Flags,Dimensions,Parameters,Path,OutsideRoot,FixedPath,Reparse,MissingArtifact,ReportCollision,Internal};
struct LastErrorScope {DWORD saved=GetLastError();~LastErrorScope(){SetLastError(saved);}};
struct Program {const wchar_t* directory;const wchar_t* digest;const wchar_t* suffix;DWORD role;};
inline constexpr std::array<Program,5> Programs{{
    {L"leaf-angular-lobe-v2",L"119352b9def2ba290c8a5bc99b18b77cf4730461e97d0fbf677fba104e1e472a",L".vs.bin",MaterialShaderEffects::Leaf},
    {L"terrain-normal-BumpLayerShader-v2",L"c2ae22a6739e071774edd071870e6dc8fd058b5bf6eec6490bc1a4603fea2fa3",L".ps.bin",MaterialShaderEffects::Terrain},
    {L"terrain-normal-ParallaxLayerShader-v2",L"d0d082bbb9b8d2de1388b790165bcebc5fda571d88895db3e8b36004bf2e52fa",L".ps.bin",MaterialShaderEffects::Terrain},
    {L"terrain-near-distance-BumpLayerShader-v2",L"46a8dd89646f84cb761f832f47739f3d0c4fe5494688b3104b56c43fac8d3f07",L".vs.bin",MaterialShaderEffects::Terrain},
    {L"terrain-near-distance-ParallaxLayerShader-v2",L"d985366573816225b04473b762fd6f899241849185274347f513c1f4cf7de6c6",L".vs.bin",MaterialShaderEffects::Terrain}
}};
// Explicit immutable recipe alternative on the same wire2 ABI. Common PS/leaf
// identities stay equal; both terrain VS paths must select this set together.
inline constexpr auto MacroPrograms=[](){auto result=Programs;
    result[3]={L"terrain-macro-relief-BumpLayerShader-v3",L"da8154477a4fd3c44ace75d9c42d6d3714dc196c9c0f30fc332f06493539a21b",L".vs.bin",MaterialShaderEffects::Terrain};
    result[4]={L"terrain-macro-relief-ParallaxLayerShader-v3",L"b421d294516b89d0e0de90af56982b76ad8d196808c2d548ed251a3312313c7d",L".vs.bin",MaterialShaderEffects::Terrain};
    return result;}();
inline constexpr Program LeafHemisphereProgram{L"leaf-hemisphere-contrast-v3",
    L"ffb6d89661e9ae8a9d75ba3d322a318996d07d1dbb81281fefdb4b8df2f277f7",L".vs.bin",MaterialShaderEffects::Leaf};
#ifdef S7_MATERIAL_EFFECTS_FIXTURE
// Authored GPU fixtures must explicitly pin their synthetic programs in BOTH
// this policy and the effects component. This API is absent from production.
inline std::array<std::array<wchar_t,65>,5> fixture_digests{};
inline bool FixtureDigest(unsigned slot,const wchar_t* value) noexcept {
    if(slot>=5||!value||wcsnlen_s(value,65)!=64)return false;
    for(size_t i=0;i<64;++i)if(!((value[i]>=L'0'&&value[i]<=L'9')||(value[i]>=L'a'&&value[i]<=L'f')))return false;
    std::memcpy(fixture_digests[slot].data(),value,64*sizeof(wchar_t));return true;
}
#endif
inline const wchar_t* Digest(size_t slot,bool macro=false) noexcept {
    if(macro&&slot>=3)return MacroPrograms[slot].digest;
#ifdef S7_MATERIAL_EFFECTS_FIXTURE
    if(fixture_digests[slot][0])return fixture_digests[slot].data();
#endif
    return Programs[slot].digest;
}
inline DWORD Requested(DWORD native_flags) noexcept {
    return ((native_flags&NativeGraphicsConfig::Terrain)?MaterialShaderEffects::Terrain:0)
        |((native_flags&NativeGraphicsConfig::Vegetation)?MaterialShaderEffects::Leaf:0);
}
inline bool SamePinnedConfig(const Config& a,const Config& b) noexcept {
    constexpr size_t flag_offset=offsetof(Config,flags),after_flags=flag_offset+sizeof(DWORD);
    return a.size==6180&&a.version==2&&b.size==6180&&b.version==2
        &&!std::memcmp(&a,&b,flag_offset)
        &&!std::memcmp(reinterpret_cast<const unsigned char*>(&a)+after_flags,
                         reinterpret_cast<const unsigned char*>(&b)+after_flags,sizeof(Config)-after_flags);
}
inline bool PositiveRange(float value,float low,float high) noexcept {
    // Bounds are positive; IEEE float bit ordering avoids touching caller FP
    // exception status even for hostile subnormal/NaN transport values.
    DWORD bits=0,lo=0,hi=0;std::memcpy(&bits,&value,4);std::memcpy(&lo,&low,4);std::memcpy(&hi,&high,4);
    return !(bits&0x80000000u)&&(bits&0x7f800000u)!=0x7f800000u&&bits>=lo&&bits<=hi;
}
inline bool Canonical(const wchar_t (&value)[512],const wchar_t* suffix,std::wstring& output) {
    if(!MaterialShaderConfigV2::Path(value,suffix,true))return false;
    output=value;std::replace(output.begin(),output.end(),L'/',L'\\');
    wchar_t full[512]{};const DWORD size=GetFullPathNameW(output.c_str(),512,full,nullptr);
    return size&&size<512&&!_wcsicmp(output.c_str(),full);
}
inline bool Within(const std::wstring& file,const std::wstring& root) noexcept {
    return file.size()>root.size()+1&&file[root.size()]==L'\\'&&!_wcsnicmp(file.c_str(),root.c_str(),root.size());
}
inline Error Preflight(const std::wstring& full,bool may_be_absent) {
    size_t root=3;
    if(full.size()>2&&full[0]==L'\\'&&full[1]==L'\\'){
        const auto server=full.find(L'\\',2);if(server==std::wstring::npos)return Error::Path;
        const auto share=full.find(L'\\',server+1);if(share==std::wstring::npos)return Error::Path;root=share+1;
    }
    std::wstring part=full;bool leaf=true;
    for(;;){const DWORD attributes=GetFileAttributesW(part.c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES){if(!(leaf&&may_be_absent&&GetLastError()==ERROR_FILE_NOT_FOUND))return leaf?Error::MissingArtifact:Error::Path;}
        else {if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return Error::Reparse;
            if(leaf?(attributes&FILE_ATTRIBUTE_DIRECTORY)!=0:(attributes&FILE_ATTRIBUTE_DIRECTORY)==0)return Error::Path;}
        if(part.size()<=root)break;const auto slash=part.find_last_of(L'\\');if(slash==std::wstring::npos)return Error::Path;
        part.resize(slash<root?root:slash);leaf=false;}
    return Error::None;
}
inline Error Validate(const Config& effects,const NativeGraphicsConfig::Version2& native,Layout layout,bool inspect_files=true) noexcept {
    LastErrorScope error;try {
        if(effects.size!=6180||effects.version!=2||native.base.version!=2||native.base.size!=sizeof(native))return Error::Header;
        const DWORD intent=Requested(native.flags);
        if(!intent||(native.flags&~NativeGraphicsConfig::KnownFlags)||(effects.flags&~Allowed)||(effects.flags&~intent))return Error::Flags;
        const auto& base=native.base;
        if(base.output_width<320||base.output_height<180||base.render_height<200||base.render_width>3840||base.render_height>2160
            ||base.render_width<base.output_width||base.render_height<base.output_height
            ||static_cast<std::uint64_t>(base.render_width)*base.output_height!=static_cast<std::uint64_t>(base.render_height)*base.output_width
            ||effects.render_width!=base.render_width||effects.render_height!=base.render_height)return Error::Dimensions;
        if(!PositiveRange(effects.river_reflection_scale,1,1.3f)||!PositiveRange(effects.river_density_scale,.85f,1)
            ||!PositiveRange(effects.terrain_detail_scale,1,1.4f)||!PositiveRange(effects.terrain_relief_scale,1,1.2f))return Error::Parameters;
        std::wstring native_report,report;
        if(!Canonical(base.report_path,L".json",native_report)||!Canonical(effects.report_path,L".private.json",report)||report.size()>=508)return Error::Path;
        const auto slash=native_report.find_last_of(L'\\');if(slash==std::wstring::npos)return Error::Path;
        const auto root=native_report.substr(0,slash);
        if(!Within(report,root))return Error::OutsideRoot;
        if(_wcsicmp(report.c_str(),(root+L"\\material-effects.private.json").c_str()))return Error::FixedPath;
        if(!_wcsicmp(report.c_str(),native_report.c_str()))return Error::ReportCollision;
        if(native.observer_report_path[0]){std::wstring observer;
            if(!Canonical(native.observer_report_path,L".json",observer))return Error::Path;
            if(!_wcsicmp(report.c_str(),observer.c_str()))return Error::ReportCollision;}
        std::wstring artifacts;
        if(layout==Layout::PrivateCase)artifacts=root+L"\\LocalData\\MaterialShaders\\";
        else if(layout==Layout::RecipientLocalData){const auto last=root.find_last_of(L'\\');
            if(last==std::wstring::npos||_wcsicmp(root.c_str()+last+1,L"LocalData"))return Error::Path;
            artifacts=root+L"\\MaterialShaders\\";}
        else return Error::Path;
        const wchar_t (*paths[])[512]={&effects.leaf_vertex_path,&effects.bump_pixel_path,&effects.parallax_pixel_path,&effects.bump_vertex_path,&effects.parallax_vertex_path};
        bool historical=true,macro=(intent&MaterialShaderEffects::Terrain)!=0,hemisphere=intent==MaterialShaderEffects::Leaf;
        for(size_t slot=0;slot<5;++slot){const auto& spec=Programs[slot];
            const auto& value=*paths[slot];
            if(!(intent&spec.role)){if(!MaterialShaderConfigV2::Path(value,spec.suffix,false))return Error::Flags;continue;}
            std::wstring full;if(!Canonical(value,spec.suffix,full))return Error::Path;
            if(!Within(full,root))return Error::OutsideRoot;
            const std::wstring expected=artifacts+spec.directory+L"\\"+Digest(slot)+spec.suffix;
            const auto& macro_spec=MacroPrograms[slot];
            const std::wstring macro_expected=artifacts+macro_spec.directory+L"\\"+Digest(slot,true)+macro_spec.suffix;
            historical=historical&&!_wcsicmp(full.c_str(),expected.c_str());
            macro=macro&&!_wcsicmp(full.c_str(),macro_expected.c_str());
            if(slot==0){const std::wstring hemi_expected=artifacts+LeafHemisphereProgram.directory+L"\\"+LeafHemisphereProgram.digest+LeafHemisphereProgram.suffix;
                hemisphere=hemisphere&&!_wcsicmp(full.c_str(),hemi_expected.c_str());}
            else hemisphere=false; // No mixed terrain/leaf hemisphere profile.
            if(!historical&&!macro&&!hemisphere)return Error::FixedPath;
            if(inspect_files){const auto check=Preflight(full,false);if(check!=Error::None)return check;}
        }
        if(!historical&&macro&&(!PositiveRange(effects.terrain_detail_scale,1,1)||!PositiveRange(effects.terrain_relief_scale,1,1)))return Error::Parameters;
        if(inspect_files){const auto first=Preflight(native_report,true);if(first!=Error::None)return first;
            const auto second=Preflight(report,true);if(second!=Error::None)return second;}
        return Error::None;
    }catch(...){return Error::Internal;}
}
inline Error PrepareInitial(const Config& request,const NativeGraphicsConfig::Version2& native,Layout layout,Config& output,bool inspect_files=true) noexcept {
    const auto error=Validate(request,native,layout,inspect_files);if(error!=Error::None)return error;
    output=request;output.flags=Requested(native.flags);return Error::None;
}
}
