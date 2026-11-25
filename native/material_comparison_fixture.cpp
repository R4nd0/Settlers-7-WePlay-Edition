#include "material_comparison.h"
#include <bcrypt.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <float.h>
#include <intrin.h>
#include <stdexcept>
#include <vector>

namespace {
using namespace MaterialComparison;
std::atomic<unsigned> checks{0};
void Check(bool value,const char* name){++checks;if(!value)throw std::runtime_error(name);}
std::array<unsigned char,32> Hash(const std::vector<unsigned char>& bytes){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> result{};
    Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"fixture SHA provider");
    bool valid=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(valid)valid=BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)>=0;
    if(valid)valid=BCryptFinishHash(hash,result.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);Check(valid,"fixture SHA digest");return result;
}
std::vector<unsigned char> Code(DWORD version,DWORD marker){
    const DWORD words[]={version,0x02000001u,0x800f0000u,marker,0x0000ffffu};
    const auto* begin=reinterpret_cast<const unsigned char*>(words);return {begin,begin+sizeof(words)};
}
struct Shader {void** table=nullptr;ULONG refs=1;std::vector<unsigned char> code;bool throw_query=false;};
ULONG STDMETHODCALLTYPE Add(Shader* s){return ++s->refs;}
ULONG STDMETHODCALLTYPE Release(Shader* s){Check(s->refs>1,"shader ref underflow");return --s->refs;}
HRESULT STDMETHODCALLTYPE Function(Shader* s,void* out,UINT* count){
    if(s->throw_query)throw std::runtime_error("fixture query exception");
    const UINT size=static_cast<UINT>(s->code.size());if(!out){*count=size;return S_OK;}
    if(*count<size)return E_FAIL;std::memcpy(out,s->code.data(),size);*count=size;return S_OK;
}
struct Surface {void** table=nullptr;ULONG refs=1;DWORD width=1920,height=1080;D3DFORMAT format=D3DFMT_A8R8G8B8;};
ULONG STDMETHODCALLTYPE SurfaceAdd(Surface* s){return ++s->refs;}
ULONG STDMETHODCALLTYPE SurfaceRelease(Surface* s){Check(s->refs>1,"surface ref underflow");return --s->refs;}
HRESULT STDMETHODCALLTYPE Desc(Surface* s,D3DSURFACE_DESC* out){*out={};out->Width=s->width;out->Height=s->height;out->Format=s->format;return S_OK;}
struct Device {
    void** table=nullptr;Shader* vertex=nullptr;Shader* pixel=nullptr;Surface* surface=nullptr;
    std::array<float,4> ps80{};DWORD blend=0,alpha=0,srgb=0,viewport_x=0;
    bool fail_apply=false,fail_restore=false,throw_set=false,silent_set=false,throw_pixel=false;
    unsigned sets=0;
};
HRESULT STDMETHODCALLTYPE Vertex(Device* d,IDirect3DVertexShader9** out){*out=reinterpret_cast<IDirect3DVertexShader9*>(d->vertex);if(d->vertex)Add(d->vertex);return S_OK;}
HRESULT STDMETHODCALLTYPE Pixel(Device* d,IDirect3DPixelShader9** out){*out=reinterpret_cast<IDirect3DPixelShader9*>(d->pixel);if(d->pixel)Add(d->pixel);if(d->throw_pixel)throw std::runtime_error("fixture pixel getter exception");return S_OK;}
HRESULT STDMETHODCALLTYPE Target(Device* d,DWORD index,IDirect3DSurface9** out){if(index)return E_FAIL;*out=reinterpret_cast<IDirect3DSurface9*>(d->surface);SurfaceAdd(d->surface);return S_OK;}
HRESULT STDMETHODCALLTYPE Viewport(Device* d,D3DVIEWPORT9* out){*out={d->viewport_x,0,1920,1080,0,1};return S_OK;}
HRESULT STDMETHODCALLTYPE RenderState(Device* d,D3DRENDERSTATETYPE key,DWORD* out){*out=key==D3DRS_ALPHABLENDENABLE?d->blend:key==D3DRS_ALPHATESTENABLE?d->alpha:d->srgb;return S_OK;}
HRESULT STDMETHODCALLTYPE Get(Device* d,UINT index,float* out,UINT count){if(index!=80||count!=1)return E_FAIL;std::memcpy(out,d->ps80.data(),16);return S_OK;}
HRESULT STDMETHODCALLTYPE Set(Device* d,UINT index,const float* values,UINT count){
    if(index!=80||count!=1)return E_FAIL;++d->sets;std::memcpy(d->ps80.data(),values,16);
    if(d->silent_set){d->silent_set=false;d->ps80[0]=12345;return S_OK;}
    if(d->throw_set){d->throw_set=false;throw std::runtime_error("fixture setter exception after mutation");}
    if(d->fail_apply){d->fail_apply=false;return E_FAIL;}return d->fail_restore?E_FAIL:S_OK;
}
State Snapshot(){State result{};Check(ReadState(&result),"POD state read");return result;}
bool Bits(const std::array<float,4>& a,const std::array<float,4>& b){return !std::memcmp(a.data(),b.data(),16);}
DWORD WINAPI ForeignReset(void* value){Reset(static_cast<IDirect3DDevice9*>(value));return 0;}
DWORD WINAPI ForeignUnrelatedDraw(void* value){Transaction draw{};BeginDraw(static_cast<IDirect3DDevice9*>(value),&draw);EndDraw(&draw,S_OK);return draw.active?1u:0u;}
}
int main(){using namespace MaterialComparison;try{
    std::array<void*,5> shader_table{};shader_table[1]=reinterpret_cast<void*>(&Add);shader_table[2]=reinterpret_cast<void*>(&Release);shader_table[4]=reinterpret_cast<void*>(&Function);
    std::array<void*,17> surface_table{};surface_table[1]=reinterpret_cast<void*>(&SurfaceAdd);surface_table[2]=reinterpret_cast<void*>(&SurfaceRelease);surface_table[12]=reinterpret_cast<void*>(&Desc);
    std::array<void*,134> table{};table[93]=reinterpret_cast<void*>(&Vertex);table[108]=reinterpret_cast<void*>(&Pixel);table[38]=reinterpret_cast<void*>(&Target);table[48]=reinterpret_cast<void*>(&Viewport);table[58]=reinterpret_cast<void*>(&RenderState);table[110]=reinterpret_cast<void*>(&Get);table[109]=reinterpret_cast<void*>(&Set);
    Surface surface{surface_table.data()};Shader vs{shader_table.data(),1,Code(0xfffe0300u,1)},ps{shader_table.data(),1,Code(0xffff0300u,2)};
    Device d{};d.table=table.data();d.vertex=&vs;d.pixel=&ps;d.surface=&surface;d.ps80={1,2,3,-0.0f};
    auto* device=reinterpret_cast<IDirect3DDevice9*>(&d);
    Check(!Initialize(0,1080,true)&&!Initialize(8192,8192,true),"dimension bounds");
    const auto vh=Hash(vs.code),ph=Hash(ps.code);Check(FixtureIdentity(vh.data(),20,ph.data(),20),"synthetic identities only in CPU fixture build");
    SetLastError(0x37578001);Check(Initialize(1920,1080),"disabled initialization");Check(GetLastError()==0x37578001,"initialize LastError");
    Transaction draw{};BeginDraw(device,&draw);Check(!draw.active&&!d.sets&&!Snapshot().cached_refs,"off state performs no shader queries/writes");
    Config config{};config.enabled=2;Check(!Configure(&config),"enabled boolean range");config.enabled=1;config.size=16;Check(!Configure(&config),"fixed 12-byte ABI");config.size=12;config.version=2;Check(!Configure(&config),"version guard");config.version=1;
    SetLastError(0x37578002);Check(Configure(&config),"explicit diagnostic enable");Check(GetLastError()==0x37578002,"configure LastError");
    const auto first=d.ps80;BeginDraw(device,&draw);Check(draw.active&&Bits(first,d.ps80)&&!Snapshot().pinned,"first pin tentative until original success");EndDraw(&draw,E_FAIL);
    Check(!Snapshot().pinned&&Snapshot().failed_draws==1&&Bits(first,d.ps80),"failed original draw does not pin and restores");
    d.ps80={4,5,6,-0.0f};const auto pin=d.ps80;unsigned cw_before=0,cw_after=0;_controlfp_s(&cw_before,0,0);const unsigned mx_before=_mm_getcsr();
    SetLastError(0x37578003);BeginDraw(device,&draw);Check(GetLastError()==0x37578003,"Begin LastError");Check(draw.active&&!draw.unsafe_state,"matching full-context draw activates");
    Check(!Configure(&config)&&!Initialize(1920,1080,true),"configuration refuses active transaction");
    SetLastError(0x37578004);EndDraw(&draw,S_OK);Check(GetLastError()==0x37578004,"End retains original draw LastError");
    _controlfp_s(&cw_after,0,0);Check(cw_before==cw_after&&mx_before==_mm_getcsr(),"FPU/SSE controls preserved");
    Check(Snapshot().pinned&&Bits(pin,Snapshot().pinned_ps80)&&Bits(pin,d.ps80),"first successful draw commits exact float4 including signed zero");
    for(unsigned candidate=0;candidate<2;++candidate){d.ps80={10.0f+candidate,20,30,40};const auto original=d.ps80;const unsigned before=d.sets;
        BeginDraw(device,&draw);Check(draw.active&&Bits(pin,d.ps80)&&d.sets==before+1,"both control and candidate set pin on every matching draw");EndDraw(&draw,candidate?E_FAIL:S_OK);Check(Bits(original,d.ps80)&&!draw.unsafe_state,"success/failure restoration bitwise");}
    Check(vs.refs==2&&ps.refs==2&&Snapshot().cached_refs==2,"shader identity cache retains refs to prevent pointer reuse");EndFrame(device);
    Check(vs.refs==1&&ps.refs==1&&!Snapshot().cached_refs&&Snapshot().pinned,"EndFrame releases identities and keeps pin");
    Check(Snapshot().frames==1&&Snapshot().completed_frame_applications==4&&Snapshot().completed_frame_restorations==4
        &&Snapshot().last_applied_frame==1&&Snapshot().pin_committed_frame==1,"completed frame counters bind actual applications/restorations");
    EndFrame(device);Check(Snapshot().frames==2&&!Snapshot().completed_frame_applications&&Snapshot().last_applied_frame==1,"no-geometry frame never reuses application proof");
    const DWORD nan=0x7fc01234;std::memcpy(d.ps80.data(),&nan,4);const unsigned nonfinite_sets=d.sets;BeginDraw(device,&draw);Check(!draw.active&&d.sets==nonfinite_sets&&Snapshot().input_rejections==1,"nonfinite original PS80 refused");d.ps80={11,12,13,14};
    for(unsigned guard=0;guard<5;++guard){d.blend=guard==0;d.alpha=guard==1;d.srgb=guard==2;d.viewport_x=guard==3;surface.width=guard==4?1280u:1920u;BeginDraw(device,&draw);Check(!draw.active,"full original context mandatory");}
    d.blend=d.alpha=d.srgb=d.viewport_x=0;surface.width=1920;EndFrame(device);
    ps.code[8]^=1;BeginDraw(device,&draw);Check(!draw.active,"same address with changed shader bytes reclassified after EndFrame");EndFrame(device);ps.code[8]^=1;
    BeginDraw(device,&draw);EndDraw(&draw,S_OK);const State pre_reset=Snapshot();HANDLE thread=CreateThread(nullptr,0,&ForeignReset,device,0,nullptr);Check(thread!=nullptr,"foreign cleanup thread");Check(WaitForSingleObject(thread,3000)==WAIT_OBJECT_0,"foreign cleanup returned");CloseHandle(thread);
    Check(!Snapshot().cached_refs&&vs.refs==1&&ps.refs==1&&Snapshot().pinned&&Bits(pin,Snapshot().pinned_ps80),"Reset on foreign thread drops refs, retains exact pin");
    Check(Snapshot().frame_applications==pre_reset.frame_applications&&Snapshot().frame_restorations==pre_reset.frame_restorations
        &&Snapshot().completed_frame_applications==pre_reset.completed_frame_applications&&Snapshot().last_applied_frame==pre_reset.last_applied_frame,
        "routine Release cleanup preserves recorded/current frame evidence");
    SetLastError(0x37578007);InvalidateFrame();Check(GetLastError()==0x37578007,"invalidate LastError");
    Check(!Snapshot().frame_applications&&!Snapshot().frame_restorations&&!Snapshot().completed_frame_applications
        &&!Snapshot().completed_frame_restorations&&!Snapshot().last_applied_frame&&Snapshot().pinned,
        "actual context reset invalidates frame evidence but retains comparison pin");
    config.enabled=0;Check(Configure(&config),"disable boundary");const unsigned disabled_sets=d.sets;BeginDraw(device,&draw);Check(!draw.active&&d.sets==disabled_sets&&!Snapshot().pinned,"disable clears pin and preserves ordinary draw");
    config.enabled=1;Check(Configure(&config),"re-enable fresh pin");d.ps80={15,16,17,18};d.fail_apply=true;const auto rollback=d.ps80;BeginDraw(device,&draw);
    Check(!draw.active&&!draw.unsafe_state&&Bits(rollback,d.ps80)&&Snapshot().faulted&&!Snapshot().pinned,"failed setter after mutation rolls back without pin");
    Check(Configure(&config),"explicit reconfigure after safe fault");d.throw_set=true;BeginDraw(device,&draw);Check(!draw.active&&!draw.unsafe_state&&Bits(rollback,d.ps80)&&Snapshot().faulted,"setter exception after mutation rolls back");
    Check(Configure(&config),"fresh after exception");d.silent_set=true;BeginDraw(device,&draw);Check(!draw.active&&!draw.unsafe_state&&Bits(rollback,d.ps80)&&Snapshot().set_failures>=2,"silent setter mismatch detected/restored");
    Check(Configure(&config),"fresh restore-failure test");BeginDraw(device,&draw);Check(draw.active,"restoration failure transaction");d.fail_restore=true;EndDraw(&draw,S_OK);
    Check(draw.unsafe_state&&Snapshot().unsafe_state&&Snapshot().faulted&&!Snapshot().pinned,"restoration failure is explicit, no false pin or fallback");
    Check(!Configure(&config),"unsafe state cannot be discarded by reconfigure");Shutdown();Check(Snapshot().initialized==1,"unsafe Shutdown refuses state destruction");
    d.fail_restore=false;SetLastError(0x37578005);Check(RetryRestore(&draw)&&!draw.unsafe_state&&Bits(rollback,d.ps80),"immediate retry restores original");Check(GetLastError()==0x37578005,"retry LastError");
    Check(Configure(&config),"fresh after recovered unsafe state");d.throw_pixel=true;BeginDraw(device,&draw);d.throw_pixel=false;Check(!draw.active&&vs.refs==1&&ps.refs==1&&Snapshot().faulted,"getter exception releases both acquired COM refs");
    Check(Configure(&config),"fresh final pin");BeginDraw(device,&draw);EndDraw(&draw,S_OK);Check(Configure(&config)&&!Snapshot().pinned,"enabled-to-enabled explicit reconfigure resets pin");
    vs.throw_query=true;BeginDraw(device,&draw);vs.throw_query=false;
    Check(!draw.active&&vs.refs==1&&ps.refs==1&&Snapshot().faulted,"shader GetFunction exception releases uncached query references");
    Check(Configure(&config),"fresh after identity query exception");
    ps.code.resize(3744);BeginDraw(device,&draw);Check(!draw.active,"unknown oversized program never read into bounded scratch");EndFrame(device);ps.code=Code(0xffff0300u,2);
    std::array<Shader,130> many{};
    for(auto& shader:many){shader.table=shader_table.data();shader.code=vs.code;d.vertex=&shader;BeginDraw(device,&draw);if(draw.active)EndDraw(&draw,S_OK);}
    Check(Snapshot().cached_refs<=128&&Snapshot().cache_overflows>0,"identity cache hard bound skips excess objects");
    EndFrame(device);for(const auto& shader:many)Check(shader.refs==1,"bounded cache releases all distinct original identities");d.vertex=&vs;
    EndFrame(device);Reset(device);SetLastError(0x37578006);Shutdown();Check(GetLastError()==0x37578006,"shutdown LastError");Check(!Snapshot().initialized&&!Snapshot().pinned&&!Snapshot().cached_refs&&vs.refs==1&&ps.refs==1&&surface.refs==1,"shutdown fully clears bounded state/references");
    // Large negative population at the REAL original byte sizes. Bodies are
    // synthetic opaque GetFunction fixtures, never original shader bytes.
    auto large_vertex=Code(0xfffe0300u,31),large_pixel=Code(0xffff0300u,32);
    large_vertex.resize(2592);large_pixel.resize(3740);
    const auto large_vh=Hash(large_vertex),large_ph=Hash(large_pixel);
    Check(FixtureIdentity(large_vh.data(),2592,large_ph.data(),3740),"actual-size synthetic original identity");
    Check(Initialize(1920,1080,true),"fresh late-River regression");
    Shader late_vs{shader_table.data(),1,large_vertex},late_ps{shader_table.data(),1,large_pixel};
    std::array<Shader,129> negatives_vs{},negatives_ps{};
    for(size_t i=0;i<negatives_vs.size();++i){
        auto& v=negatives_vs[i];auto& p=negatives_ps[i];v.table=p.table=shader_table.data();v.code=large_vertex;p.code=large_pixel;
        const DWORD marker=static_cast<DWORD>(i+1);std::memcpy(v.code.data()+100,&marker,4);std::memcpy(p.code.data()+100,&marker,4);
    }
    d.vertex=&negatives_vs[0];d.pixel=&negatives_ps[0];d.ps80={31,32,33,34};
    thread=CreateThread(nullptr,0,&ForeignUnrelatedDraw,device,0,nullptr);Check(thread!=nullptr,"first unrelated shader thread");
    Check(WaitForSingleObject(thread,3000)==WAIT_OBJECT_0,"unrelated first-thread draw returned");CloseHandle(thread);
    for(size_t i=0;i<negatives_vs.size();++i){d.vertex=&negatives_vs[i];d.pixel=&negatives_ps[i];BeginDraw(device,&draw);Check(!draw.active,"same-size wrong hash never activates");}
    Check(!Snapshot().cached_refs&&!Snapshot().cache_overflows&&!Snapshot().pinned,"129 wrong pairs cannot consume identity capacity or pin");
    d.vertex=&late_vs;d.pixel=&late_ps;const auto late_pin=d.ps80;BeginDraw(device,&draw);
    Check(draw.active,"valid late River matches after129 actual-size wrong pairs and foreign-thread noise");EndDraw(&draw,S_OK);
    Check(Snapshot().pinned&&Bits(late_pin,Snapshot().pinned_ps80)&&Snapshot().cached_refs==2,"late real pair commits pin and retains only two positive refs");
    EndFrame(device);Reset(device);Shutdown();
    for(size_t i=0;i<negatives_vs.size();++i)Check(negatives_vs[i].refs==1&&negatives_ps[i].refs==1,"negative identities never retain COM refs");
    Check(late_vs.refs==1&&late_ps.refs==1,"late matched identities cleaned up");
    std::printf("{\"format\":\"s7-material-comparison-cpu-v1\",\"checks\":%u,\"passed\":true,\"gpu_started\":false,\"game_started\":false}\n",checks.load());return 0;
}catch(const std::exception& e){std::fprintf(stderr,"comparison CPU fixture failed: %s\n",e.what());return 1;}}
