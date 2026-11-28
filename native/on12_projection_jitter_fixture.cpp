#include "on12_projection_jitter.h"
#include "on12_projection_binding.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <cstdio>
#include <thread>

namespace {
struct Device {std::array<float,1024> values{};unsigned sets=0,gets=0;bool fail=false;};
HRESULT STDMETHODCALLTYPE Set(IDirect3DDevice9* opaque,UINT first,const float* values,UINT count){auto& d=*reinterpret_cast<Device*>(opaque);++d.sets;
    if(d.fail){d.fail=false;return E_FAIL;}if(first>=256||count>256-first||!values)return D3DERR_INVALIDCALL;std::memcpy(d.values.data()+first*4,values,count*16);return S_OK;}
HRESULT STDMETHODCALLTYPE Get(IDirect3DDevice9* opaque,UINT first,float* values,UINT count){auto& d=*reinterpret_cast<Device*>(opaque);++d.gets;
    if(first>=256||count>256-first||!values)return D3DERR_INVALIDCALL;std::memcpy(values,d.values.data()+first*4,count*16);return S_OK;}
unsigned checks=0;bool Check(bool good,unsigned line){++checks;if(!good)std::fprintf(stderr,"projection jitter check failed at %u\n",line);return good;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
}
int main(){
    Device device;auto* raw=reinterpret_cast<IDirect3DDevice9*>(&device);
    const std::array<float,16> matrix{1.5f,0,0,0,0,2,0,0,0,0,1,1,1,2,3,0};
    REQUIRE(SUCCEEDED(Set(raw,12,matrix.data(),4))&&SUCCEEDED(Set(raw,64,matrix.data(),4)));
    On12Graphics::ProjectionJitter jitter;REQUIRE(SUCCEEDED(jitter.Initialize(raw,Set,Get)));
    REQUIRE(SUCCEEDED(jitter.Begin(1,1280,720,true))&&SUCCEEDED(jitter.BeforeDraw(12,1280,720)));
    REQUIRE(std::memcmp(device.values.data()+48,matrix.data(),64)!=0);
    std::array<float,16> logical{};REQUIRE(SUCCEEDED(jitter.GetConstants(12,logical.data(),4))&&logical==matrix);
    const auto writes=device.sets;for(unsigned i=0;i<1000;++i)REQUIRE(SUCCEEDED(jitter.BeforeDraw(12,1280,720)));
    REQUIRE(device.sets==writes);
    const float update[]{0.2f,0.4f,0.6f,0.8f};REQUIRE(SUCCEEDED(jitter.SetConstants(13,update,1,false)));
    REQUIRE(SUCCEEDED(jitter.GetConstants(13,logical.data(),1))&&!std::memcmp(update,logical.data(),16));
    auto expected=matrix;std::memcpy(expected.data()+4,update,16);
    REQUIRE(SUCCEEDED(jitter.BeforeDraw(0,1280,720))&&!std::memcmp(device.values.data()+48,expected.data(),64));
    REQUIRE(SUCCEEDED(jitter.BeforeDraw(12,512,512))&&!std::memcmp(device.values.data()+48,expected.data(),64));
    REQUIRE(SUCCEEDED(jitter.BeforeDraw(64,1280,720))&&SUCCEEDED(jitter.Suspend()));
    REQUIRE(!std::memcmp(device.values.data()+256,matrix.data(),64));
    auto changed=matrix;changed[12]=9;REQUIRE(SUCCEEDED(Set(raw,12,changed.data(),4)));
    REQUIRE(SUCCEEDED(jitter.Resume())&&SUCCEEDED(jitter.BeforeDraw(12,1280,720))&&SUCCEEDED(jitter.GetConstants(12,logical.data(),4))&&logical==changed);
    REQUIRE(SUCCEEDED(jitter.Ui())&&!std::memcmp(device.values.data()+48,changed.data(),64));
    REQUIRE(SUCCEEDED(jitter.BeforeDraw(12,1280,720))&&!std::memcmp(device.values.data()+48,changed.data(),64));
    REQUIRE(SUCCEEDED(jitter.Begin(2,1280,720,true)));device.fail=true;
    REQUIRE(FAILED(jitter.BeforeDraw(12,1280,720))&&!std::memcmp(device.values.data()+48,changed.data(),64));
    REQUIRE(SUCCEEDED(jitter.BeforeDraw(12,1280,720))&&SUCCEEDED(jitter.Stop()));
    HRESULT foreign=S_OK;std::thread other([&]{foreign=jitter.BeforeDraw(12,1280,720);});other.join();REQUIRE(foreign==RPC_E_WRONG_THREAD);
    REQUIRE(!std::memcmp(device.values.data()+48,changed.data(),64));
    for(unsigned slot:{12u,64u}){
        const char* name=slot==12?"g_VSMainViewProjectionMatrix":"g_View2ClipTM";
        const auto source=std::string("float4x4 ")+name+":register(c"+std::to_string(slot)+");float4 VS(float4 p:POSITION0):POSITION0{return mul("+name+",p);}";
        Microsoft::WRL::ComPtr<ID3DBlob> code,error;
        REQUIRE(SUCCEEDED(D3DCompile(source.data(),source.size(),"authored-projection-binding",nullptr,nullptr,"VS","vs_3_0",D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error)));
        REQUIRE(On12Graphics::ProjectionRegisterForShader(code->GetBufferPointer(),code->GetBufferSize())==slot);
        REQUIRE(!On12Graphics::ProjectionRegisterForShader(code->GetBufferPointer(),code->GetBufferSize()-4));
    }
    REQUIRE(!On12Graphics::ProjectionRegisterForShader(nullptr,0));
    std::thread inactive([&]{jitter.ForeignMutation();});inactive.join();
    REQUIRE(SUCCEEDED(jitter.Begin(3,1280,720,false))&&SUCCEEDED(jitter.BeforeDraw(12,1280,720)));
    REQUIRE(SUCCEEDED(jitter.Begin(4,1280,720,true))&&SUCCEEDED(jitter.BeforeDraw(12,1280,720))&&SUCCEEDED(jitter.Stop()));
    std::array<float,1024> palette{};for(unsigned n=0;n<palette.size();++n)palette[n]=static_cast<float>(n)*0.125f;
    REQUIRE(SUCCEEDED(jitter.Begin(5,1280,720,true))&&SUCCEEDED(jitter.BeforeDraw(12,1280,720))&&SUCCEEDED(jitter.SetConstants(0,palette.data(),256,false)));
    REQUIRE(SUCCEEDED(jitter.GetConstants(0,logical.data(),4))&&!std::memcmp(logical.data(),palette.data(),64));
    REQUIRE(!std::memcmp(device.values.data()+128,palette.data()+128,(palette.size()-128)*sizeof(float)));
    REQUIRE(SUCCEEDED(jitter.GetConstants(12,logical.data(),4))&&!std::memcmp(logical.data(),palette.data()+48,64));
    REQUIRE(SUCCEEDED(jitter.Stop())&&device.values==palette);
    std::printf("{\"passed\":true,\"checks\":%u,\"cpu_only\":true,\"gpu_device_created\":false,\"logical_projection_preserved\":true,\"repeated_draws\":1000,\"redundant_uploads\":0}\n",checks);return 0;
}
