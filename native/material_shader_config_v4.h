// SPDX-License-Identifier: MIT
#pragma once
#include "material_shader_config_v2.h"
namespace MaterialShaderConfigV4 {
constexpr DWORD Bytes=4120,WorkerBudget=8192;
inline bool FitsWorker(size_t overhead)noexcept{return overhead>0&&overhead<=WorkerBudget-Bytes;}
struct Config {
    DWORD size=Bytes,version=4,flags=0,render_width=0,render_height=0,selected_variant=0;
    wchar_t program_paths[3][512]{},report_path[512]{};
};
static_assert(sizeof(Config)==Bytes&&sizeof(wchar_t)==2&&std::is_trivially_copyable<Config>::value,"Flat V4 Leaf palette wire");
static_assert(offsetof(Config,flags)==8&&offsetof(Config,selected_variant)==20&&offsetof(Config,program_paths)==24&&offsetof(Config,report_path)==3096,"Explicit independent V4 offsets");
inline bool Validate(const Config& v)noexcept{
    using MaterialShaderConfigV2::Path;
    if(v.size!=Bytes||v.version!=4||v.flags!=MaterialShaderEffects::Leaf||v.selected_variant>2
        ||v.render_width<320||v.render_height<200||v.render_width>8192||v.render_height>8192
        ||static_cast<std::uint64_t>(v.render_width)*v.render_height>32u*1024u*1024u
        ||!Path(v.report_path,L".private.json",true)||std::wcslen(v.report_path)>=508)return false;
    for(unsigned i=0;i<3;++i){if(!Path(v.program_paths[i],L".ps.bin",true)||!_wcsicmp(v.program_paths[i],v.report_path))return false;
        for(unsigned j=0;j<i;++j)if(!_wcsicmp(v.program_paths[i],v.program_paths[j]))return false;}
    return true;
}
inline bool Decode(const void* bytes,size_t count,Config* output)noexcept{
    if(!bytes||!output||count!=Bytes)return false;DWORD header[2]{};std::memcpy(header,bytes,sizeof(header));
    if(header[0]!=Bytes||header[1]!=4)return false;Config value{};std::memcpy(&value,bytes,sizeof(value));
    if(!Validate(value))return false;*output=value;return true;
}
}
