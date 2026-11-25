#include "water_shader_effects.h"
#include <bcrypt.h>
#include <atomic>
#include <cstdio>
#include <cstring>
#include <float.h>
#include <intrin.h>
#include <memory>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace WaterShaderEffects;
std::atomic<unsigned> checks{0};
void Check(bool value,const char* text){++checks;if(!value)throw std::runtime_error(text);}
std::array<unsigned char,32> Hash(const std::vector<unsigned char>& bytes){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> result{};
    Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"SHA provider");
    bool valid=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(valid)valid=BCryptHashData(hash,const_cast<PUCHAR>(bytes.data()),static_cast<ULONG>(bytes.size()),0)>=0;
    if(valid)valid=BCryptFinishHash(hash,result.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);Check(valid,"SHA digest");return result;
}
std::vector<unsigned char> Code(DWORD version,size_t bytes,DWORD marker){
    std::vector<DWORD> words={version,0x05000051,0xa00f0000,0,0,0,0x3f800000};
    if(version==0xfffe0300)words.insert(words.end(),{0x0200001f,0x80000000,0xe00f0000});
    words.insert(words.end(),{0x02000001,version==0xfffe0300?0xe00f0000u:0x800f0800u,0xa0e40000});
    const size_t padding=bytes/4-words.size()-2;Check(padding<0x7fff,"authored comment bound");
    words.push_back(static_cast<DWORD>(padding<<16)|0xfffe);words.insert(words.end(),padding,marker);words.push_back(0xffff);
    const auto* start=reinterpret_cast<const unsigned char*>(words.data());return {start,start+words.size()*4};
}
struct Device;
struct Shader {void** table=nullptr;ULONG refs=1;std::vector<unsigned char> code;Device* owner=nullptr;unsigned queries=0;bool throw_query=false;};
struct Surface {void** table=nullptr;ULONG refs=1;DWORD width=1920,height=1080;D3DFORMAT format=D3DFMT_A8R8G8B8;};
struct Device {
    void** table=nullptr;Shader* vertex=nullptr;Shader* pixel=nullptr;Shader* original=nullptr;Surface* surface=nullptr;
    std::vector<std::unique_ptr<Shader>> created;void** shader_table=nullptr;ULONG refs=1;
    DWORD blend=0,alpha=0,srgb=0,viewport_x=0;float min_z=0,max_z=1;
    bool fail_create=false,partial_create=false,throw_create=false,fail_apply=false,throw_apply=false,silent_apply=false;
    bool fail_restore=false,throw_restore=false,throw_get=false,wrong_get=false;
    unsigned sets=0,original_draws=0;
};
ULONG STDMETHODCALLTYPE Add(Shader* shader){Check(shader->refs>0,"AddRef live shader");return ++shader->refs;}
ULONG STDMETHODCALLTYPE Release(Shader* shader){Check(shader->refs>0,"no shader reference underflow");const ULONG result=--shader->refs;if(!result&&shader->owner)--shader->owner->refs;return result;}
HRESULT STDMETHODCALLTYPE Function(Shader* shader,void* output,UINT* count){
    ++shader->queries;SetLastError(0x7712);
    unsigned ignored=0;_controlfp_s(&ignored,_RC_UP,_MCW_RC);_mm_setcsr((_mm_getcsr()&~_MM_ROUND_MASK)|_MM_ROUND_UP);
    if(shader->throw_query)throw std::runtime_error("authored GetFunction exception");
    const UINT size=static_cast<UINT>(shader->code.size());if(!output){*count=size;return S_OK;}
    if(*count<size)return E_FAIL;std::memcpy(output,shader->code.data(),size);*count=size;return S_OK;
}
ULONG STDMETHODCALLTYPE SurfaceAdd(Surface* surface){return ++surface->refs;}
ULONG STDMETHODCALLTYPE SurfaceRelease(Surface* surface){Check(surface->refs>1,"surface reference balance");return --surface->refs;}
HRESULT STDMETHODCALLTYPE Desc(Surface* surface,D3DSURFACE_DESC* desc){*desc={};desc->Width=surface->width;desc->Height=surface->height;desc->Format=surface->format;return S_OK;}
HRESULT STDMETHODCALLTYPE Vertex(Device* device,IDirect3DVertexShader9** output){*output=reinterpret_cast<IDirect3DVertexShader9*>(device->vertex);if(device->vertex)Add(device->vertex);return S_OK;}
HRESULT STDMETHODCALLTYPE Pixel(Device* device,IDirect3DPixelShader9** output){
    auto* shader=device->wrong_get?device->original:device->pixel;
    *output=reinterpret_cast<IDirect3DPixelShader9*>(shader);if(shader)Add(shader);
    if(device->throw_get){device->throw_get=false;throw std::runtime_error("authored getter exception after AddRef");}return S_OK;
}
HRESULT STDMETHODCALLTYPE Target(Device* device,DWORD index,IDirect3DSurface9** output){if(index)return E_FAIL;*output=reinterpret_cast<IDirect3DSurface9*>(device->surface);SurfaceAdd(device->surface);return S_OK;}
HRESULT STDMETHODCALLTYPE Viewport(Device* device,D3DVIEWPORT9* output){*output={device->viewport_x,0,1920,1080,device->min_z,device->max_z};return S_OK;}
HRESULT STDMETHODCALLTYPE RenderState(Device* device,D3DRENDERSTATETYPE key,DWORD* output){*output=key==D3DRS_ALPHABLENDENABLE?device->blend:key==D3DRS_ALPHATESTENABLE?device->alpha:device->srgb;return S_OK;}
HRESULT STDMETHODCALLTYPE Create(Device* device,const DWORD* bytes,IDirect3DPixelShader9** output){
    if(device->throw_create){device->throw_create=false;throw std::runtime_error("authored creation exception");}
    if(device->fail_create){*output=nullptr;return E_FAIL;}
    auto shader=std::make_unique<Shader>();shader->table=device->shader_table;shader->owner=device;
    const auto* start=reinterpret_cast<const unsigned char*>(bytes);shader->code.assign(start,start+3840);++device->refs;
    *output=reinterpret_cast<IDirect3DPixelShader9*>(shader.get());device->created.push_back(std::move(shader));return device->partial_create?E_FAIL:S_OK;
}
void Bind(Device& device,Shader* shader){if(shader)Add(shader);auto* old=device.pixel;device.pixel=shader;if(old)Release(old);}
HRESULT STDMETHODCALLTYPE Set(Device* device,IDirect3DPixelShader9* value){
    ++device->sets;SetLastError(0x8823);auto* shader=reinterpret_cast<Shader*>(value);const bool generated=shader&&shader->owner;
    if(!generated&&device->fail_restore)return E_FAIL;
    if(generated&&device->silent_apply){device->silent_apply=false;return S_OK;}
    Bind(*device,shader);
    if(generated&&device->throw_apply){device->throw_apply=false;throw std::runtime_error("authored setter exception after mutation");}
    if(!generated&&device->throw_restore){device->throw_restore=false;throw std::runtime_error("authored restore exception after mutation");}
    if(generated&&device->fail_apply){device->fail_apply=false;return E_FAIL;}return S_OK;
}
Stats Snapshot(){Stats stats{};Check(ReadStats(&stats),"stats POD read");return stats;}
std::wstring WideHash(const std::array<unsigned char,32>& hash){std::wstring result;for(auto b:hash){constexpr wchar_t digits[]=L"0123456789abcdef";result+=digits[b>>4];result+=digits[b&15];}return result;}
void Write(const std::wstring& path,const std::vector<unsigned char>& bytes){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file!=INVALID_HANDLE_VALUE,"create owned fixture program");DWORD count=0;const bool valid=WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size();CloseHandle(file);Check(valid,"write exact authored program");
}
DWORD WINAPI ForeignReset(void* value){return Reset(static_cast<IDirect3DDevice9*>(value))?0u:1u;}
DWORD WINAPI ForeignDraw(void* value){DrawTransaction draw{};BeginDraw(static_cast<IDirect3DDevice9*>(value),&draw);const bool active=draw.active||draw.unsafe_state;EndDraw(&draw,S_OK);return active?1u:0u;}
DWORD WINAPI ForeignRecovery(void* value){return RetryPendingRestore(static_cast<IDirect3DDevice9*>(value))?1u:0u;}
__declspec(noinline) bool ExpiredCaller(Device& d,IDirect3DDevice9* device){
    DrawTransaction local{};BeginDraw(device,&local);Check(local.active,"expiring callback begins genuine transaction");
    d.fail_restore=true;EndDraw(&local,S_OK);return local.unsafe_state;
}
__declspec(noinline) void OverwriteOldStack(){volatile unsigned char data[4096]{};for(size_t i=0;i<sizeof(data);++i)data[i]=static_cast<unsigned char>(i);}
}

int wmain(int argc,wchar_t** argv){using namespace WaterShaderEffects;try{
    Check(argc==2,"explicit fresh fixture case root");const std::wstring root=argv[1];
    Check(CreateDirectoryW(root.c_str(),nullptr)!=FALSE,"fresh owned case directory");
    std::array<void*,5> shader_table{};shader_table[1]=reinterpret_cast<void*>(&Add);shader_table[2]=reinterpret_cast<void*>(&Release);shader_table[4]=reinterpret_cast<void*>(&Function);
    std::array<void*,17> surface_table{};surface_table[1]=reinterpret_cast<void*>(&SurfaceAdd);surface_table[2]=reinterpret_cast<void*>(&SurfaceRelease);surface_table[12]=reinterpret_cast<void*>(&Desc);
    std::array<void*,134> table{};table[93]=reinterpret_cast<void*>(&Vertex);table[108]=reinterpret_cast<void*>(&Pixel);table[107]=reinterpret_cast<void*>(&Set);table[106]=reinterpret_cast<void*>(&Create);
    table[38]=reinterpret_cast<void*>(&Target);table[48]=reinterpret_cast<void*>(&Viewport);table[58]=reinterpret_cast<void*>(&RenderState);
    Shader vertex{shader_table.data(),1,Code(0xfffe0300,2592,1)},pixel{shader_table.data(),1,Code(0xffff0300,3740,2)};
    const auto program=Code(0xffff0300,3840,3);const auto vh=Hash(vertex.code),ph=Hash(pixel.code),gh=Hash(program);
    Check(FixtureIdentities(vh.data(),ph.data(),gh.data()),"authored identities only in fixture build");
    const auto report=root+L"\\native.private.json";
    Check(!Initialize(report.c_str(),0,1080)&&!Initialize(report.c_str(),4096,2160),"dimension bounds");
    Check(!Initialize((root+L"\\wrong.json").c_str(),1920,1080),"trusted private report suffix");
    SetLastError(0x37579001);Check(Initialize(report.c_str(),1920,1080),"disabled admitted initialization");Check(GetLastError()==0x37579001,"initialize LastError preserved");
    Check(!FixtureIdentities(vh.data(),ph.data(),gh.data()),"production identity cannot change after initialization");
    const auto local=root+L"\\LocalData",materials=local+L"\\MaterialShaders",recipe=materials+L"\\river-depth-fresnel-v1-candidate";
    Check(CreateDirectoryW(local.c_str(),nullptr)&&CreateDirectoryW(materials.c_str(),nullptr)&&CreateDirectoryW(recipe.c_str(),nullptr),"owned recipe hierarchy");
    const auto path=recipe+L"\\"+WideHash(gh)+L".ps.bin";Write(path,program);
    Config config{};config.render_width=1920;config.render_height=1080;config.enabled=1;wcscpy_s(config.pixel_path,path.c_str());std::memcpy(config.pixel_sha256,gh.data(),32);
    Config bad=config;bad.size=1072;Check(!Configure(&bad),"fixed1076 ABI");bad=config;bad.version=2;Check(!Configure(&bad),"version guard");bad=config;bad.enabled=2;Check(!Configure(&bad),"explicit boolean");
    bad=config;bad.render_width=1280;Check(!Configure(&bad),"trusted source dimensions");bad=config;bad.pixel_sha256[0]^=1;Check(!Configure(&bad),"candidate SHA guard");
    bad=config;wcscpy_s(bad.pixel_path,(root+L"\\outside.ps.bin").c_str());Write(bad.pixel_path,program);Check(!Configure(&bad),"correct bytes outside immutable recipe path refused");
    SetLastError(0x37579002);Check(Configure(&config),"explicit exact authored program enable");Check(GetLastError()==0x37579002,"configure LastError");
    HANDLE overwrite=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(overwrite==INVALID_HANDLE_VALUE,"pinned program denies writes");
    Surface surface{surface_table.data()};Device d{};d.table=table.data();d.vertex=&vertex;d.pixel=&pixel;d.original=&pixel;d.surface=&surface;d.shader_table=shader_table.data();Add(&vertex);Add(&pixel);
    auto* device=reinterpret_cast<IDirect3DDevice9*>(&d);DrawTransaction draw{};
    unsigned cw=0;_controlfp_s(&cw,0,0);const auto mx=_mm_getcsr();
    SetLastError(0x37579003);BeginDraw(device,&draw);Check(GetLastError()==0x37579003,"Begin LastError");Check(draw.active&&!draw.unsafe_state&&d.pixel!=&pixel,"matching original pair temporarily replaced");
    Check(!Configure(&config)&&!Reset(device)&&!Shutdown(),"lifecycle refuses active draw transaction");
    // Reset's refusal faults the effect, while the original transaction must
    // still restore before any reconfiguration or fallback is permitted.
    SetLastError(0x37579004);EndDraw(&draw,S_OK);Check(GetLastError()==0x37579004,"post-original LastError");
    unsigned after=0;_controlfp_s(&after,0,0);Check(cw==after&&mx==_mm_getcsr(),"x87/SSE state preserved");
    Check(d.pixel==&pixel&&!draw.active&&!draw.unsafe_state,"original program restored");Check(Configure(&config),"explicit recovery after safe refusal");
    const auto warm=Snapshot().shader_creations;
    for(unsigned frame=0;frame<8;++frame){BeginDraw(device,&draw);Check(draw.active,"warm matching draw");EndDraw(&draw,S_OK);EndFrame(device);}
    const auto steady=Snapshot();Check(steady.shader_creations==warm+1&&steady.retained_programs==1&&d.refs==2,"one retained generated program across warm frames");
    Check(!steady.cached_refs&&steady.completed_applications==1&&steady.completed_restorations==1,"completed frame proof and identity refs released");
    EndFrame(device);Check(!Snapshot().completed_applications,"no draw frame never reuses application proof");
    Check(vertex.refs==2&&pixel.refs==2&&surface.refs==1,"balanced original and target references");
    const unsigned unchanged=d.sets;
    for(unsigned guard=0;guard<7;++guard){
        if(guard==0)d.blend=1;if(guard==1)d.alpha=1;if(guard==2)d.srgb=1;if(guard==3)d.viewport_x=1;
        if(guard==4)d.max_z=.5f;if(guard==5)surface.width=960;if(guard==6)surface.format=D3DFMT_R5G6B5;
        BeginDraw(device,&draw);Check(!draw.active&&!draw.unsafe_state,"wrong render context refused");
        d.blend=d.alpha=d.srgb=d.viewport_x=0;d.max_z=1;surface.width=1920;surface.format=D3DFMT_A8R8G8B8;
    }
    Check(d.sets==unchanged,"context guards never mutate a shader");
    EndFrame(device);Check(Reset(device),"clear between identity stress cases");
    std::vector<std::unique_ptr<Shader>> wrong;
    for(unsigned i=0;i<129;++i){auto shader=std::make_unique<Shader>();shader->table=shader_table.data();shader->code=Code(0xfffe0300,2592,100+i);d.vertex=shader.get();Add(d.vertex);BeginDraw(device,&draw);Check(!draw.active,"same-sized wrong hash refused");Release(d.vertex);wrong.push_back(std::move(shader));}
    d.vertex=&vertex;Check(!Snapshot().cached_refs,"negative identities do not fill cache");
    BeginDraw(device,&draw);Check(draw.active,"valid River survives129 unrelated programs");EndDraw(&draw,S_OK);EndFrame(device);
    // Fill the positive cache with distinct original objects. A valid overflow
    // remains eligible; capacity is a performance bound, never a rejection.
    std::vector<std::unique_ptr<Shader>> positive;
    for(unsigned i=0;i<130;++i){auto shader=std::make_unique<Shader>();shader->table=shader_table.data();shader->code=vertex.code;d.vertex=shader.get();Add(d.vertex);BeginDraw(device,&draw);Check(draw.active,"positive saturation still matches");EndDraw(&draw,S_OK);Release(d.vertex);positive.push_back(std::move(shader));}
    d.vertex=&vertex;Check(Snapshot().cache_overflows>0,"uncached positive overflow measured");EndFrame(device);
    for(const auto& shader:positive)Check(shader->refs==1,"saturated original refs released");
    auto restore_config=[&]{Check(!Snapshot().unsafe_state,"reconfiguration only after verified original state");Check(Configure(&config),"re-enable exact pinned shader");};
    d.fail_apply=true;BeginDraw(device,&draw);Check(!draw.active&&!draw.unsafe_state&&d.pixel==&pixel,"partial failed setter restored before original fallback");Check(Snapshot().faulted&&!Snapshot().enabled,"failed apply disables water truthfully");restore_config();
    d.silent_apply=true;BeginDraw(device,&draw);Check(!draw.active&&!draw.unsafe_state&&d.pixel==&pixel,"silent setter mismatch detected and restored");restore_config();
    d.throw_apply=true;BeginDraw(device,&draw);Check(!draw.active&&!draw.unsafe_state&&d.pixel==&pixel,"exception after mutation restores original");restore_config();
    BeginDraw(device,&draw);Check(draw.active,"unsafe restore test activates");d.fail_restore=true;EndDraw(&draw,S_OK);Check(draw.unsafe_state&&Snapshot().unsafe_state,"failed restoration retains unsafe state");
    Check(!RetryRestore(&draw)&&!Reset(device)&&!Configure(&config)&&!Shutdown(),"failed retry blocks lifecycle and reconfiguration");
    DrawTransaction blocked{};const auto calls=d.original_draws;BeginDraw(device,&blocked);if(!blocked.unsafe_state)++d.original_draws;
    Check(blocked.unsafe_state&&d.original_draws==calls,"no original draw may run after an unrestored shader");
    d.fail_restore=false;Check(RetryRestore(&draw)&&d.pixel==&pixel&&!Snapshot().unsafe_state,"immediate same-callback retry verifies original");restore_config();
    Check(ExpiredCaller(d,device),"unsafe callback returns while State owns original ref");OverwriteOldStack();
    const auto pending=Snapshot();Check(pending.pending_original_refs==1&&pending.unsafe_state,"durable pending ownership remains after caller lifetime");
    const unsigned pending_sets=d.sets;DrawTransaction later{};BeginDraw(device,&later);
    Check(later.unsafe_state&&!later.active&&d.sets==pending_sets,"later callback does not dereference old stack or draw");
    Check(!Reset(device)&&!Shutdown()&&Snapshot().pending_original_refs==1,"unsafe lifecycle retains durable original ref without caller access");
    HANDLE recovery_thread=CreateThread(nullptr,0,ForeignRecovery,device,0,nullptr);Check(recovery_thread!=nullptr,"foreign recovery worker");WaitForSingleObject(recovery_thread,5000);DWORD recovered=1;GetExitCodeThread(recovery_thread,&recovered);CloseHandle(recovery_thread);
    Check(!recovered&&d.sets==pending_sets,"foreign thread cannot restore pending GPU state");
    d.fail_restore=false;Check(RetryPendingRestore(device)&&d.pixel==&pixel&&!Snapshot().pending_original_refs&&!Snapshot().unsafe_state,
        "explicit owner-serial recovery uses only module-owned state after caller expired");
    Check(Reset(device)&&pixel.refs==2&&vertex.refs==2,"recovered expired callback releases all pending/original cache refs");restore_config();
    BeginDraw(device,&draw);const auto healthy_sets=d.sets;Check(draw.active&&!RetryPendingRestore(device)&&d.sets==healthy_sets,"pending recovery refuses a healthy active callback");EndDraw(&draw,S_OK);
    BeginDraw(device,&draw);d.throw_restore=true;EndDraw(&draw,E_FAIL);Check(draw.unsafe_state,"restore exception is unsafe despite partial mutation");Check(RetryRestore(&draw)&&d.pixel==&pixel,"restore exception recoverable by verified retry");restore_config();
    d.throw_get=true;BeginDraw(device,&draw);Check(!draw.active&&!draw.unsafe_state&&d.pixel==&pixel,"getter exception balances obtained references");restore_config();
    Check(Reset(device),"clear cache before creation failures");d.fail_create=true;BeginDraw(device,&draw);Check(!draw.active&&!draw.unsafe_state&&Snapshot().shader_failures,"CreatePixelShader failure preserves original draw");d.fail_create=false;restore_config();
    d.partial_create=true;BeginDraw(device,&draw);Check(!draw.active&&!draw.unsafe_state&&d.refs==1&&!Snapshot().retained_programs,"failed creation with assigned COM pointer releases it");d.partial_create=false;restore_config();
    d.throw_create=true;BeginDraw(device,&draw);Check(!draw.active&&!draw.unsafe_state&&d.pixel==&pixel,"creation exception preserves original");restore_config();
    BeginDraw(device,&draw);EndDraw(&draw,E_FAIL);Check(d.pixel==&pixel&&Snapshot().failed_draws>0,"failed original draw still restores");
    Check(Reset(device),"external Release preparation");Check(d.refs==1&&!Snapshot().retained_programs&&!Snapshot().cached_refs,"all module device-child refs cleared before Release");
    HANDLE thread=CreateThread(nullptr,0,ForeignDraw,device,0,nullptr);Check(thread!=nullptr,"foreign draw worker");WaitForSingleObject(thread,5000);DWORD code=1;GetExitCodeThread(thread,&code);CloseHandle(thread);Check(code==0,"even eligible foreign draw cannot claim ownership after routine Release cleanup");
    BeginDraw(device,&draw);EndDraw(&draw,S_OK);thread=CreateThread(nullptr,0,ForeignReset,device,0,nullptr);Check(thread!=nullptr,"foreign cleanup worker");WaitForSingleObject(thread,5000);GetExitCodeThread(thread,&code);CloseHandle(thread);Check(code==0&&d.refs==1,"same-device foreign Release cleanup drops refs");
    Config disabled{};disabled.render_width=1920;disabled.render_height=1080;Check(Configure(&disabled),"explicit disable");const unsigned off_sets=d.sets;
    BeginDraw(device,&draw);Check(!draw.active&&!draw.unsafe_state&&d.sets==off_sets,"disabled water does not query or mutate draw");
    const auto history=Snapshot();Check(Shutdown(),"safe full shutdown");const auto closed=Snapshot();
    Check(!closed.initialized&&!closed.enabled&&!closed.cached_refs&&!closed.retained_programs&&closed.applications==history.applications
        &&closed.restore_failures==history.restore_failures,"shutdown preserves truthful scalar history and clears live refs");
    Check(vertex.refs==2&&pixel.refs==2&&surface.refs==1&&d.refs==1,"all references balanced");
    Release(&vertex);Bind(d,nullptr);Release(&vertex);Release(&pixel);
    std::printf("{\"format\":\"s7-authored-water-com-fixture-v1\",\"passed\":true,\"checks\":%u,\"gpu\":false,\"game\":false}\n",checks.load());return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Water fixture failed: %s (%u checks)\n",error.what(),checks.load());return 1;}}
