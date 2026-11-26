// Authored CPU object lifetimes only; no game, graphics API or GPU access.
#define CINTERFACE
#include <windows.h>
#include <d3d9.h>
#include "water_on12_shader_cache.h"
#include "water_on12_constants.h"
#include <cstdio>
#include <cstdlib>

#define SLOT(method,index) static_assert(offsetof(IDirect3DDevice9Vtbl,method)==index*sizeof(void*),"D3D9 ABI slot changed: " #method)
SLOT(SetRenderState,57);SLOT(SetTexture,65);SLOT(SetSamplerState,69);SLOT(SetVertexShaderConstantF,94);SLOT(SetPixelShaderConstantF,109);
SLOT(SetVertexShader,92);SLOT(SetPixelShader,107);SLOT(SetStreamSource,100);SLOT(SetIndices,104);SLOT(EndScene,42);SLOT(BeginScene,41);
SLOT(GetRenderTargetData,32);SLOT(StretchRect,34);SLOT(Clear,43);
SLOT(BeginStateBlock,60);SLOT(EndStateBlock,61);
SLOT(CreateStateBlock,59);
SLOT(SetSoftwareVertexProcessing,77);
static_assert(offsetof(IDirect3DStateBlock9Vtbl,Apply)==5*sizeof(void*),"D3D9 StateBlock ABI changed");
#undef SLOT

struct Shader {
    unsigned references=1,adds=0,releases=0;
    void AddRef(){++references;++adds;}
    void Release(){if(!references)std::exit(2);--references;++releases;}
};
struct ConstantsDevice {
    float values[1024]{};unsigned reads=0;bool fail=false;
    HRESULT GetVertexShaderConstantF(UINT start,float* output,UINT count){++reads;if(fail)return E_FAIL;std::memcpy(output,values+start*4,count*16);return S_OK;}
};
int main() {
    unsigned checks=0;auto check=[&](bool value){++checks;if(!value)std::exit(1);};
    WaterOn12ShaderCache::Cache<Shader,2> cache;Shader a,b,c;bool target=false;
    check(!cache.Lookup(nullptr,target));check(!cache.Store(nullptr,true));
    check(!cache.Lookup(&a,target));check(cache.Store(&a,true));check(a.references==2&&a.adds==1);
    // External release cannot destroy or recycle a cached object's address.
    a.Release();check(a.references==1);check(cache.Lookup(&a,target)&&target);
    check(cache.Store(&a,true)&&a.adds==1);check(!cache.Store(&a,false));
    check(cache.Lookup(&a,target)&&target); // Conflicting admission cannot change it.
    check(cache.Store(&b,false));check(cache.Lookup(&b,target)&&!target);
    check(cache.Lookup(&a,target)&&target); // A collision/another last entry is safe.
    check(!cache.Lookup(&c,target));check(!cache.Store(&c,true)&&cache.count==2&&c.adds==0&&cache.full==1);
    cache.Clear();check(a.references==0&&b.references==1&&cache.count==0);
    check(!cache.Lookup(&a,target));cache.Clear();check(a.releases==2&&b.releases==1);
    check(cache.Store(&c,false)&&c.references==2);check(cache.Lookup(&c,target)&&!target);
    cache.Clear();check(c.references==1&&c.releases==1);
    ConstantsDevice device;float data[64]{};
    check(WaterOn12Constants::Same(&device,10,data,16));check(device.reads==1);
    for(const auto& shape:std::array<std::pair<UINT,UINT>,5>{{{0,0},{0,17},{256,1},{255,2},{0xffffffffu,1}}})
        check(!WaterOn12Constants::Same(&device,shape.first,data,shape.second));
    check(!WaterOn12Constants::Same(&device,0,nullptr,1)&&device.reads==1);
    device.fail=true;check(!WaterOn12Constants::Same(&device,0,data,1));device.fail=false;
    DWORD bits=0x80000000;std::memcpy(data,&bits,4);check(!WaterOn12Constants::Same(&device,0,data,1));
    std::memcpy(device.values,&bits,4);check(WaterOn12Constants::Same(&device,0,data,1));
    bits=0x7fc01234;std::memcpy(data,&bits,4);std::memcpy(device.values,&bits,4);check(WaterOn12Constants::Same(&device,0,data,1));
    bits=0x7fc01235;std::memcpy(data,&bits,4);check(!WaterOn12Constants::Same(&device,0,data,1));
    check(!WaterOn12Constants::Equal(nullptr,data,16));
    WaterOn12Constants::Hint hint;const float pair[]={1,2,3,4,5,6,7,8},other[]={9,10,11,12};
    check(!hint.Candidate(4,pair,2));hint.Remember(4,pair,2);check(hint.Candidate(4,pair,2));
    hint.Remember(5,other,1);check(!hint.Candidate(4,pair,2));check(hint.Candidate(4,pair,1));check(hint.Candidate(5,other,1));
    // A matching hint alone must never authorize elision after an external write.
    check(hint.Candidate(4,pair,1)&&!WaterOn12Constants::Same(&device,4,pair,1));
    hint.Remember(4,reinterpret_cast<const float*>(1),1);check(!hint.Candidate(4,pair,1));
    hint.Clear();check(!hint.Candidate(5,other,1));hint.Remember(256,pair,1);check(!hint.Candidate(256,pair,1));
    hint.Remember(4,pair,1);hint.Remember(0,data,17);check(!hint.Candidate(4,pair,1));
    hint.Remember(4,pair,1);hint.Remember(0,data,256);check(!hint.Candidate(4,pair,1));
    std::printf("{\"cache_cpu_checks\":%u,\"gpu_accessed\":false}\n",checks);return 0;
}
