#include "on12_temporal_camera.h"
#include "on12_temporal_camera_pass.h"
#include <d3dcompiler.h>
#include <d3d12shader.h>
#include <wrl/client.h>
#include <cmath>
#include <cstdio>
#include <limits>

using namespace On12Graphics::Temporal;
using Microsoft::WRL::ComPtr;
namespace {
unsigned checks=0;
bool Check(bool value,unsigned line){++checks;if(!value)std::fprintf(stderr,"camera check failed at %u\n",line);return value;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
bool Near(float a,float b,float tolerance=0.0003f){return std::fabs(a-b)<=tolerance;}
CameraMatrix Perspective(float x=0,float y=0,float z=0,float angle=0){
    constexpr float near_z=0.1f,far_z=100;const float c=std::cos(angle),s=std::sin(angle),a=far_z/(far_z-near_z),b=-near_z*a;
    const float right[]{c,0,-s},forward[]{s,0,c};CameraMatrix m{};
    for(unsigned k=0;k<3;++k){m[k*4]=1.5f*right[k]-0.001f*forward[k];m[k*4+1]=(k==1?2.0f:0.0f)-0.002f*forward[k];m[k*4+2]=a*forward[k];m[k*4+3]=forward[k];}
    for(unsigned r=0;r<4;++r)m[12+r]=-m[r]*x-m[4+r]*y-m[8+r]*z;m[14]+=b;return m;
}
std::array<double,4> Project(const CameraMatrix& m,const std::array<double,3>& world){
    std::array<double,4> clip{};for(unsigned r=0;r<4;++r){clip[r]=m[12+r];for(unsigned c=0;c<3;++c)clip[r]+=m[c*4+r]*world[c];}return clip;
}
bool Write(const wchar_t* path,ID3DBlob* b){FILE* f=nullptr;if(_wfopen_s(&f,path,L"wb")||!f)return false;const bool ok=std::fwrite(b->GetBufferPointer(),1,b->GetBufferSize(),f)==b->GetBufferSize();std::fclose(f);return ok;}
}
int wmain(){
    constexpr unsigned width=1280,height=720;CameraParameters p;Motion motion;float depth=0;
    const auto original=Perspective();
    REQUIRE(BuildCamera(original,original,100,width,height,0,0,0,p)&&ValidCamera(p));
    // Stationary camera: no apparent motion even with jitter, off-center
    // projection and a large world-space camera position.
    for(float origin:{0.0f,0.5f})for(float j:{-0.375f,0.0f,0.25f})for(float position:{0.0f,65000.0f}){
        const auto view=Perspective(position,position*0.7f,position*0.1f,0.4f);
        REQUIRE(BuildCamera(view,view,100,width,height,j,-j,origin,p));
        for(unsigned x:{0u,1u,640u,1279u})for(unsigned y:{0u,360u,719u}){
            REQUIRE(CameraMotion(p,x,y,0.08f,1,motion,depth));
            REQUIRE(Near(motion.x,0)&&Near(motion.y,0)&&Near(motion.previous_depth,8)&&motion.confidence==1&&Near(depth,8));
        }
    }
    // Independent forward projection of known world points: lateral, vertical
    // and forward camera motion must agree with the inverse reconstruction.
    for(float origin:{0.0f,0.5f})for(float j:{-0.25f,0.375f}){
        const float cx=0.4f,cy=-0.3f,cz=0.5f,z=8;
        const auto current=Perspective(cx,cy,cz);
        REQUIRE(BuildCamera(current,original,100,width,height,j,-j,origin,p));
        for(unsigned x:{1u,320u,640u,1000u})for(unsigned y:{10u,360u,700u}){
            const double ndc_x=(x+origin-j)*2.0/width-1,ndc_y=1-(y+origin+j)*2.0/height;
            const std::array<double,3> world{cx+(ndc_x+0.001)*z/1.5,cy+(ndc_y+0.002)*z/2,cz+z};
            const auto before=Project(original,world);
            const float px=static_cast<float>((before[0]/before[3]*0.5+0.5)*width-origin);
            const float py=static_cast<float>((-before[1]/before[3]*0.5+0.5)*height-origin);
            REQUIRE(CameraMotion(p,x,y,z/100,1,motion,depth));
            REQUIRE(Near(motion.x,px-(x-j))&&Near(motion.y,py-(y+j))&&Near(motion.previous_depth,static_cast<float>(before[3])));
            REQUIRE(!Near(motion.previous_depth,depth)); // Current Z is not previous Z.
        }
    }
    // Geometry jitter changes projected sampling by the requested pixels and
    // preserves clip Z/W. Input matrix remains unchanged, including alias use.
    CameraMatrix jittered;REQUIRE(JitterCamera(original,width,height,0.25f,-0.375f,jittered));
    const std::array<double,3> world{1,2,10};const auto before=Project(original,world),after=Project(jittered,world);
    REQUIRE(Near(static_cast<float>((after[0]/after[3]-before[0]/before[3])*width/2),0.25f));
    REQUIRE(Near(static_cast<float>(-(after[1]/after[3]-before[1]/before[3])*height/2),-0.375f));
    REQUIRE(after[2]==before[2]&&after[3]==before[3]);
    auto alias=original;REQUIRE(JitterCamera(alias,width,height,0.25f,-0.375f,alias)&&alias==jittered);
    REQUIRE(CameraMotion(p,100,100,0.08f,0,motion,depth)&&motion.confidence==0&&Near(depth,8));
    REQUIRE(CameraMotion(p,100,100,0.08f,0.25f,motion,depth)&&motion.confidence==0.25f);
    REQUIRE(CameraMotion(p,100,100,0.08f,(std::numeric_limits<float>::quiet_NaN)(),motion,depth)&&motion.confidence==0);
    for(float z:{-1.0f,0.0f,1.0f,2.0f,(std::numeric_limits<float>::quiet_NaN)()})REQUIRE(!CameraMotion(p,100,100,z,1,motion,depth)&&depth==0&&motion.confidence==0);
    REQUIRE(!CameraMotion(p,width,0,0.08f,1,motion,depth));
    REQUIRE(BuildCamera(original,Perspective(0,0,20),100,width,height,0,0,0,p));
    REQUIRE(CameraMotion(p,100,100,0.08f,1,motion,depth)&&motion.confidence==0);
    auto invalid=original;invalid[0]=0;REQUIRE(!BuildCamera(invalid,original,100,width,height,0,0,0,p));
    invalid=original;invalid[3]=(std::numeric_limits<float>::quiet_NaN)();REQUIRE(!BuildCamera(invalid,original,100,width,height,0,0,0,p));
    REQUIRE(!BuildCamera(original,original,100,width,height,0,0,0.25f,p));
    REQUIRE(!BuildCamera(original,original,100,0,height,0,0,0,p));
    REQUIRE(!JitterCamera(original,width,height,1,0,jittered));
    // Shader compilation/reflection uses the real SDK on CPU, no device.
    constexpr UINT flags=D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3;
    ComPtr<ID3DBlob> vs,ps,error;HRESULT hr=D3DCompileFromFile(L"on12_temporal_camera.hlsl",nullptr,nullptr,"VS","vs_5_0",flags,0,&vs,&error);
    if(FAILED(hr)&&error)std::fprintf(stderr,"%s",static_cast<const char*>(error->GetBufferPointer()));REQUIRE(SUCCEEDED(hr));error.Reset();
    hr=D3DCompileFromFile(L"on12_temporal_camera.hlsl",nullptr,nullptr,"PS","ps_5_0",flags,0,&ps,&error);
    if(FAILED(hr)&&error)std::fprintf(stderr,"%s",static_cast<const char*>(error->GetBufferPointer()));REQUIRE(SUCCEEDED(hr));
    ComPtr<ID3D12ShaderReflection> reflection;REQUIRE(SUCCEEDED(D3DReflect(ps->GetBufferPointer(),ps->GetBufferSize(),IID_PPV_ARGS(&reflection))));
    D3D12_SHADER_BUFFER_DESC cb{};REQUIRE(SUCCEEDED(reflection->GetConstantBufferByName("CameraParameters")->GetDesc(&cb))&&cb.Size==sizeof(CameraParameters));
    D3D12_SHADER_DESC shader{};REQUIRE(SUCCEEDED(reflection->GetDesc(&shader))&&shader.OutputParameters==2);
    const char* names[]{"NormalizedDepth","StaticConfidence"};for(UINT i=0;i<2;++i){D3D12_SHADER_INPUT_BIND_DESC binding{};REQUIRE(SUCCEEDED(reflection->GetResourceBindingDescByName(names[i],&binding))&&binding.BindPoint==i&&binding.Type==D3D_SIT_TEXTURE);}
    REQUIRE(Write(L"on12_temporal_camera_vs.bin",vs.Get())&&Write(L"on12_temporal_camera_ps.bin",ps.Get()));
    ComPtr<ID3DBlob> signature;REQUIRE(SUCCEEDED(On12Graphics::CameraPass::SerializeSignature(&signature))&&signature->GetBufferSize()>0);
    std::printf("{\"passed\":true,\"checks\":%u,\"cpu_only\":true,\"gpu_device_created\":false,\"camera_bytes\":96,\"dynamic_motion_proven\":false}\n",checks);return 0;
}
