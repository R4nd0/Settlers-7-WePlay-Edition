// SPDX-License-Identifier: MIT
// Fixed original-S7 SSAO strength candidate. No game IO or shader replacement.
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

namespace AmbientOcclusionPolicy {
enum class Decision {Disabled,NotAuthorized,MissingObservation,Program,Map,Context,State,Strength,NoChange,Apply,Profile};
struct Input {
    bool enabled=false,observe_authorized=false,explicit_lab_request=false,nonzero_geometry=false;
    std::uint32_t observation_mask=0;
    std::array<unsigned char,32> vertex_sha{},pixel_sha{};
    std::uint32_t vertex_bytes=0,pixel_bytes=0,map_type=0,map_format=0,map_width=0,map_height=0,map_levels=0;
    std::uint32_t render_width=0,render_height=0,target_width=0,target_height=0,target_format=0;
    std::uint32_t viewport_x=0,viewport_y=0,viewport_width=0,viewport_height=0;
    std::uint32_t alpha_blend=0,alpha_test=0,srgb_write=0,depth_enable=0,depth_write=0;
    std::uint32_t stencil_enable=0,stencil_function=0,stencil_reference=0,stencil_fail=0,stencil_zfail=0,stencil_pass=0,color_write=0;
    std::array<float,4> strength{};
    std::uint32_t profile_version=1;
};
struct Plan {Decision decision=Decision::Disabled;std::uint32_t pixel_constant=81;std::array<float,4> original{},applied{};};
inline bool Hash(const std::array<unsigned char,32>& actual,const char* expected){
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(unsigned i=0;i<32;++i)if(actual[i]!=static_cast<unsigned char>((digit(expected[2*i])<<4)|digit(expected[2*i+1])))return false;
    return true;
}
inline std::uint32_t Bits(float value){std::uint32_t result=0;std::memcpy(&result,&value,4);return result;}
// Exact round-to-nearest-even binary32 multiplication by5/4, then fixed profile clamp.
// Integer arithmetic keeps pure policy and caller FP flags/DAZ/FTZ untouched,
// including tiny finite strengths. Module uses only the fixed A/B ceilings.
inline std::uint32_t ScaleBits(std::uint32_t bits,std::uint32_t ceiling=0x3f800000u){
    const auto rounded=[](std::uint32_t value,unsigned shift){
        const std::uint32_t whole=value>>shift,rest=value&((1u<<shift)-1),half=1u<<(shift-1);
        return whole+(rest>half||(rest==half&&(whole&1u))?1u:0u);
    };
    std::uint32_t exponent=bits>>23,significand=bits&0x7fffffu,result=0;
    if(!exponent)result=rounded(significand*5u,2);
    else {
        const std::uint32_t product=(significand|0x800000u)*5u;
        const unsigned shift=product>=0x4000000u?3u:2u;
        significand=rounded(product,shift);exponent+=shift-2;
        if(significand==0x1000000u){significand>>=1;++exponent;}
        result=(exponent<<23)|(significand&0x7fffffu);
    }
    return result>ceiling?ceiling:result;
}
inline Plan Create(const Input& input){
    Plan plan;std::memcpy(plan.original.data(),input.strength.data(),16);std::memcpy(plan.applied.data(),input.strength.data(),16);
    if(!input.enabled)return plan;
    if(!input.observe_authorized||!input.explicit_lab_request||!input.nonzero_geometry){plan.decision=Decision::NotAuthorized;return plan;}
    if(input.profile_version!=1&&input.profile_version!=2){plan.decision=Decision::Profile;return plan;}
    if((input.observation_mask&31u)!=31u){plan.decision=Decision::MissingObservation;return plan;}
    if(input.vertex_bytes!=452||input.pixel_bytes!=5104
        ||!Hash(input.vertex_sha,"f7ddf52720df92c869594da9c7826f275834f9a44ed2694226c42133763a0d11")
        ||!Hash(input.pixel_sha,"aaaed0141362683d3c82f27557e1e63813e385ad43b21bb6cea2ee517562163c")){
        plan.decision=Decision::Program;return plan;
    }
    if(input.map_type!=D3DRTYPE_TEXTURE||input.map_format!=D3DFMT_R32F||input.map_width!=input.render_width
        ||input.map_height!=input.render_height||input.map_levels!=1){plan.decision=Decision::Map;return plan;}
    if(input.render_width<800||input.render_height<600||input.render_width>3840||input.render_height>2160
        ||(input.render_width&1u)||(input.render_height&1u)||input.target_width!=input.render_width/2||input.target_height!=input.render_height/2
        ||input.target_format!=D3DFMT_A8R8G8B8||input.viewport_x||input.viewport_y
        ||input.viewport_width!=input.target_width||input.viewport_height!=input.target_height){plan.decision=Decision::Context;return plan;}
    if(input.alpha_blend||input.alpha_test||input.srgb_write||input.depth_enable||input.depth_write||input.stencil_enable||input.color_write!=15){plan.decision=Decision::State;return plan;}
    const std::uint32_t before=Bits(input.strength[0]);
    const std::uint32_t ceiling=input.profile_version==1?0x3f800000u:0x3fa00000u;
    if(before!=0x80000000u&&((before&0x80000000u)||before>ceiling)){plan.decision=Decision::Strength;return plan;}
    if(!(before&0x7fffffffu)||before==ceiling){plan.decision=Decision::NoChange;return plan;}
    const auto after=ScaleBits(before,ceiling);if(after==before){plan.decision=Decision::NoChange;return plan;}
    std::memcpy(plan.applied.data(),&after,4);plan.decision=Decision::Apply;return plan;
}
}
