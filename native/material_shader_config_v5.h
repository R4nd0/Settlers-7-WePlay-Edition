// SPDX-License-Identifier: MIT
#pragma once
#include "material_shader_effects.h"
#include <cstring>
#include <type_traits>
namespace MaterialShaderConfigV5 {
constexpr DWORD Bytes=24,Allowed=MaterialShaderEffects::Terrain|MaterialShaderEffects::Leaf;
enum class Layout {RecipientLocalData,PrivateCase};
struct Config {DWORD size=Bytes,version=5,flags=0,render_width=0,render_height=0,selected_variant=0;};
static_assert(sizeof(Config)==24&&std::is_trivially_copyable<Config>::value,"Independent compact V5 wire");
static_assert(offsetof(Config,flags)==8&&offsetof(Config,selected_variant)==20,"V5 offsets");
inline bool Validate(const Config&c)noexcept{return c.size==Bytes&&c.version==5&&!(c.flags&~Allowed)&&c.selected_variant<3
    &&c.render_width>=320&&c.render_height>=200&&c.render_width<=8192&&c.render_height<=8192
    &&static_cast<std::uint64_t>(c.render_width)*c.render_height<=32u*1024u*1024u;}
inline bool FitsWorker(size_t overhead)noexcept{return overhead>0&&overhead<=8192-Bytes;}
inline bool Decode(const void* bytes,size_t count,Config*out)noexcept{
    if(!bytes||!out||count!=Bytes)return false;DWORD header[2]{};std::memcpy(header,bytes,8);if(header[0]!=Bytes||header[1]!=5)return false;
    Config value{};std::memcpy(&value,bytes,sizeof(value));if(!Validate(value))return false;*out=value;return true;
}
}
