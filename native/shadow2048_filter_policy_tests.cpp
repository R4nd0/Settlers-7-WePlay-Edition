#include "shadow2048_filter_policy.h"
#include <cstdio>
namespace {
unsigned checks=0,failures=0;void Check(bool value){++checks;if(!value)++failures;}
void Hex(const char* s,std::array<unsigned char,32>& out){const auto d=[](char c){return c<='9'?c-'0':c-'a'+10;};for(unsigned i=0;i<32;++i)out[i]=static_cast<unsigned char>((d(s[2*i])<<4)|d(s[2*i+1]));}
Shadow2048Filter::Input Eligible(){using namespace Shadow2048Filter;Input i;i.enabled=i.observe_authorized=i.explicit_lab_request=i.nonzero_geometry=true;i.observation_mask=31;
    Hex("7744611870305c0b6b9a330010cda3599c34080774b7f11bef367fdcd21eb698",i.vertex_sha);Hex("6be44bdf12ff3d9b94cadc9fed076c55e26207d4a4233120e5c073a76dcd38ea",i.pixel_sha);
    i.vertex_bytes=856;i.pixel_bytes=2216;i.map_type=D3DRTYPE_TEXTURE;i.map_format=D3DFMT_D16;i.map_width=i.map_height=2048;i.map_levels=1;
    i.render_width=i.target_width=i.viewport_width=3840;i.render_height=i.target_height=i.viewport_height=2160;i.target_format=D3DFMT_A8R8G8B8;
    i.stencil_enable=1;i.stencil_function=D3DCMP_EQUAL;i.stencil_reference=3;i.stencil_fail=i.stencil_zfail=i.stencil_pass=D3DSTENCILOP_KEEP;i.color_write=15;
    i.inverse={1.f/2048,1.f/2048,0.f,-0.f};i.radius={2.75f,2.75f,0,0};const std::uint32_t fade[4]={0xc2f9fffdu,0x407ffffeu,0xc279fffdu,0x407ffffeu};std::memcpy(i.fade.data(),fade,16);i.kernel=ObservedKernel;return i;}
}
int main(){using namespace Shadow2048Filter;Check(Create(Input{}).decision==Decision::Disabled);auto i=Eligible();auto p=Create(i);Check(p.decision==Decision::Apply&&p.pixel_constant==57);Check(Bits(p.applied[0])==0x3a600000u&&Bits(p.applied[1])==0x3a600000u);Check(!std::memcmp(p.original.data(),i.inverse.data(),16));Check(!std::memcmp(p.applied.data()+2,i.inverse.data()+2,8));
    for(unsigned bit=0;bit<5;++bit){i=Eligible();i.observation_mask^=1u<<bit;Check(Create(i).decision==Decision::MissingObservation);}
    i=Eligible();i.enabled=false;Check(Create(i).decision==Decision::Disabled);i=Eligible();i.observe_authorized=false;Check(Create(i).decision==Decision::NotAuthorized);
    i=Eligible();i.pixel_sha[0]^=1;Check(Create(i).decision==Decision::Program);
    for(unsigned size:{4096u,8192u}){i=Eligible();i.map_width=i.map_height=size;Check(Create(i).decision==Decision::Map);}
    i=Eligible();i.map_format=D3DFMT_R32F;Check(Create(i).decision==Decision::Map);i=Eligible();i.map_levels=2;Check(Create(i).decision==Decision::Map);
    i=Eligible();i.viewport_width=1920;Check(Create(i).decision==Decision::Context);i=Eligible();i.stencil_reference=1;Check(Create(i).decision==Decision::State);
    i=Eligible();i.depth_write=1;Check(Create(i).decision==Decision::State);i=Eligible();i.radius[1]=3;Check(Create(i).decision==Decision::Radius);
    i=Eligible();i.fade[0]=-250;Check(Create(i).decision==Decision::Fade);i=Eligible();i.kernel[7]^=1;Check(Create(i).decision==Decision::Kernel);
    i=Eligible();i.inverse[0]=1.f/4096;Check(Create(i).decision==Decision::Inverse);i=Eligible();const std::uint32_t nan=0x7fc12345u;std::memcpy(&i.inverse[3],&nan,4);p=Create(i);Check(p.decision==Decision::Apply&&Bits(p.applied[3])==nan);
    std::printf("{\"format\":\"s7-shadow2048-private-policy-cpu-v1\",\"checks\":%u,\"failures\":%u,\"gpu_started\":false,\"game_started\":false}\n",checks,failures);return failures?1:0;}
