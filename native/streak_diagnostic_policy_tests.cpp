#include "streak_diagnostic_policy.h"
#include <cstdio>

namespace {
unsigned checks=0,failures=0;
void Check(bool value){++checks;if(!value)++failures;}
void Hex(const char* text,std::array<unsigned char,32>& value){
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(unsigned i=0;i<32;++i)value[i]=static_cast<unsigned char>((digit(text[2*i])<<4)|digit(text[2*i+1]));
}
StreakDiagnosticPolicy::Input Eligible(bool terrain=false){
    using namespace StreakDiagnosticPolicy;Input input;input.mode=Mode::NeutralBlobRgb;
    input.observe_authorized=input.diagnostic_explicitly_requested=input.nonzero_geometry=true;
    input.program.vertex_bytes=terrain?468u:412u;input.program.pixel_bytes=332;
    Hex(terrain?"42ce847ed7e767153a02083281ec067af114d2a72b8df4c42ef7960df7461fd3":"6280521d601d78a18b368fcf428288b2595d6638949120e85c16807f5b19ae59",input.program.vertex);
    Hex(terrain?"4fe6d699e0c59c14f7aa28bb518d4bcc0b74622e82ca1debe06603be74c4f295":"2e555aa46e512fd8e43a7e4c8bf6d6f1eb99e98cbeaa61a1a1b455e9321306cd",input.program.pixel);
    input.observations=RequiredObservations;input.render_width=input.target_width=input.viewport_width=3840;
    input.render_height=input.target_height=input.viewport_height=2160;input.target_format=D3DFMT_A8R8G8B8;
    input.blend_enabled=1;input.src_blend=D3DBLEND_ZERO;input.dst_blend=D3DBLEND_INVSRCCOLOR;input.blend_op=D3DBLENDOP_ADD;
    input.depth_enabled=D3DZB_TRUE;input.depth_func=D3DCMP_EQUAL;input.alpha_test=1;input.alpha_func=D3DCMP_GREATER;input.alpha_ref=100;input.color_write=15;
    input.sampler_type=D3DRTYPE_TEXTURE;input.sampler_width=input.sampler_height=64;input.sampler_levels=7;
    input.original_shadow_color={.1f,.2f,.3f,-0.f};return input;
}
}
int main(){using namespace StreakDiagnosticPolicy;
    Input empty;Check(Create(empty).decision==Decision::Disabled);
    auto input=Eligible();auto plan=Create(input);Check(plan.decision==Decision::ApplyDiagnosticOnly&&plan.pixel_constant==2);
    Check(plan.applied[0]==0&&plan.applied[1]==0&&plan.applied[2]==0);
    Check(!std::memcmp(&plan.applied[3],&input.original_shadow_color[3],4));
    Check(!std::memcmp(plan.original.data(),input.original_shadow_color.data(),16));
    plan=Create(Eligible(true));Check(plan.decision==Decision::ApplyDiagnosticOnly&&plan.pixel_constant==7&&plan.diagnostic_only);
    for(unsigned bit=0;bit<7;++bit){input=Eligible();input.observations^=1u<<bit;Check(Create(input).decision==Decision::UnobservedContext);}
    input=Eligible();input.mode=static_cast<Mode>(99);Check(Create(input).decision==Decision::UnsupportedMode);
    input=Eligible();input.observe_authorized=false;Check(Create(input).decision==Decision::NotAuthorized);
    input=Eligible();input.nonzero_geometry=false;Check(Create(input).decision==Decision::NotAuthorized);
    input=Eligible();input.diagnostic_explicitly_requested=false;Check(Create(input).decision==Decision::NotAuthorized);
    input=Eligible();input.program.pixel[0]^=1;Check(Create(input).decision==Decision::UnmatchedProgram);
    input=Eligible();input.program.vertex_bytes=468;Check(Create(input).decision==Decision::UnmatchedProgram);
    input=Eligible();input.viewport_width=1920;Check(Create(input).decision==Decision::UnmatchedTarget);
    input=Eligible();input.target_format=D3DFMT_D16;Check(Create(input).decision==Decision::UnmatchedTarget);
    input=Eligible();input.sampler_type=D3DRTYPE_CUBETEXTURE;Check(Create(input).decision==Decision::UnmatchedTarget);
    input=Eligible();input.src_blend=D3DBLEND_ONE;Check(Create(input).decision==Decision::UnmatchedState);
    input=Eligible();input.blend_op=D3DBLENDOP_MAX;Check(Create(input).decision==Decision::UnmatchedState);
    input=Eligible();input.separate_alpha=1;Check(Create(input).decision==Decision::UnmatchedState);
    input=Eligible();input.depth_write=1;Check(Create(input).decision==Decision::UnmatchedState);
    input=Eligible();input.depth_func=D3DCMP_LESSEQUAL;Check(Create(input).decision==Decision::UnmatchedState);
    input=Eligible();input.alpha_ref=256;Check(Create(input).decision==Decision::UnmatchedState);
    input=Eligible();input.color_write=8;Check(Create(input).decision==Decision::UnmatchedState);
    input=Eligible();const std::uint32_t nan=0x7fc12345;std::memcpy(&input.original_shadow_color[3],&nan,4);Check(Create(input).decision==Decision::NonfiniteInput);
    std::printf("{\"format\":\"s7-streak-policy-cpu-v1\",\"checks\":%u,\"failures\":%u,\"game_started\":false,\"gpu_started\":false}\n",checks,failures);
    return failures?1:0;
}
