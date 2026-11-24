// CPU-only state transaction fixture, using authored fake shader identities.
#include "material_shader_effects.h"
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
using namespace MaterialShaderEffects;
void Check(bool value,const char* message){if(!value)throw std::runtime_error(message);}
std::array<unsigned char,32> Hash(const void* data,size_t bytes){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> output{};
    Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"hash provider");
    Check(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"hash create");
    Check(BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),static_cast<ULONG>(bytes),0)>=0,"hash update");
    Check(BCryptFinishHash(hash,output.data(),32,0)>=0,"hash finish");BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);return output;
}
std::vector<unsigned char> Code(bool vertex,DWORD tag){std::array<DWORD,5> words{vertex?0xfffe0300u:0xffff0300u,0x0001fffe,tag,0,0xffff};const auto* p=reinterpret_cast<const unsigned char*>(words.data());return{p,p+sizeof(words)};}
unsigned live_created=0;
struct Shader{void** table=nullptr;ULONG refs=1;bool dynamic=false;std::vector<unsigned char> code;};
ULONG STDMETHODCALLTYPE ShaderAddRef(Shader* value){return ++value->refs;}
ULONG STDMETHODCALLTYPE ShaderRelease(Shader* value){Check(value->refs>0,"shader reference underflow");ULONG refs=--value->refs;if(!refs&&value->dynamic){--live_created;delete value;}return refs;}
HRESULT STDMETHODCALLTYPE Function(Shader* value,void* data,UINT* bytes){if(!bytes)return E_INVALIDARG;if(!data){*bytes=static_cast<UINT>(value->code.size());return S_OK;}if(*bytes<value->code.size())return E_FAIL;std::memcpy(data,value->code.data(),value->code.size());*bytes=static_cast<UINT>(value->code.size());return S_OK;}
struct Surface{void** table=nullptr;ULONG refs=1;};
ULONG STDMETHODCALLTYPE SurfaceAddRef(Surface* value){return ++value->refs;}
ULONG STDMETHODCALLTYPE SurfaceRelease(Surface* value){Check(value->refs>1,"surface reference underflow");return --value->refs;}
HRESULT STDMETHODCALLTYPE Description(Surface*,D3DSURFACE_DESC* output){*output={};output->Width=1920;output->Height=1080;output->Format=D3DFMT_A8R8G8B8;return S_OK;}
struct Device{void** table=nullptr;Shader* vertex=nullptr;Shader* pixel=nullptr;Surface* surface=nullptr;void** shader_table=nullptr;DWORD blend=0;int fail_set=0;bool fail_restore=false,throw_once=false;
    std::array<std::array<float,4>,256> vs{};std::array<std::array<float,4>,224> ps{};};
HRESULT STDMETHODCALLTYPE GetVertex(Device* d,IDirect3DVertexShader9** out){*out=reinterpret_cast<IDirect3DVertexShader9*>(d->vertex);if(d->vertex)ShaderAddRef(d->vertex);return S_OK;}
HRESULT STDMETHODCALLTYPE FixtureGetPixel(Device* d,IDirect3DPixelShader9** out){*out=reinterpret_cast<IDirect3DPixelShader9*>(d->pixel);if(d->pixel)ShaderAddRef(d->pixel);return S_OK;}
HRESULT STDMETHODCALLTYPE SetVertex(Device* d,IDirect3DVertexShader9* shader){auto* value=reinterpret_cast<Shader*>(shader);if(value)ShaderAddRef(value);if(d->vertex)ShaderRelease(d->vertex);d->vertex=value;return S_OK;}
HRESULT STDMETHODCALLTYPE FixtureSetPixel(Device* d,IDirect3DPixelShader9* shader){auto* value=reinterpret_cast<Shader*>(shader);if(value)ShaderAddRef(value);if(d->pixel)ShaderRelease(d->pixel);d->pixel=value;return S_OK;}
HRESULT STDMETHODCALLTYPE GetVS(Device* d,UINT index,float* out,UINT count){if(count!=1||index>=256)return E_FAIL;std::memcpy(out,d->vs[index].data(),16);return S_OK;}
HRESULT STDMETHODCALLTYPE GetPS(Device* d,UINT index,float* out,UINT count){if(count!=1||index>=224)return E_FAIL;std::memcpy(out,d->ps[index].data(),16);return S_OK;}
HRESULT STDMETHODCALLTYPE SetVS(Device* d,UINT index,const float* data,UINT count){if(count!=1||index>=256)return E_FAIL;std::memcpy(d->vs[index].data(),data,16);if(d->throw_once){d->throw_once=false;throw std::runtime_error("fixture setter exception");}if(d->fail_set>0&&!--d->fail_set)return E_FAIL;return d->fail_restore?E_FAIL:S_OK;}
HRESULT STDMETHODCALLTYPE SetPS(Device* d,UINT index,const float* data,UINT count){if(count!=1||index>=224)return E_FAIL;std::memcpy(d->ps[index].data(),data,16);if(d->fail_set>0&&!--d->fail_set)return E_FAIL;return d->fail_restore?E_FAIL:S_OK;}
HRESULT STDMETHODCALLTYPE Target(Device* d,DWORD,IDirect3DSurface9** out){*out=reinterpret_cast<IDirect3DSurface9*>(d->surface);SurfaceAddRef(d->surface);return S_OK;}
HRESULT STDMETHODCALLTYPE Viewport(Device*,D3DVIEWPORT9* out){*out={0,0,1920,1080,0,1};return S_OK;}
HRESULT STDMETHODCALLTYPE RenderState(Device* d,D3DRENDERSTATETYPE kind,DWORD* out){*out=kind==D3DRS_ALPHABLENDENABLE?d->blend:0;return S_OK;}
HRESULT STDMETHODCALLTYPE CreateVertex(Device* d,const DWORD* code,IDirect3DVertexShader9** out){auto* s=new Shader{d->shader_table,1,true,{reinterpret_cast<const unsigned char*>(code),reinterpret_cast<const unsigned char*>(code)+20}};++live_created;*out=reinterpret_cast<IDirect3DVertexShader9*>(s);return S_OK;}
HRESULT STDMETHODCALLTYPE CreatePixel(Device* d,const DWORD* code,IDirect3DPixelShader9** out){auto* s=new Shader{d->shader_table,1,true,{reinterpret_cast<const unsigned char*>(code),reinterpret_cast<const unsigned char*>(code)+20}};++live_created;*out=reinterpret_cast<IDirect3DPixelShader9*>(s);return S_OK;}
void Write(const std::wstring& path,const std::vector<unsigned char>& data){HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE,"new fixture artifact");DWORD wrote=0;bool ok=WriteFile(file,data.data(),static_cast<DWORD>(data.size()),&wrote,nullptr)&&wrote==data.size();CloseHandle(file);Check(ok,"fixture write");}
std::string Read(const std::wstring& path){HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);Check(file!=INVALID_HANDLE_VALUE,"read private report");std::array<char,32768> bytes{};DWORD count=0;bool valid=ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)!=0;CloseHandle(file);Check(valid,"read report bytes");return{bytes.data(),count};}
Stats Snapshot(){Stats stats{};Check(ReadStats(&stats),"read stats");return stats;}
}
int wmain(int argc,wchar_t** argv){using namespace MaterialShaderEffects;try{
    Check(argc==2,"use a new private fixture directory");const std::wstring folder=argv[1];Check(CreateDirectoryW(folder.c_str(),nullptr)!=0,"new fixture directory");
    std::array<void*,5> shader_table{};shader_table[1]=reinterpret_cast<void*>(&ShaderAddRef);shader_table[2]=reinterpret_cast<void*>(&ShaderRelease);shader_table[4]=reinterpret_cast<void*>(&Function);
    std::array<void*,17> surface_table{};surface_table[1]=reinterpret_cast<void*>(&SurfaceAddRef);surface_table[2]=reinterpret_cast<void*>(&SurfaceRelease);surface_table[12]=reinterpret_cast<void*>(&Description);Surface surface{surface_table.data(),1};
    std::array<void*,134> table{};table[93]=reinterpret_cast<void*>(&GetVertex);table[108]=reinterpret_cast<void*>(&FixtureGetPixel);table[92]=reinterpret_cast<void*>(&SetVertex);table[107]=reinterpret_cast<void*>(&FixtureSetPixel);
    table[95]=reinterpret_cast<void*>(&GetVS);table[110]=reinterpret_cast<void*>(&GetPS);table[94]=reinterpret_cast<void*>(&SetVS);table[109]=reinterpret_cast<void*>(&SetPS);table[38]=reinterpret_cast<void*>(&Target);table[48]=reinterpret_cast<void*>(&Viewport);table[58]=reinterpret_cast<void*>(&RenderState);table[91]=reinterpret_cast<void*>(&CreateVertex);table[106]=reinterpret_cast<void*>(&CreatePixel);
    Device d{};d.table=table.data();d.surface=&surface;d.shader_table=shader_table.data();auto* device=reinterpret_cast<IDirect3DDevice9*>(&d);
    std::array<Shader,8> originals{};for(unsigned role=0;role<4;++role){originals[role*2]={shader_table.data(),1,false,Code(true,100+role)};originals[role*2+1]={shader_table.data(),1,false,Code(false,200+role)};}
    Config config{};config.flags=AllowedFlags;config.render_width=1920;config.render_height=1080;
    const std::wstring leaf=folder+L"/leaf.bin",bump=folder+L"/bump.bin",parallax=folder+L"/parallax.bin",report=folder+L"/effects.private.json";
    const auto leaf_code=Code(true,300),bump_code=Code(false,301),parallax_code=Code(false,302);Write(leaf,leaf_code);Write(bump,bump_code);Write(parallax,parallax_code);
    wcscpy_s(config.leaf_vertex_path,leaf.c_str());wcscpy_s(config.bump_pixel_path,bump.c_str());wcscpy_s(config.parallax_pixel_path,parallax.c_str());wcscpy_s(config.report_path,report.c_str());
    for(unsigned role=0;role<4;++role){const auto v=Hash(originals[role*2].code.data(),20),p=Hash(originals[role*2+1].code.data(),20);auto replacement=Hash((role==1?bump_code:role==2?parallax_code:leaf_code).data(),20);Check(FixtureCompatibility(role,v.data(),p.data(),20,20,replacement.data()),"fixture-only identity override");}
    SetLastError(0x37571001);Check(Initialize(&config),"initialize fixture");Check(GetLastError()==0x37571001,"initialize LastError");
    for(unsigned role=0;role<4;++role){SetVertex(&d,reinterpret_cast<IDirect3DVertexShader9*>(&originals[role*2]));FixtureSetPixel(&d,reinterpret_cast<IDirect3DPixelShader9*>(&originals[role*2+1]));d.blend=role==1||role==2?1u:0u;
        d.vs[68]={.8333333f,.84f,.8266667f,.8333333f};d.vs[69]={.6333333f,.66f,.64f,.66f};d.ps[33]={role==2?15.0f:1.0f,.5f,.02f,role==2?.2f:.25f};
        const auto fog=d.vs[68],reflection=d.vs[69],terrain=d.ps[33];DrawTransaction draw{};SetLastError(0x37571002);BeginDraw(device,&draw);Check(GetLastError()==0x37571002,"begin LastError");Check(draw.active,"known original pair activates draw-local effect");
        Check(!SetEnabled(0),"toggle refused while a draw transaction is active");
        if(role==0)Check(d.vs[69][0]>.63f&&d.vs[68][0]<fog[0],"river classes independently modified");
        if(role==1||role==2)Check(d.ps[33][1]>.59f&&d.pixel!=&originals[role*2+1],"terrain shader plus material contrast");
        if(role==3)Check(d.vertex!=&originals[role*2],"leaf replacement temporarily bound");
        SetLastError(0x37571003);EndDraw(&draw,role==2?E_FAIL:S_OK);Check(GetLastError()==0x37571003,"end preserves original draw LastError");
        Check(d.vertex==&originals[role*2]&&d.pixel==&originals[role*2+1],"original shader pair restored");
        Check(d.vs[68]==fog&&d.vs[69]==reflection&&d.ps[33]==terrain,"all original constant components restored");EndFrame(device);
        Check(live_created==role&&Snapshot().retained_programs==role&&!Snapshot().cached_refs,"EndFrame clears original refs and retains at most three device programs");}
    Check(Snapshot().restored_draws==4&&!Snapshot().restore_failures&&Snapshot().failed_draws==1,"successful and failed original draw both restored");
    auto WarmRole=[&](unsigned role){SetVertex(&d,reinterpret_cast<IDirect3DVertexShader9*>(&originals[role*2]));FixtureSetPixel(&d,reinterpret_cast<IDirect3DPixelShader9*>(&originals[role*2+1]));d.blend=role==1||role==2?1u:0u;
        DrawTransaction draw{};BeginDraw(device,&draw);Check(draw.active,"warm role activates");EndDraw(&draw,S_OK);Check(!draw.unsafe_state,"warm role restores");};
    const auto warm_creations=Snapshot().shader_creations;Check(warm_creations==3&&Snapshot().peak_retained_programs==3,"one generated program per supported role");
    for(unsigned frame=0;frame<8;++frame){for(unsigned role=1;role<4;++role)WarmRole(role);EndFrame(device);
        Check(Snapshot().shader_creations==warm_creations&&Snapshot().retained_programs==3&&!Snapshot().cached_refs&&live_created==3,"warm frames do not recreate device programs");}
    Reset(device);Check(!live_created&&!Snapshot().retained_programs&&!Snapshot().cached_refs&&Snapshot().peak_retained_programs==3,"external Release/Reset boundary drops all device references and retains peak evidence");
    WarmRole(1);EndFrame(device);Check(Snapshot().shader_creations==warm_creations+1&&live_created==1&&Snapshot().retained_programs==1,"device program recreated lazily after Reset");
    const auto applied_before=Snapshot().applied;SetLastError(0x37571005);Check(SetEnabled(0),"disable at safe boundary");Check(GetLastError()==0x37571005,"toggle preserves LastError");
    Check(!live_created&&!Snapshot().retained_programs,"A/B disable drops retained programs");
    Check(Snapshot().enabled_flags==0&&Snapshot().initialized_flags==AllowedFlags&&Snapshot().applied==applied_before,"toggle retains initialized resources and cumulative evidence");
    DrawTransaction control{};BeginDraw(device,&control);Check(!control.active,"disabled control performs original draw unchanged");
    Check(!SetEnabled(8),"unsupported toggle flags refused");
    Check(SetEnabled(River)&&Snapshot().enabled_flags==River,"initialized subset enabled");Reset(device);
    Check(SetEnabled(AllowedFlags),"full re-enable after Reset");Check(Snapshot().enable_changes==3,"toggle epochs recorded");
    Check(!Snapshot().periodic_reporting,"periodic reports off by default");const auto no_heartbeat=Read(report);
    for(unsigned i=0;i<120;++i)EndFrame(device);Check(Read(report)==no_heartbeat,"ordinary EndFrame performs no periodic report writes");
    SetDiagnostics(true);Check(Snapshot().periodic_reporting==1,"OBSERVE explicitly enables periodic reports");const auto diagnostic=Read(report);
    for(unsigned i=0;i<120;++i)EndFrame(device);Check(Read(report)!=diagnostic,"OBSERVE periodic report advances");SetDiagnostics(false);
    SetVertex(&d,reinterpret_cast<IDirect3DVertexShader9*>(&originals[0]));FixtureSetPixel(&d,reinterpret_cast<IDirect3DPixelShader9*>(&originals[1]));d.blend=0;
    const auto fog=d.vs[68],reflection=d.vs[69];d.fail_set=1;DrawTransaction failed{};BeginDraw(device,&failed);Check(!failed.active&&d.vs[68]==fog&&d.vs[69]==reflection,"partial setter failure rolled back before draw");
    d.vs[68][0]=2;BeginDraw(device,&failed);Check(!failed.active,"unsupported original input fallback");d.vs[68]=fog;
    DrawTransaction unsafe{};BeginDraw(device,&unsafe);Check(unsafe.active,"prepare restoration failure");d.fail_restore=true;EndDraw(&unsafe,S_OK);
    Check(unsafe.unsafe_state&&Snapshot().faulted,"unrestored state surfaced to caller, never claimed original fallback");
    Check(!SetEnabled(0),"unsafe pending restoration cannot drop borrowed references via toggle");
    d.fail_restore=false;Check(RetryRestore(&unsafe)&&!unsafe.unsafe_state,"immediate explicit restoration retry");
    Check(d.vs[68]==fog&&d.vs[69]==reflection,"retry recovers original components");
    Reset(device);Shutdown();Check(!Snapshot().initialized_flags&&!Snapshot().enabled_flags&&!Snapshot().faulted&&!Snapshot().cached_refs,"safe Shutdown fully resets state");Check(!SetEnabled(River),"closed resources cannot be toggled on");Check(Initialize(&config),"fresh initialization after expected diagnostic fault");
    d.throw_once=true;DrawTransaction exceptional{};BeginDraw(device,&exceptional);
    Check(!exceptional.active&&!exceptional.unsafe_state&&d.vs[68]==fog&&d.vs[69]==reflection&&Snapshot().faulted,
          "exception unwinding restores before returning an unchanged original draw");
    Reset(device);SetVertex(&d,nullptr);FixtureSetPixel(&d,nullptr);Flush();Shutdown();for(const auto& shader:originals)Check(shader.refs==1,"original references balanced");Check(surface.refs==1&&!live_created,"all owned references balanced");
    std::puts("native material effects CPU transaction checks passed; no GPU/game started");return 0;
}catch(const std::exception& error){std::fprintf(stderr,"material effects fixture failed: %s\n",error.what());return 1;}}
