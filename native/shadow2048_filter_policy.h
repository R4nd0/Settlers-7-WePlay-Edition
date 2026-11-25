// Private candidate only: measured 2048 far-fade pass, not a runtime component.
#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <d3d9.h>
#include <array>
#include <cstdint>
#include <cstring>

namespace Shadow2048Filter {
enum class Decision {Disabled,NotAuthorized,MissingObservation,Program,Map,Context,State,Radius,Fade,Kernel,Inverse,Apply};
constexpr std::array<std::uint32_t,32> ObservedKernel{{
    0xbd947128u,0xbf042708u,0xbdd7d1b1u,0x3f7d3ffau,0x3f72d7e6u,0x3e844d08u,0xbf774fefu,0x3e5cd9b8u,
    0x3f48c392u,0xbf19db34u,0x3e0e2920u,0x3e6be9d8u,0xbf50e7a2u,0xbf041308u,0x3f076b0eu,0x3f476f8eu,
    0xbf2a3b54u,0x3f3c9f7au,0xbed13da2u,0xbc5a81c1u,0x3e9acd34u,0xbf73f3e8u,0x3f0ddb1cu,0xbdee31e0u,
    0xbece259cu,0xbf6973d3u,0xbe88e512u,0x3ef35de8u,0x3eba7574u,0xbf005f01u,0x3f7a53f4u,0xbe4c5998u
}};
struct Input {
    bool enabled=false,observe_authorized=false,explicit_lab_request=false,nonzero_geometry=false;
    std::uint32_t observation_mask=0; // Actual successful program/map/context/state/uniform+kernel reads: all5bits required.
    std::array<unsigned char,32> vertex_sha{},pixel_sha{};
    std::uint32_t vertex_bytes=0,pixel_bytes=0,map_type=0,map_format=0,map_width=0,map_height=0,map_levels=0;
    std::uint32_t render_width=0,render_height=0,target_width=0,target_height=0,target_format=0;
    std::uint32_t viewport_x=0,viewport_y=0,viewport_width=0,viewport_height=0;
    std::uint32_t alpha_blend=0,alpha_test=0,srgb_write=0,depth_enable=0,depth_write=0;
    std::uint32_t stencil_enable=0,stencil_function=0,stencil_reference=0,stencil_fail=0,stencil_zfail=0,stencil_pass=0,color_write=0;
    std::array<float,4> inverse{},radius{},fade{};
    std::array<std::uint32_t,32> kernel{};
};
struct Plan {Decision decision=Decision::Disabled;std::uint32_t pixel_constant=57;std::array<float,4> original{},applied{};};
inline bool Hash(const std::array<unsigned char,32>& actual,const char* expected){
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(unsigned i=0;i<32;++i)if(actual[i]!=static_cast<unsigned char>((digit(expected[2*i])<<4)|digit(expected[2*i+1])))return false;
    return true;
}
inline std::uint32_t Bits(float value){std::uint32_t result=0;std::memcpy(&result,&value,4);return result;}
inline bool NearBits(std::uint32_t actual,std::uint32_t expected){return actual>expected?actual-expected<=8:expected-actual<=8;}
inline Plan Create(const Input& input){
    Plan plan;std::memcpy(plan.original.data(),input.inverse.data(),16);std::memcpy(plan.applied.data(),input.inverse.data(),16);
    if(!input.enabled)return plan;
    if(!input.observe_authorized||!input.explicit_lab_request||!input.nonzero_geometry){plan.decision=Decision::NotAuthorized;return plan;}
    if((input.observation_mask&31u)!=31u){plan.decision=Decision::MissingObservation;return plan;}
    if(input.vertex_bytes!=856||input.pixel_bytes!=2216
        ||!Hash(input.vertex_sha,"7744611870305c0b6b9a330010cda3599c34080774b7f11bef367fdcd21eb698")
        ||!Hash(input.pixel_sha,"6be44bdf12ff3d9b94cadc9fed076c55e26207d4a4233120e5c073a76dcd38ea")){
        plan.decision=Decision::Program;return plan;
    }
    // Deliberately excludes every4096/8192 draw and its existing correction.
    if(input.map_type!=D3DRTYPE_TEXTURE||input.map_format!=D3DFMT_D16||input.map_width!=2048||input.map_height!=2048||input.map_levels!=1){plan.decision=Decision::Map;return plan;}
    if(input.render_width<800||input.render_height<600||input.render_width>3840||input.render_height>2160
        ||input.target_width!=input.render_width||input.target_height!=input.render_height||input.target_format!=D3DFMT_A8R8G8B8
        ||input.viewport_x||input.viewport_y||input.viewport_width!=input.render_width||input.viewport_height!=input.render_height){plan.decision=Decision::Context;return plan;}
    if(input.alpha_blend||input.alpha_test||input.srgb_write||input.depth_enable||input.depth_write
        ||input.stencil_enable!=1||input.stencil_function!=D3DCMP_EQUAL||input.stencil_reference!=3
        ||input.stencil_fail!=D3DSTENCILOP_KEEP||input.stencil_zfail!=D3DSTENCILOP_KEEP||input.stencil_pass!=D3DSTENCILOP_KEEP
        ||input.color_write!=15){plan.decision=Decision::State;return plan;}
    if(Bits(input.radius[0])!=0x40300000u||Bits(input.radius[1])!=0x40300000u){plan.decision=Decision::Radius;return plan;}
    // Actual observed full-fade24k/out32k normalized interval, not an inferred
    // engine cascade index. Other near/fade passes are left completely alone.
    if(!NearBits(Bits(input.fade[0]),0xc2f9fffdu)||!NearBits(Bits(input.fade[1]),0x407ffffeu)){plan.decision=Decision::Fade;return plan;}
    if(input.kernel!=ObservedKernel){plan.decision=Decision::Kernel;return plan;}
    if(Bits(input.inverse[0])!=0x3a000000u||Bits(input.inverse[1])!=0x3a000000u){plan.decision=Decision::Inverse;return plan;}
    std::array<std::uint32_t,4> words{};std::memcpy(words.data(),input.inverse.data(),16);
    words[0]=words[1]=0x3a600000u; // Exact1.75/2048, no FP state or radius/noise-frequency mutation.
    std::memcpy(plan.applied.data(),words.data(),16);plan.decision=Decision::Apply;return plan;
}
}
