// SPDX-License-Identifier: MIT
#pragma once
#include "material_shader_config_v2.h"

namespace MaterialShaderConfigV3 {
constexpr DWORD Bytes=7204,WorkerBudget=8192;
inline bool FitsWorker(size_t overhead) noexcept{return overhead>0&&overhead<=WorkerBudget-Bytes;}
struct Config {
    DWORD size=Bytes,version=3,flags=0,render_width=0,render_height=0;
    float river_reflection_scale=1.15f,river_density_scale=.94f,terrain_detail_scale=1.20f,terrain_relief_scale=1.10f;
    wchar_t leaf_vertex_path[512]{},bump_pixel_path[512]{},parallax_pixel_path[512]{},report_path[512]{};
    wchar_t bump_vertex_path[512]{},parallax_vertex_path[512]{},leaf_pixel_path[512]{};
};
static_assert(sizeof(wchar_t)==2&&sizeof(Config)==Bytes,"Flat explicit V3 wire7204");
static_assert(std::is_standard_layout<Config>::value&&std::is_trivially_copyable<Config>::value,"Flat V3 transport");
#define S7_V3_OFFSET(field) static_assert(offsetof(Config,field)==offsetof(MaterialShaderConfigV2::Config,field),"Historical V2 prefix offset")
S7_V3_OFFSET(size);S7_V3_OFFSET(version);S7_V3_OFFSET(flags);S7_V3_OFFSET(render_width);S7_V3_OFFSET(render_height);
S7_V3_OFFSET(river_reflection_scale);S7_V3_OFFSET(river_density_scale);S7_V3_OFFSET(terrain_detail_scale);S7_V3_OFFSET(terrain_relief_scale);
S7_V3_OFFSET(leaf_vertex_path);S7_V3_OFFSET(bump_pixel_path);S7_V3_OFFSET(parallax_pixel_path);S7_V3_OFFSET(report_path);
S7_V3_OFFSET(bump_vertex_path);S7_V3_OFFSET(parallax_vertex_path);
#undef S7_V3_OFFSET
static_assert(offsetof(Config,leaf_pixel_path)==6180,"Only appended Leaf PS path; never reuse terrain/VS fields");
inline bool Parameters(const Config& v) noexcept {
    const float fixed[]={1.15f,.94f,1.20f,1.10f};
    return !std::memcmp(&v.river_reflection_scale,fixed,sizeof(fixed));
}
inline bool UnusedPaths(const Config& v) noexcept {
    using MaterialShaderConfigV2::Path;
    return Path(v.leaf_vertex_path,L".vs.bin",false)&&Path(v.bump_vertex_path,L".vs.bin",false)&&Path(v.parallax_vertex_path,L".vs.bin",false)
        &&Path(v.bump_pixel_path,L".ps.bin",false)&&Path(v.parallax_pixel_path,L".ps.bin",false);
}
// Initial material-module configuration only. The host normalizes its initial
// OFF request from independently declared Leaf intent, after host validation.
inline bool Validate(const Config& v) noexcept {
    using MaterialShaderConfigV2::Path;
    return v.size==Bytes&&v.version==3&&v.flags==MaterialShaderEffects::Leaf
        &&v.render_width>=320&&v.render_height>=200&&v.render_width<=8192&&v.render_height<=8192
        &&static_cast<std::uint64_t>(v.render_width)*v.render_height<=32u*1024u*1024u
        &&Parameters(v)&&UnusedPaths(v)&&Path(v.leaf_pixel_path,L".ps.bin",true)&&Path(v.report_path,L".private.json",true)
        &&std::wcslen(v.report_path)<508&&_wcsicmp(v.leaf_pixel_path,v.report_path);
}
// Caller still supplies a readable region of the exact declared size. The
// root-owned wire dispatcher uses guarded ReadProcessMemory, never this helper
// on an unknown pointer. Old buffers are refused before a full copy.
inline bool Decode(const void* bytes,size_t count,Config* output) noexcept {
    if(!bytes||!output||count!=Bytes)return false;
    DWORD header[2]{};std::memcpy(header,bytes,sizeof(header));if(header[0]!=Bytes||header[1]!=3)return false;
    Config value{};std::memcpy(&value,bytes,sizeof(value));if(!Validate(value))return false;*output=value;return true;
}
}
