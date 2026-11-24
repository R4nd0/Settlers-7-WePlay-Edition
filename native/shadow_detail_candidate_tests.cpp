// Authored CPU policy fixtures; no original program bodies, game or GPU.
#include "shadow_detail_candidate.h"
#include <cstdio>
#include <cfenv>

namespace {
void Fill(std::array<unsigned char,32>& output,const char* value) {
    const auto digit=[](char c){return c<='9'?c-'0':c-'a'+10;};
    for(unsigned i=0;i<32;++i)output[i]=static_cast<unsigned char>((digit(value[2*i])<<4)|digit(value[2*i+1]));
}
}
int main() {
    using namespace ShadowDetailCandidate;
    unsigned checks=0,failures=0;
    const auto check=[&](bool value,const char* name){++checks;if(!value){++failures;std::fprintf(stderr,"failed: %s\n",name);}};
    Input input;Fill(input.vertex_sha,VertexSha);Fill(input.pixel_sha,PixelSha);
    input.vertex_bytes=VertexBytes;input.pixel_bytes=PixelBytes;input.sampler_is_2d=true;
    input.sampler_width=input.sampler_height=4096;input.original_inverse={1.f/4096.f,1.f/4096.f,0.f,0.f};
    auto plan=Create(input);
    check(plan.decision==Decision::Disabled && plan.inverse==input.original_inverse,"off is byte-neutral");
    input.mode=Mode::SoftHighResolutionEdges;
    for(uint32_t dimension:{4096u,8192u}) {
        input.sampler_width=input.sampler_height=dimension;
        input.original_inverse[0]=input.original_inverse[1]=1.f/static_cast<float>(dimension);
        const uint32_t special[]={0x7fc01234u,0x80000000u};
        std::memcpy(input.original_inverse.data()+2,special,sizeof(special));
        const int old_round=std::fegetround();std::fesetround(FE_DOWNWARD);
        plan=Create(input);
        check(plan.decision==Decision::Apply,"known exact native footprint selected");
        check(plan.inverse[0]==1.25f/static_cast<float>(dimension) && plan.inverse[1]==plan.inverse[0],"1.25 footprint");
        check(!std::memcmp(plan.inverse.data()+2,special,sizeof(special)),"unused z/w preserved including NaN payload");
        check(std::fegetround()==FE_DOWNWARD,"rounding state preserved");std::fesetround(old_round);
        input.original_inverse[0]=input.original_inverse[1]=1.f/2048.f;
        check(Create(input).decision==Decision::PreserveExistingFarCorrection,"production far correction retained");
    }
    input.original_inverse[0]=1.f/8192.f;input.original_inverse[1]=1.f/4096.f;
    check(Create(input).decision==Decision::UnknownFootprint,"mixed inverse rejected");
    input.sampler_width=2048;input.sampler_height=2048;
    check(Create(input).decision==Decision::UnmatchedSampler,"stock map not widened");
    input.sampler_width=4096;input.sampler_height=8192;
    check(Create(input).decision==Decision::UnmatchedSampler,"nonsquare sampler rejected");
    input.sampler_height=4096;input.vertex_sha[0]^=1;
    check(Create(input).decision==Decision::UnmatchedProgram,"other original program excluded");
    input.mode=static_cast<Mode>(2);
    check(Create(input).decision==Decision::UnsupportedMode,"unsupported choice rejected");
    std::printf("shadow detail candidate: %u checks, %u failures; CPU only, no visible improvement qualified\n",checks,failures);
    return failures?1:0;
}
