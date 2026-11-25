#include "shadow_filter_effects.h"
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <float.h>
#include <intrin.h>
#include <stdexcept>
#include <vector>

namespace {
using namespace ShadowFilterEffects;
unsigned checks=0;
void Check(bool valid,const char* message){++checks;if(!valid)throw std::runtime_error(message);}
std::array<unsigned char,32> Hash(const std::vector<unsigned char>& code){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> result{};
    Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA provider");
    Check(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"SHA instance");
    Check(BCryptHashData(hash,const_cast<PUCHAR>(code.data()),static_cast<ULONG>(code.size()),0)>=0,"SHA bytes");
    Check(BCryptFinishHash(hash,result.data(),32,0)>=0,"SHA finish");
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);return result;
}
struct Shader {void** table=nullptr;ULONG refs=1;std::vector<unsigned char> code;bool throw_function=false;};
struct Surface {void** table=nullptr;ULONG refs=1;DWORD width=1920,height=1080;D3DFORMAT format=D3DFMT_A8R8G8B8;bool throw_desc=false;};
struct Texture {void** table=nullptr;ULONG refs=1;DWORD width=2048,height=2048,levels=1;D3DFORMAT format=D3DFMT_D16;};
struct Device {
    void** table=nullptr;Shader* vertex=nullptr;Shader* pixel=nullptr;Surface* target=nullptr;Texture* texture=nullptr;
    std::array<std::array<float,4>,256> constants{};std::array<float,4> original{};
    DWORD viewport_x=0,blend=0,stencil_reference=3,failed_state=0;
    DWORD failed_sampler=0;
    bool fail_apply=false,throw_apply=false,fail_restore=false,throw_restore=false,silent_apply=false;
    bool throw_vertex=false,throw_pixel=false,throw_target=false,throw_texture=false;
    bool fail_uniform=false,wrong_readback=false;
    unsigned setters=0,draws=0;
};
void Clobber(){SetLastError(0x17491234);unsigned ignored=0;_controlfp_s(&ignored,_RC_UP,_MCW_RC);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_UP);}
ULONG STDMETHODCALLTYPE AddShader(Shader* p){Check(p->refs>0,"shader alive AddRef");return ++p->refs;}
ULONG STDMETHODCALLTYPE ReleaseShader(Shader* p){Check(p->refs>0,"shader refs balanced");return --p->refs;}
HRESULT STDMETHODCALLTYPE Function(Shader* p,void* bytes,UINT* count){
    Clobber();if(p->throw_function)throw std::runtime_error("GetFunction");
    if(!bytes){*count=static_cast<UINT>(p->code.size());return S_OK;}
    if(*count<p->code.size())return E_FAIL;std::memcpy(bytes,p->code.data(),p->code.size());*count=static_cast<UINT>(p->code.size());return S_OK;
}
ULONG STDMETHODCALLTYPE AddSurface(Surface* p){return ++p->refs;}
ULONG STDMETHODCALLTYPE ReleaseSurface(Surface* p){Check(p->refs>1,"target refs balanced");return --p->refs;}
HRESULT STDMETHODCALLTYPE Desc(Surface* p,D3DSURFACE_DESC* desc){
    if(p->throw_desc)throw std::runtime_error("GetDesc");*desc={};desc->Width=p->width;desc->Height=p->height;desc->Format=p->format;return S_OK;
}
ULONG STDMETHODCALLTYPE AddTexture(Texture* p){return ++p->refs;}
ULONG STDMETHODCALLTYPE ReleaseTexture(Texture* p){Check(p->refs>1,"texture refs balanced");return --p->refs;}
D3DRESOURCETYPE STDMETHODCALLTYPE Type(Texture*){return D3DRTYPE_TEXTURE;}
DWORD STDMETHODCALLTYPE Levels(Texture* p){return p->levels;}
HRESULT STDMETHODCALLTYPE LevelDesc(Texture* p,UINT level,D3DSURFACE_DESC* desc){
    if(level)return E_FAIL;*desc={};desc->Width=p->width;desc->Height=p->height;desc->Format=p->format;return S_OK;
}
HRESULT STDMETHODCALLTYPE Vertex(Device* d,IDirect3DVertexShader9** output){
    *output=reinterpret_cast<IDirect3DVertexShader9*>(d->vertex);if(d->vertex)AddShader(d->vertex);
    if(d->throw_vertex){d->throw_vertex=false;throw std::runtime_error("GetVS after out-ref");}return S_OK;
}
HRESULT STDMETHODCALLTYPE Pixel(Device* d,IDirect3DPixelShader9** output){
    *output=reinterpret_cast<IDirect3DPixelShader9*>(d->pixel);if(d->pixel)AddShader(d->pixel);
    if(d->throw_pixel){d->throw_pixel=false;throw std::runtime_error("GetPS after out-ref");}return S_OK;
}
HRESULT STDMETHODCALLTYPE Target(Device* d,DWORD index,IDirect3DSurface9** output){
    if(index)return E_FAIL;*output=reinterpret_cast<IDirect3DSurface9*>(d->target);AddSurface(d->target);
    if(d->throw_target){d->throw_target=false;throw std::runtime_error("GetRT after out-ref");}return S_OK;
}
HRESULT STDMETHODCALLTYPE GetTexture(Device* d,DWORD index,IDirect3DBaseTexture9** output){
    if(index!=1)return E_FAIL;*output=reinterpret_cast<IDirect3DBaseTexture9*>(d->texture);AddTexture(d->texture);
    if(d->throw_texture){d->throw_texture=false;throw std::runtime_error("GetTexture after out-ref");}return S_OK;
}
HRESULT STDMETHODCALLTYPE Viewport(Device* d,D3DVIEWPORT9* output){*output={d->viewport_x,0,1920,1080,0,1};return S_OK;}
HRESULT STDMETHODCALLTYPE RenderState(Device* d,D3DRENDERSTATETYPE key,DWORD* output){
    if(d->failed_state==static_cast<DWORD>(key)){*output=0;return E_FAIL;}
    switch(key){
        case D3DRS_ALPHABLENDENABLE:*output=d->blend;break;
        case D3DRS_STENCILENABLE:*output=1;break;
        case D3DRS_STENCILFUNC:*output=D3DCMP_EQUAL;break;
        case D3DRS_STENCILREF:*output=d->stencil_reference;break;
        case D3DRS_STENCILFAIL:case D3DRS_STENCILZFAIL:case D3DRS_STENCILPASS:*output=D3DSTENCILOP_KEEP;break;
        case D3DRS_COLORWRITEENABLE:*output=15;break;
        case D3DRS_STENCILMASK:case D3DRS_STENCILWRITEMASK:*output=0xffffffffu;break;
        case D3DRS_CULLMODE:*output=D3DCULL_NONE;break;
        default:*output=0;break;
    }return S_OK;
}
HRESULT STDMETHODCALLTYPE SamplerState(Device* d,DWORD sampler,D3DSAMPLERSTATETYPE key,DWORD* output){
    if(sampler!=1||static_cast<DWORD>(key)==d->failed_sampler){*output=0xaaaaaaaa;return E_FAIL;}
    *output=(key==D3DSAMP_MINFILTER||key==D3DSAMP_MAGFILTER)?D3DTEXF_LINEAR:key==D3DSAMP_MIPFILTER?D3DTEXF_NONE:D3DTADDRESS_CLAMP;return S_OK;
}
HRESULT STDMETHODCALLTYPE GetConstants(Device* d,UINT index,float* output,UINT count){
    if(index+count>d->constants.size()||d->fail_uniform)return E_FAIL;
    std::memcpy(output,d->constants[index].data(),count*16);
    if(index==57&&d->wrong_readback){output[0]=0;d->wrong_readback=false;}return S_OK;
}
HRESULT STDMETHODCALLTYPE SetConstants(Device* d,UINT index,const float* input,UINT count){
    Clobber();++d->setters;if(index!=57||count!=1)return E_FAIL;
    const bool original=!std::memcmp(input,d->original.data(),16);
    if(original&&d->fail_restore)return E_FAIL;
    if(!original&&d->silent_apply){d->silent_apply=false;return S_OK;}
    std::memcpy(d->constants[57].data(),input,16);
    if(original&&d->throw_restore){d->throw_restore=false;throw std::runtime_error("restore after write");}
    if(!original&&d->throw_apply){d->throw_apply=false;throw std::runtime_error("apply after write");}
    if(!original&&d->fail_apply){d->fail_apply=false;return E_FAIL;}return S_OK;
}
void Constants(Device& d){
    d.constants={};d.constants[33]={2.75f,2.75f,0,0};
    std::memcpy(d.constants[34].data(),Shadow2048Filter::ObservedKernel.data(),128);
    const std::uint32_t fade[4]={0xc2f9fffdu,0x407ffffeu,0xc279fffdu,0x407ffffeu};std::memcpy(d.constants[47].data(),fade,16);
    const std::uint32_t inverse[4]={0x3a000000u,0x3a000000u,0x80000000u,0x7fc12345u};std::memcpy(d.constants[57].data(),inverse,16);d.original=d.constants[57];
}
Stats Snapshot(){Stats s;Check(ReadStats(&s),"stats exact POD");return s;}
void Start(){Config c;Check(Configure(&c),"disable/config epoch");c.enabled=1;Check(Configure(&c),"enable fixed profile");}
DWORD WINAPI ForeignDraw(void* value){DrawTransaction tx;BeginDraw(static_cast<IDirect3DDevice9*>(value),&tx);return tx.active||tx.unsafe_state?1:0;}
DWORD WINAPI ForeignRecovery(void* value){return RetryPendingRestore(static_cast<IDirect3DDevice9*>(value))?1:0;}
void Thread(DWORD(WINAPI* procedure)(void*),void* context){HANDLE thread=CreateThread(nullptr,0,procedure,context,0,nullptr);Check(thread!=nullptr,"fixture thread");Check(WaitForSingleObject(thread,5000)==WAIT_OBJECT_0,"fixture thread done");DWORD code=1;Check(GetExitCodeThread(thread,&code)&&code==0,"foreign operation refused");CloseHandle(thread);}
__declspec(noinline) void Expired(Device& d,IDirect3DDevice9* api){DrawTransaction tx;BeginDraw(api,&tx);Check(tx.active,"pending active");d.fail_restore=true;EndDraw(&tx,S_OK);Check(tx.unsafe_state,"expired caller backup durable");}
__declspec(noinline) void Overwrite(){volatile unsigned char bytes[4096]{};for(size_t i=0;i<sizeof(bytes);++i)bytes[i]=static_cast<unsigned char>(i);}
}
int main(){try{
    std::array<void*,5> shader_table{};shader_table[1]=reinterpret_cast<void*>(&AddShader);shader_table[2]=reinterpret_cast<void*>(&ReleaseShader);shader_table[4]=reinterpret_cast<void*>(&Function);
    std::array<void*,17> surface_table{};surface_table[1]=reinterpret_cast<void*>(&AddSurface);surface_table[2]=reinterpret_cast<void*>(&ReleaseSurface);surface_table[12]=reinterpret_cast<void*>(&Desc);
    std::array<void*,22> texture_table{};texture_table[1]=reinterpret_cast<void*>(&AddTexture);texture_table[2]=reinterpret_cast<void*>(&ReleaseTexture);texture_table[10]=reinterpret_cast<void*>(&Type);texture_table[13]=reinterpret_cast<void*>(&Levels);texture_table[17]=reinterpret_cast<void*>(&LevelDesc);
    std::array<void*,119> table{};table[93]=reinterpret_cast<void*>(&Vertex);table[108]=reinterpret_cast<void*>(&Pixel);table[38]=reinterpret_cast<void*>(&Target);table[48]=reinterpret_cast<void*>(&Viewport);table[58]=reinterpret_cast<void*>(&RenderState);table[64]=reinterpret_cast<void*>(&GetTexture);table[68]=reinterpret_cast<void*>(&SamplerState);table[109]=reinterpret_cast<void*>(&SetConstants);table[110]=reinterpret_cast<void*>(&GetConstants);
    Shader vs{shader_table.data(),1,std::vector<unsigned char>(856,0x34)},ps{shader_table.data(),1,std::vector<unsigned char>(2216,0x57)};
    const auto vh=Hash(vs.code),ph=Hash(ps.code);Check(FixtureIdentities(vh.data(),ph.data()),"test-only identities");
    Check(!Initialize(1920,1080,false)&&!Initialize(4096,2160,true),"authorization and dimensions");
    SetLastError(0x3717);Check(Initialize(1920,1080,true),"authorized disabled init");Check(GetLastError()==0x3717,"init LastError");
    Check(!FixtureIdentities(vh.data(),ph.data()),"identity locked initialized");
    Config config;config.enabled=1;Config bad=config;bad.enabled=2;Check(!Configure(&bad),"strict boolean");bad=config;bad.version=2;Check(!Configure(&bad),"strict version");bad=config;bad.size=16;Check(!Configure(&bad),"fixed12");
    Check(Configure(&config),"own-render-thread configure");
    Surface target{surface_table.data()};Texture texture{texture_table.data()};Device d;d.table=table.data();d.vertex=&vs;d.pixel=&ps;d.target=&target;d.texture=&texture;Constants(d);
    auto* api=reinterpret_cast<IDirect3DDevice9*>(&d);DrawTransaction tx;
    unsigned before=0;_controlfp_s(&before,0,0);const unsigned mx=_mm_getcsr();SetLastError(0x7317);
    BeginDraw(api,&tx);Check(tx.active&&!tx.unsafe_state,"eligible changed");Check(GetLastError()==0x7317,"begin LastError");
    Check(Shadow2048Filter::Bits(d.constants[57][0])==0x3a600000u,"fixed1.75 inverse");Check(!std::memcmp(d.constants[57].data()+2,d.original.data()+2,8),"z/w bitwise including signed0/NaN");
    Check(!RetryPendingRestore(api),"healthy active cannot recover behind caller");
    SetLastError(0x8317);EndDraw(&tx,S_OK);Check(GetLastError()==0x8317,"original postdraw LastError");Check(!tx.active&&!tx.unsafe_state&&!std::memcmp(d.constants[57].data(),d.original.data(),16),"bitwise restored");
    unsigned after=0;_controlfp_s(&after,0,0);Check(before==after&&mx==_mm_getcsr(),"x87/SSE control preserved");
    EndFrame(api);auto stats=Snapshot();Check(stats.completed_frame==1&&stats.completed_applications==1&&stats.completed_restorations==1,"same-frame proof");
    Check(stats.sampler_available==31&&stats.sampler_min==D3DTEXF_LINEAR&&stats.sampler_mag==D3DTEXF_LINEAR,"sampler filtering provenance");
    Check(stats.last_original_bits[3]==0x7fc12345u&&stats.last_applied_bits[3]==0x7fc12345u,"raw bitword provenance");
    Check(Reset(api),"ordinary Release cleanup");Check(Snapshot().completed_applications==1,"Release keeps copied frame proof");
    Check(vs.refs==1&&ps.refs==1&&target.refs==1&&texture.refs==1,"COM refs balanced after frame");
    Thread(ForeignDraw,api);
    // No bad getter/context is assumed zero or silently accepted.
    for(unsigned mode=0;mode<10;++mode){Start();Constants(d);const unsigned sets=d.setters;
        switch(mode){case 0:texture.width=4096;break;case 1:texture.height=8192;break;case 2:texture.levels=2;break;case 3:d.viewport_x=1;break;case 4:d.blend=1;break;case 5:d.stencil_reference=1;break;case 6:d.failed_state=D3DRS_ZENABLE;break;case 7:d.constants[33][0]=3;break;case 8:d.constants[34][0]=0;break;default:d.fail_uniform=true;break;}
        BeginDraw(api,&tx);Check(!tx.active&&!tx.unsafe_state&&sets==d.setters,"ineligible untouched");
        texture.width=texture.height=2048;texture.levels=1;d.viewport_x=d.blend=d.failed_state=0;d.stencil_reference=3;d.fail_uniform=false;
    }
    Start();Constants(d);d.failed_sampler=D3DSAMP_MINFILTER;const unsigned no_sets=d.setters;BeginDraw(api,&tx);
    Check(!tx.active&&d.setters==no_sets&&Snapshot().sampler_available==30,"partial sampler read never enables candidate");d.failed_sampler=0;
    // Every partial/failed apply still restores original state.
    for(unsigned mode=0;mode<3;++mode){Start();Constants(d);if(mode==0)d.fail_apply=true;else if(mode==1)d.throw_apply=true;else d.silent_apply=true;
        BeginDraw(api,&tx);Check(!tx.active&&!tx.unsafe_state&&!std::memcmp(d.constants[57].data(),d.original.data(),16),"failed setter transaction restored");Check(Snapshot().faulted==1,"failed apply faults fixed effect");
    }
    Start();Constants(d);BeginDraw(api,&tx);Check(tx.active,"failed original path active");EndDraw(&tx,E_FAIL);Check(!tx.active&&Snapshot().failed_draws>0,"original failed draw restored");
    // Restore failure retains original float4 after caller lifetime and blocks cleanup.
    Start();Constants(d);Expired(d,api);Overwrite();Check(!Reset(api)&&!Shutdown()&&!Configure(&config),"unsafe state blocks lifecycle");
    Thread(ForeignRecovery,api);d.fail_restore=false;Check(RetryPendingRestore(api),"durable owner-thread recovery");Check(!std::memcmp(d.constants[57].data(),d.original.data(),16),"durable original restored");
    Start();Constants(d);BeginDraw(api,&tx);d.throw_restore=true;EndDraw(&tx,S_OK);Check(tx.unsafe_state,"throw after restore remains unsafe until verified retry");Check(RetryRestore(&tx)&&!tx.unsafe_state,"same callback retry verifies original");
    // Getter exceptions after producing COM out refs must never leak.
    for(unsigned mode=0;mode<4;++mode){Start();Constants(d);if(mode==0)d.throw_vertex=true;else if(mode==1)d.throw_pixel=true;else if(mode==2)d.throw_target=true;else d.throw_texture=true;
        BeginDraw(api,&tx);Check(!tx.active&&!tx.unsafe_state&&Snapshot().faulted==1,"getter exception faults without mutation");Check(Reset(api),"exception cleanup");Check(vs.refs==1&&ps.refs==1&&target.refs==1&&texture.refs==1,"out refs recovered");
    }
    // Same-size negative shaders cannot fill the positive cache before a late valid pair.
    Start();Constants(d);std::vector<Shader> wrong;wrong.reserve(129);
    for(unsigned n=0;n<129;++n){wrong.push_back({shader_table.data(),1,std::vector<unsigned char>(856,static_cast<unsigned char>(n))});wrong.back().code[0]=0xff;d.vertex=&wrong.back();BeginDraw(api,&tx);Check(!tx.active,"negative stays negative");}
    d.vertex=&vs;BeginDraw(api,&tx);Check(tx.active,"late valid after129 same-size negatives");EndDraw(&tx,S_OK);Check(Reset(api),"drop late valid refs");
    // Full positive cache cannot suppress a fresh valid dynamic shader.
    Start();std::vector<Shader> positives;positives.reserve(40);
    for(unsigned n=0;n<40;++n){positives.push_back({shader_table.data(),1,vs.code});d.vertex=&positives.back();BeginDraw(api,&tx);Check(tx.active,"valid after full positive cache");EndDraw(&tx,S_OK);}
    Check(Snapshot().cached_refs<=32&&Snapshot().cache_bypasses>0,"bounded positive cache bypass counted");Check(Reset(api),"full cache cleanup");for(auto& shader:positives)Check(shader.refs==1,"dynamic refs balanced");d.vertex=&vs;
    Check(InvalidateFrame()&&Snapshot().completed_applications==0,"actual Reset invalidates proof");
    Check(Shutdown(),"safe shutdown");Check(vs.refs==1&&ps.refs==1&&target.refs==1&&texture.refs==1,"final refs balanced");
    std::printf("{\"format\":\"s7-shadow2048-runtime-cpu-v1\",\"checks\":%u,\"failures\":0,\"stats_bytes\":%zu,\"gpu\":false,\"game\":false}\n",checks,sizeof(Stats));return 0;
}catch(const std::exception& error){std::fprintf(stderr,"CPU failure after%u checks: %s\n",checks,error.what());return 1;}}
