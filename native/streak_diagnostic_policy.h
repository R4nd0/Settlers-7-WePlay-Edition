// SPDX-License-Identifier: MIT
// Pure diagnostic plan only: no D3D calls, activation, allocation or game writes.
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

namespace StreakDiagnosticPolicy {
enum class Mode : std::uint32_t { Off=0, NeutralBlobRgb=1 };
enum class Decision : std::uint32_t { Disabled=0, UnsupportedMode=1, NotAuthorized=2,
    UnmatchedProgram=3, UnobservedContext=4, UnmatchedTarget=5, UnmatchedState=6,
    NonfiniteInput=7, ApplyDiagnosticOnly=8 };
constexpr std::uint32_t TargetObserved=1,ViewportObserved=2,BlendObserved=4,
    DepthObserved=8,AlphaObserved=16,ColorWriteObserved=32,SamplerObserved=64;
constexpr std::uint32_t RequiredObservations=127;
struct Identity {std::array<unsigned char,32> vertex{},pixel{};std::uint32_t vertex_bytes=0,pixel_bytes=0;};
struct Input {
    Mode mode=Mode::Off;
    bool observe_authorized=false,diagnostic_explicitly_requested=false,nonzero_geometry=false;
    Identity program{};
    std::uint32_t observations=0,render_width=0,render_height=0,target_width=0,target_height=0,target_format=0;
    std::uint32_t viewport_x=0,viewport_y=0,viewport_width=0,viewport_height=0;
    std::uint32_t blend_enabled=0,src_blend=0,dst_blend=0,blend_op=0,separate_alpha=0,srgb_write=0;
    std::uint32_t depth_enabled=0,depth_write=0,depth_func=0,alpha_test=0,alpha_func=0,alpha_ref=0,color_write=0;
    std::uint32_t sampler_type=0,sampler_width=0,sampler_height=0,sampler_levels=0;
    std::array<float,4> original_shadow_color{};
};
struct Plan {
    Decision decision=Decision::Disabled;
    std::uint32_t pixel_constant=0;
    std::array<float,4> original{},applied{};
    bool diagnostic_only=true;
};
inline bool Hash(const std::array<unsigned char,32>& actual,const char* expected) noexcept {
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(unsigned i=0;i<32;++i)if(actual[i]!=static_cast<unsigned char>((digit(expected[2*i])<<4)|digit(expected[2*i+1])))return false;
    return true;
}
inline Plan Create(const Input& input) noexcept {
    Plan plan;std::memcpy(plan.original.data(),input.original_shadow_color.data(),16);
    std::memcpy(plan.applied.data(),input.original_shadow_color.data(),16);
    if(input.mode==Mode::Off)return plan;
    if(input.mode!=Mode::NeutralBlobRgb){plan.decision=Decision::UnsupportedMode;return plan;}
    if(!input.observe_authorized||!input.diagnostic_explicitly_requested||!input.nonzero_geometry){plan.decision=Decision::NotAuthorized;return plan;}
    const bool regular=input.program.vertex_bytes==412&&input.program.pixel_bytes==332
        &&Hash(input.program.vertex,"6280521d601d78a18b368fcf428288b2595d6638949120e85c16807f5b19ae59")
        &&Hash(input.program.pixel,"2e555aa46e512fd8e43a7e4c8bf6d6f1eb99e98cbeaa61a1a1b455e9321306cd");
    const bool terrain=input.program.vertex_bytes==468&&input.program.pixel_bytes==332
        &&Hash(input.program.vertex,"42ce847ed7e767153a02083281ec067af114d2a72b8df4c42ef7960df7461fd3")
        &&Hash(input.program.pixel,"4fe6d699e0c59c14f7aa28bb518d4bcc0b74622e82ca1debe06603be74c4f295");
    if(!regular&&!terrain){plan.decision=Decision::UnmatchedProgram;return plan;}
    plan.pixel_constant=regular?2u:7u;
    // These bits mean actual device observations, never inferred library defaults.
    // The existing 952-byte observer cannot supply the complete state set yet.
    if((input.observations&RequiredObservations)!=RequiredObservations){plan.decision=Decision::UnobservedContext;return plan;}
    if(input.render_width<320||input.render_height<200||input.render_width>8192||input.render_height>8192
        ||static_cast<std::uint64_t>(input.render_width)*input.render_height>32u*1024u*1024u
        ||input.target_width!=input.render_width||input.target_height!=input.render_height||input.target_format!=D3DFMT_A8R8G8B8
        ||input.viewport_x||input.viewport_y||input.viewport_width!=input.render_width||input.viewport_height!=input.render_height
        ||input.sampler_type!=D3DRTYPE_TEXTURE||!input.sampler_width||!input.sampler_height
        ||input.sampler_width>8192||input.sampler_height>8192||!input.sampler_levels||input.sampler_levels>16){
        plan.decision=Decision::UnmatchedTarget;return plan;
    }
    if(input.blend_enabled!=1||input.src_blend!=D3DBLEND_ZERO||input.dst_blend!=D3DBLEND_INVSRCCOLOR
        ||input.blend_op!=D3DBLENDOP_ADD||input.separate_alpha||input.srgb_write
        ||input.depth_enabled!=D3DZB_TRUE||input.depth_write||input.depth_func!=D3DCMP_EQUAL
        ||input.alpha_test!=1||input.alpha_func<D3DCMP_NEVER||input.alpha_func>D3DCMP_ALWAYS||input.alpha_ref>255
        ||!(input.color_write&7u)||(input.color_write&~15u)){
        plan.decision=Decision::UnmatchedState;return plan;
    }
    std::array<std::uint32_t,4> words{};std::memcpy(words.data(),input.original_shadow_color.data(),16);
    for(const auto bits:words)if((bits&0x7f800000u)==0x7f800000u){plan.decision=Decision::NonfiniteInput;return plan;}
    // Original pixel expression is sampledColor*ShadowColor. Preserve alpha
    // BITWISE so the original alpha test, depth/stencil decisions and alpha
    // output remain unchanged. RGB zero is neutral under ZERO/INVSRCCOLOR ADD.
    // This is a short pass-association diagnostic, never a shadow-quality fix.
    words[0]=words[1]=words[2]=0;std::memcpy(plan.applied.data(),words.data(),16);
    plan.decision=Decision::ApplyDiagnosticOnly;return plan;
}
}
