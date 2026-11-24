#pragma once
#include "material_shader_effects.h"
#include "supersampling_hook_config.h"
#include <algorithm>
#include <cstring>
#include <string>

// Staging contract for the next native integration. It is deliberately not
// included by the frozen/live observer DLL until that checkpoint is retired.
namespace NativeEffectsContract {
static_assert(sizeof(MaterialShaderEffects::Config)==4132,"Effects transport ABI changed.");
enum class Error {None,Header,Flags,Dimensions,Parameters,Path,OutsideRoot,Reparse,MissingArtifact,ReportCollision};

struct LastErrorScope {DWORD saved=GetLastError();~LastErrorScope(){SetLastError(saved);}};

inline DWORD Requested(DWORD native_flags) {
    return ((native_flags&NativeGraphicsConfig::Water)?MaterialShaderEffects::River:0)
        |((native_flags&NativeGraphicsConfig::Terrain)?MaterialShaderEffects::Terrain:0)
        |((native_flags&NativeGraphicsConfig::Vegetation)?MaterialShaderEffects::Leaf:0);
}

inline bool FiniteRange(float value,float low,float high) {
    DWORD bits=0;std::memcpy(&bits,&value,sizeof(bits));
    return (bits&0x7f800000u)!=0x7f800000u && value>=low && value<=high;
}

inline bool Canonical(const wchar_t (&value)[512],const wchar_t* ending,std::wstring& output) {
    if(!NativeGraphicsConfig::AbsolutePath(value,ending))return false;
    std::wstring normalized(value);std::replace(normalized.begin(),normalized.end(),L'/',L'\\');
    wchar_t full[512]{};const DWORD length=GetFullPathNameW(normalized.c_str(),512,full,nullptr);
    if(!length || length>=512 || _wcsicmp(normalized.c_str(),full))return false;
    // Win32 trims trailing spaces/dots and accepts duplicate separators. Reject
    // those aliases rather than comparing two spellings as different reports.
    for(size_t i=3;i<normalized.size();++i) {
        if(normalized[i]==L'\\' && normalized[i-1]==L'\\')return false;
        const bool end=i+1==normalized.size() || normalized[i+1]==L'\\';
        if(end && (normalized[i]==L' ' || normalized[i]==L'.'))return false;
    }
    output=normalized;return true;
}

inline bool Within(const std::wstring& file,const std::wstring& directory) {
    return file.size()>directory.size()+1 && file[directory.size()]==L'\\'
        && !_wcsnicmp(file.c_str(),directory.c_str(),directory.size());
}

inline Error PlainAncestors(const std::wstring& file) {
    for(size_t i=3;i<file.size();++i)if(file[i]==L'\\') {
        const DWORD attributes=GetFileAttributesW(file.substr(0,i).c_str());
        if(attributes==INVALID_FILE_ATTRIBUTES || !(attributes&FILE_ATTRIBUTE_DIRECTORY))return Error::Path;
        if(attributes&FILE_ATTRIBUTE_REPARSE_POINT)return Error::Reparse;
    }
    return Error::None;
}

inline Error Validate(const MaterialShaderEffects::Config& effects,const NativeGraphicsConfig::Version2& native,
                      bool inspect_files=true) {
    LastErrorScope error;
    if(effects.size!=sizeof(effects) || effects.version!=1 || native.base.version!=2)return Error::Header;
    const DWORD requested=Requested(native.flags);
    if(!requested || (effects.flags&~requested) || (effects.flags&~MaterialShaderEffects::AllowedFlags))return Error::Flags;
    if(effects.render_width!=native.base.render_width || effects.render_height!=native.base.render_height
        || effects.render_width<320 || effects.render_height<200 || effects.render_width>3840 || effects.render_height>2160)return Error::Dimensions;
    if(!FiniteRange(effects.river_reflection_scale,1,1.3f) || !FiniteRange(effects.river_density_scale,.85f,1)
        || !FiniteRange(effects.terrain_detail_scale,1,1.4f) || !FiniteRange(effects.terrain_relief_scale,1,1.2f))return Error::Parameters;
    std::wstring native_report,report;
    if(!Canonical(native.base.report_path,L".json",native_report) || !Canonical(effects.report_path,L".private.json",report))return Error::Path;
    const size_t slash=native_report.find_last_of(L'\\');
    if(slash==std::wstring::npos)return Error::Path;
    const std::wstring root=native_report.substr(0,slash);
    if(!Within(report,root))return Error::OutsideRoot;
    if(!_wcsicmp(report.c_str(),native_report.c_str())
        || (native.observer_report_path[0] && !_wcsicmp(report.c_str(),native.observer_report_path)))return Error::ReportCollision;
    if(inspect_files) {
        const auto ancestor=PlainAncestors(report);if(ancestor!=Error::None)return ancestor;
        const DWORD attributes=GetFileAttributesW(report.c_str());
        if(attributes!=INVALID_FILE_ATTRIBUTES && (attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY)))return Error::Reparse;
    }
    const auto artifact=[&](const wchar_t (&path)[512],bool required,bool permitted)->Error {
        if(!path[0])return required?Error::Path:Error::None;
        if(!permitted)return Error::Flags;
        std::wstring full;
        if(!Canonical(path,L".bin",full))return Error::Path;
        if(!Within(full,root))return Error::OutsideRoot;
        if(!_wcsicmp(full.c_str(),report.c_str()) || !_wcsicmp(full.c_str(),native_report.c_str()))return Error::ReportCollision;
        if(inspect_files) {
            const auto ancestor=PlainAncestors(full);if(ancestor!=Error::None)return ancestor;
            const DWORD attributes=GetFileAttributesW(full.c_str());
            if(attributes==INVALID_FILE_ATTRIBUTES)return Error::MissingArtifact;
            if(attributes&(FILE_ATTRIBUTE_REPARSE_POINT|FILE_ATTRIBUTE_DIRECTORY))return Error::Reparse;
        }
        return Error::None;
    };
    const Error leaf=artifact(effects.leaf_vertex_path,(effects.flags&MaterialShaderEffects::Leaf)!=0,(requested&MaterialShaderEffects::Leaf)!=0);if(leaf!=Error::None)return leaf;
    const Error bump=artifact(effects.bump_pixel_path,(effects.flags&MaterialShaderEffects::Terrain)!=0,(requested&MaterialShaderEffects::Terrain)!=0);if(bump!=Error::None)return bump;
    const Error parallax=artifact(effects.parallax_pixel_path,(effects.flags&MaterialShaderEffects::Terrain)!=0,(requested&MaterialShaderEffects::Terrain)!=0);if(parallax!=Error::None)return parallax;
    if((effects.flags&MaterialShaderEffects::Terrain) && !_wcsicmp(effects.bump_pixel_path,effects.parallax_pixel_path))return Error::ReportCollision;
    return Error::None;
}
}
