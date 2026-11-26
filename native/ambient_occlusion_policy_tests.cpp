#include "ambient_occlusion_policy.h"
#include <algorithm>
#include <cstdio>
#include <float.h>
#include <intrin.h>
#include <stdexcept>
namespace {
using namespace AmbientOcclusionPolicy;
unsigned checks=0;
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
void Hex(const char* text,std::array<unsigned char,32>& output){const auto d=[](char c){return c<='9'?c-'0':c-'a'+10;};for(unsigned i=0;i<32;++i)output[i]=static_cast<unsigned char>((d(text[2*i])<<4)|d(text[2*i+1]));}
Input Valid(){Input v;v.enabled=v.observe_authorized=v.explicit_lab_request=v.nonzero_geometry=true;v.observation_mask=31;
    Hex("f7ddf52720df92c869594da9c7826f275834f9a44ed2694226c42133763a0d11",v.vertex_sha);Hex("aaaed0141362683d3c82f27557e1e63813e385ad43b21bb6cea2ee517562163c",v.pixel_sha);
    v.vertex_bytes=452;v.pixel_bytes=5104;v.render_width=v.map_width=3840;v.render_height=v.map_height=2160;
    v.map_type=D3DRTYPE_TEXTURE;v.map_format=D3DFMT_R32F;v.map_levels=1;
    v.target_width=v.viewport_width=1920;v.target_height=v.viewport_height=1080;v.target_format=D3DFMT_A8R8G8B8;v.color_write=15;
    const std::uint32_t words[4]={0x3f480000u,0x80000000u,0x7f812345u,0x7fc12345u};std::memcpy(v.strength.data(),words,16);return v;}
}
int main(){try{
    auto v=Valid();const auto before=_mm_getcsr();unsigned cw=0;_controlfp_s(&cw,0,0);
    const auto plan=Create(v);Check(plan.decision==Decision::Apply,"exact source context applies");Check(plan.pixel_constant==81,"only scalar source binding");
    Check(Bits(plan.applied[0])==0x3f7a0000u,"fixed1.25 strength");Check(!std::memcmp(plan.original.data(),v.strength.data(),16),"complete backup");
    Check(!std::memcmp(plan.applied.data()+1,v.strength.data()+1,12),"unused components preserved incl signed0/sNaN/qNaN");
    unsigned after_cw=0;_controlfp_s(&after_cw,0,0);Check(before==_mm_getcsr()&&cw==after_cw,"policy preserves FP state");
    for(unsigned mode=0;mode<20;++mode){auto bad=v;Decision expected=Decision::Context;
        switch(mode){case 0:bad.enabled=false;expected=Decision::Disabled;break;case 1:bad.explicit_lab_request=false;expected=Decision::NotAuthorized;break;
            case 2:bad.observation_mask=15;expected=Decision::MissingObservation;break;case 3:bad.vertex_bytes=451;expected=Decision::Program;break;
            case 4:bad.pixel_sha[0]^=1;expected=Decision::Program;break;case 5:bad.map_format=D3DFMT_D16;expected=Decision::Map;break;
            case 6:bad.map_width=1920;expected=Decision::Map;break;case 7:bad.map_levels=2;expected=Decision::Map;break;
            case 8:bad.target_width=3840;break;case 9:bad.viewport_x=1;break;case 10:bad.target_format=D3DFMT_A16B16G16R16F;break;
            case 11:bad.target_width=0x80000780u;bad.viewport_width=bad.target_width;break;
            case 12:bad.alpha_blend=1;expected=Decision::State;break;case 13:bad.alpha_test=1;expected=Decision::State;break;
            case 14:bad.depth_enable=1;expected=Decision::State;break;case 15:bad.srgb_write=1;expected=Decision::State;break;
            case 16:bad.depth_write=1;expected=Decision::State;break;case 17:bad.stencil_enable=1;expected=Decision::State;break;
            case 18:bad.color_write=7;expected=Decision::State;break;default:bad.viewport_width=1919;break;}
        const auto rejected=Create(bad);Check(rejected.decision==expected,"strict guard decision");Check(!std::memcmp(rejected.applied.data(),bad.strength.data(),16),"rejected leaves bits alone");}
    for(const std::uint32_t bits:{0u,0x80000000u,0x3f800000u}){auto no=v;std::memcpy(no.strength.data(),&bits,4);auto p=Create(no);Check(p.decision==Decision::NoChange,"unit/zero are explicit no-change");Check(!std::memcmp(p.applied.data(),no.strength.data(),16),"no-change preserves signed0");}
    for(const std::uint32_t bits:{0x3f800001u,0xbf000000u,0x7f800000u,0x7f812345u,0x7fc12345u,0xff800000u}){auto bad=v;std::memcpy(bad.strength.data(),&bits,4);Check(Create(bad).decision==Decision::Strength,"invalid scalar rejected before FP work");}
    const auto reference=[](std::uint32_t bits){float value=0;std::memcpy(&value,&bits,4);const double exact=static_cast<double>(value)*1.25;return Bits(static_cast<float>((std::min)(1.0,exact)));};
    const unsigned saved=_mm_getcsr();_mm_setcsr(saved&~0xe040u);unsigned ignored=0;_controlfp_s(&ignored,_RC_NEAR,_MCW_RC);
    for(std::uint32_t bits=1;bits<4096;++bits)Check(ScaleBits(bits)==reference(bits),"tiny strengths correctly rounded");
    for(std::uint32_t bits=0x7ffc00u;bits<0x800400u;++bits)Check(ScaleBits(bits)==reference(bits),"subnormal/normal boundary");
    std::uint32_t state=0x7acefe31u;
    for(unsigned i=0;i<50000;++i){state=state*1664525u+1013904223u;const auto bits=state%0x3f800001u;Check(ScaleBits(bits)==reference(bits),"all-exponent exactRNE strength scale");}
    // Profile A above is unchanged. Profile B is an explicit separate bounded
    // alternative for source-observed original strengths that cross1.
    auto b=v;b.profile_version=2;
    for(float strength:{0.781615913f,0.91701299f,0.998512983f,0.999923944f,1.0f,1.00118852f,1.00244355f,1.20f}){
        b.strength[0]=strength;const auto p=Create(b);Check(p.decision==Decision::Apply,"explicitB supported strength applies");
        Check(p.applied[0]>=strength&&p.applied[0]<=1.25f,"B never attenuates supported original strength");
        Check(Bits(p.applied[0])==Bits(static_cast<float>((std::min)(1.25,static_cast<double>(strength)*1.25))),"B exact fixed scale and clamp");
        Check(!std::memcmp(p.applied.data()+1,b.strength.data()+1,12),"B scalar-only unused bits unchanged");
        for(double ao:{0.0,.1,.5,1.0}){
            const double before_ao=(std::max)(0.0,(std::min)(1.0,1.0+strength*(ao-1)));
            const double after_ao=(std::max)(0.0,(std::min)(1.0,1.0+p.applied[0]*(ao-1)));
            Check(after_ao<=before_ao,"B bounded UNORM AO never brightens source contact");
            if(ao==1.0)Check(after_ao==1.0,"B unoccluded source stays1");}
        if(strength>1){auto a=b;a.profile_version=1;Check(Create(a).decision==Decision::Strength,"A still rejects original above1");}}
    for(const std::uint32_t bits:{0u,0x80000000u,0x3fa00000u}){std::memcpy(b.strength.data(),&bits,4);const auto p=Create(b);Check(p.decision==Decision::NoChange,"B floor/ceiling honest no-op");Check(!std::memcmp(p.applied.data(),b.strength.data(),16),"B floor/ceiling preserves all bits");}
    const std::uint32_t above=0x3fa00001u;std::memcpy(b.strength.data(),&above,4);Check(Create(b).decision==Decision::Strength,"B upper nextULP unsupported");
    b=v;b.profile_version=3;Check(Create(b).decision==Decision::Profile,"unknown profile rejected");
    for(unsigned i=0;i<50000;++i){state=state*1664525u+1013904223u;const auto bits=state%0x3fa00001u;float x=0;std::memcpy(&x,&bits,4);
        const auto scaled=ScaleBits(bits,0x3fa00000u);Check(scaled>=bits&&scaled<=0x3fa00000u,"B every admitted exponent monotonic bounded");
        Check(scaled==Bits(static_cast<float>((std::min)(1.25,static_cast<double>(x)*1.25))),"B every exponent exactRNE clamp");}
    _controlfp_s(&ignored,cw,_MCW_EM|_MCW_RC);_mm_setcsr(saved);
    std::printf("{\"format\":\"s7-ssao-policy-cpu-v1\",\"checks\":%u,\"failures\":0,\"gpu\":false,\"game\":false}\n",checks);return 0;
}catch(const std::exception& e){std::fprintf(stderr,"SSAO policy failure after%u checks: %s\n",checks,e.what());return 1;}}
