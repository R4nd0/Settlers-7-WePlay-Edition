// SPDX-License-Identifier: MIT
// Future integration contract only. Does not link or enable material effects.
#pragma once
#include "material_shader_effects.h"
#include <cstddef>
#include <cstring>
#include <cwchar>
#include <type_traits>

namespace MaterialShaderConfigV2 {
constexpr DWORD Bytes=6180,WorkerBudget=8192;
// Config size alone is not the complete worker budget. The trusted host must
// measure DLL/renderer/export/header/padding bytes for each actual invocation.
inline bool FitsWorker(size_t overhead_bytes) noexcept {return overhead_bytes>0&&overhead_bytes<=WorkerBudget-Bytes;}
struct Config {
    DWORD size=Bytes,version=2,flags=0,render_width=0,render_height=0;
    float river_reflection_scale=1.15f,river_density_scale=.94f,terrain_detail_scale=1.20f,terrain_relief_scale=1.10f;
    wchar_t leaf_vertex_path[512]{},bump_pixel_path[512]{},parallax_pixel_path[512]{},report_path[512]{};
    wchar_t bump_vertex_path[512]{},parallax_vertex_path[512]{};
};
static_assert(sizeof(wchar_t)==2&&sizeof(Config)==Bytes&&Bytes<=WorkerBudget,"V2 worker ABI");
static_assert(sizeof(MaterialShaderEffects::Config)==4132,"legacy V1 allocation remains unchanged");
static_assert(std::is_standard_layout<Config>::value&&std::is_trivially_copyable<Config>::value,"flat V2 transport");
#define S7_V2_PREFIX_OFFSET(field) static_assert(offsetof(Config,field)==offsetof(MaterialShaderEffects::Config,field),"V1 prefix offset")
S7_V2_PREFIX_OFFSET(size);S7_V2_PREFIX_OFFSET(version);S7_V2_PREFIX_OFFSET(flags);
S7_V2_PREFIX_OFFSET(render_width);S7_V2_PREFIX_OFFSET(render_height);
S7_V2_PREFIX_OFFSET(river_reflection_scale);S7_V2_PREFIX_OFFSET(river_density_scale);
S7_V2_PREFIX_OFFSET(terrain_detail_scale);S7_V2_PREFIX_OFFSET(terrain_relief_scale);
S7_V2_PREFIX_OFFSET(leaf_vertex_path);S7_V2_PREFIX_OFFSET(bump_pixel_path);
S7_V2_PREFIX_OFFSET(parallax_pixel_path);S7_V2_PREFIX_OFFSET(report_path);
#undef S7_V2_PREFIX_OFFSET
static_assert(offsetof(Config,bump_vertex_path)==4132&&offsetof(Config,parallax_vertex_path)==5156,"appended V2 paths");

inline bool Range(float value,float low,float high) noexcept {
    DWORD bits=0;std::memcpy(&bits,&value,4);
    return (bits&0x7f800000u)!=0x7f800000u&&value>=low&&value<=high;
}
inline bool Suffix(const wchar_t* value,size_t length,const wchar_t* ending) noexcept {
    const size_t count=std::wcslen(ending);return length>=count&&!_wcsicmp(value+length-count,ending);
}
inline bool DeviceName(const wchar_t* value,size_t length) noexcept {
    size_t base=0;while(base<length&&value[base]!=L'.')++base;
    if((base==3&&(!_wcsnicmp(value,L"CON",3)||!_wcsnicmp(value,L"PRN",3)||!_wcsnicmp(value,L"AUX",3)||!_wcsnicmp(value,L"NUL",3)))
        ||(base==6&&!_wcsnicmp(value,L"CLOCK$",6)))return true;
    return base==4&&(!_wcsnicmp(value,L"COM",3)||!_wcsnicmp(value,L"LPT",3))
        &&((value[3]>=L'1'&&value[3]<=L'9')||value[3]==0x00b9||value[3]==0x00b2||value[3]==0x00b3);
}
// Shape only: trusted host must independently confine ancestors and reject
// reparse points, verify immutable contents/hashes and the original game ABI.
inline bool Path(const wchar_t (&value)[512],const wchar_t* ending,bool required) noexcept {
    size_t length=0;while(length<512&&value[length])++length;
    if(length==512)return false;
    for(size_t i=length;i<512;++i)if(value[i])return false;
    if(!required)return length==0;
    if(length<4||!Suffix(value,length,ending))return false;
    const auto slash=[](wchar_t c){return c==L'\\'||c==L'/';};
    const bool drive=((value[0]>=L'A'&&value[0]<=L'Z')||(value[0]>=L'a'&&value[0]<=L'z'))&&value[1]==L':'&&slash(value[2]);
    const bool unc=length>5&&value[0]==L'\\'&&value[1]==L'\\'&&value[2]!=L'.'&&value[2]!=L'?'&&!slash(value[2]);
    if(!drive&&!unc)return false;
    size_t start=drive?3u:2u,components=0;
    for(size_t i=start;i<=length;++i){
        const wchar_t c=value[i];
        if(i<length&&(c<L' '||c==L'"'||c==L'<'||c==L'>'||c==L'|'||c==L'?'||c==L'*'||c==L':'))return false;
        if(i==length||slash(c)){
            const size_t count=i-start;
            if(!count||count>255||(count==1&&value[start]==L'.')||(count==2&&value[start]==L'.'&&value[start+1]==L'.')
                ||value[i-1]==L'.'||value[i-1]==L' '||DeviceName(value+start,count))return false;
            ++components;start=i+1;
        }
    }
    return !unc||components>=3;
}
// Initial configuration only. Flag-only A/B subset/zero changes use the host's
// separate safe-boundary toggle, not an implicit V1/V2 transport conversion.
inline bool Validate(const Config& value) noexcept {
    if(value.size!=Bytes||value.version!=2||!value.flags||(value.flags&~MaterialShaderEffects::AllowedFlags)
        ||value.render_width<320||value.render_height<200||value.render_width>8192||value.render_height>8192
        ||static_cast<std::uint64_t>(value.render_width)*value.render_height>32u*1024u*1024u
        ||!Range(value.river_reflection_scale,1,1.3f)||!Range(value.river_density_scale,.85f,1)
        ||!Range(value.terrain_detail_scale,1,1.4f)||!Range(value.terrain_relief_scale,1,1.2f))return false;
    const bool leaf=(value.flags&MaterialShaderEffects::Leaf)!=0,terrain=(value.flags&MaterialShaderEffects::Terrain)!=0;
    if(!Path(value.leaf_vertex_path,L".vs.bin",leaf)||!Path(value.bump_pixel_path,L".ps.bin",terrain)
        ||!Path(value.parallax_pixel_path,L".ps.bin",terrain)||!Path(value.bump_vertex_path,L".vs.bin",terrain)
        ||!Path(value.parallax_vertex_path,L".vs.bin",terrain)||!Path(value.report_path,L".private.json",true)
        ||std::wcslen(value.report_path)>=508)return false;
    const wchar_t* paths[]={value.leaf_vertex_path,value.bump_pixel_path,value.parallax_pixel_path,value.report_path,value.bump_vertex_path,value.parallax_vertex_path};
    for(size_t i=0;i<6;++i)for(size_t j=i+1;j<6;++j)if(paths[i][0]&&paths[j][0]&&!_wcsicmp(paths[i],paths[j]))return false;
    return true;
}
// A legacy 4132-byte buffer is refused BEFORE touching its contents. The caller
// must still supply a readable region of the declared size (normal worker guard).
inline bool Decode(const void* bytes,size_t count,Config* output) noexcept {
    if(!bytes||!output||count!=Bytes)return false;
    DWORD header[2]{};std::memcpy(header,bytes,sizeof(header));
    if(header[0]!=Bytes||header[1]!=2)return false;
    Config candidate{};std::memcpy(&candidate,bytes,sizeof(candidate));
    if(!Validate(candidate))return false;*output=candidate;return true;
}
}
