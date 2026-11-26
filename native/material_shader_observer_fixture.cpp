// Synthetic COM lifecycle fixture. Creates no D3D9 device, window or GPU work.
#include "material_shader_observer.h"
#include <atomic>
#include <bcrypt.h>
#include <cstdio>
#include <cstring>
#include <stdexcept>
#include <string>
#include <thread>
#include <vector>
#include <intrin.h>

namespace {
using namespace MaterialShaderObserver;
std::atomic<unsigned> checks{0};
void Check(bool value,const char* message){++checks;if(!value)throw std::runtime_error(message);}
std::array<unsigned char,32> Hash(const void* data,size_t bytes) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> result{};
    Check(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"fixture hash provider");
    Check(BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0,"fixture hash create");
    Check(BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),static_cast<ULONG>(bytes),0)>=0,"fixture hash data");
    Check(BCryptFinishHash(hash,result.data(),32,0)>=0,"fixture hash finish");
    BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);return result;
}
std::vector<unsigned char> Program(DWORD version,DWORD payload) {
    std::array<DWORD,3> words{version,payload,0x0000ffff};
    const auto begin=reinterpret_cast<const unsigned char*>(words.data());return {begin,begin+sizeof(words)};
}
struct FakeShader {
    void** table=nullptr;ULONG refs=1;unsigned reads=0;
    bool change_size=false;std::vector<unsigned char> code;
};
ULONG STDMETHODCALLTYPE AddRef(FakeShader* shader){return ++shader->refs;}
ULONG STDMETHODCALLTYPE Release(FakeShader* shader){Check(shader->refs>1,"cache reference underflow");return --shader->refs;}
HRESULT STDMETHODCALLTYPE GetFunction(FakeShader* shader,void* data,UINT* bytes) {
    if(!bytes)return D3DERR_INVALIDCALL;
    const UINT length=static_cast<UINT>(shader->code.size());
    if(!data){*bytes=length;return S_OK;}
    if(*bytes<length)return D3DERR_INVALIDCALL;
    std::memcpy(data,shader->code.data(),length);*bytes=length+(shader->change_size?4:0);++shader->reads;return S_OK;
}
struct FakeTexture {void** table=nullptr;ULONG refs=1;};
ULONG STDMETHODCALLTYPE TextureAddRef(FakeTexture* texture){return ++texture->refs;}
ULONG STDMETHODCALLTYPE TextureRelease(FakeTexture* texture){Check(texture->refs>1,"texture reference underflow");return --texture->refs;}
D3DRESOURCETYPE STDMETHODCALLTYPE TextureType(FakeTexture*){return D3DRTYPE_TEXTURE;}
DWORD STDMETHODCALLTYPE TextureLevels(FakeTexture*){return 4;}
HRESULT STDMETHODCALLTYPE TextureDesc(FakeTexture*,UINT level,D3DSURFACE_DESC* output) {
    if(level || !output)return D3DERR_INVALIDCALL;
    *output=D3DSURFACE_DESC{};output->Width=512;output->Height=256;output->Format=D3DFMT_A8R8G8B8;return S_OK;
}
struct FakeSurface {void** table=nullptr;ULONG refs=1;};
std::vector<unsigned char> trace_pixels;
unsigned trace_readbacks=0,trace_locks=0,trace_unlocks=0;
bool trace_fail=false;
HRESULT STDMETHODCALLTYPE SurfaceLock(FakeSurface*,D3DLOCKED_RECT* output,const RECT*,DWORD) {
    ++trace_locks;output->Pitch=1920*4+16;output->pBits=trace_pixels.data();return S_OK;
}
HRESULT STDMETHODCALLTYPE SurfaceUnlock(FakeSurface*){++trace_unlocks;return S_OK;}
ULONG STDMETHODCALLTYPE SurfaceAddRef(FakeSurface* surface){return ++surface->refs;}
ULONG STDMETHODCALLTYPE SurfaceRelease(FakeSurface* surface){Check(surface->refs>1,"surface reference underflow");return --surface->refs;}
HRESULT STDMETHODCALLTYPE SurfaceDesc(FakeSurface*,D3DSURFACE_DESC* output){*output={};output->Width=1920;output->Height=1080;output->Format=D3DFMT_A8R8G8B8;return S_OK;}
struct FakeDevice {void** table=nullptr;FakeShader* vertex=nullptr;FakeShader* pixel=nullptr;FakeTexture* texture=nullptr;bool nonfinite=false;FakeSurface* surface=nullptr;unsigned uniform_reads=0;bool unused_nan=false;
    std::uint32_t fail_render_states=0;std::uint64_t render_state_queries=0;bool blob_state=false;};
HRESULT STDMETHODCALLTYPE Offscreen(FakeDevice* d,UINT w,UINT h,D3DFORMAT f,D3DPOOL pool,IDirect3DSurface9** out,HANDLE*) {
    Check(w==1920&&h==1080&&f==D3DFMT_A8R8G8B8&&pool==D3DPOOL_SYSTEMMEM,"trace staging exact original dimensions/format/systemmem");
    *out=reinterpret_cast<IDirect3DSurface9*>(d->surface);SurfaceAddRef(d->surface);return S_OK;
}
HRESULT STDMETHODCALLTYPE Readback(FakeDevice*,IDirect3DSurface9*,IDirect3DSurface9*){++trace_readbacks;return trace_fail?E_FAIL:S_OK;}
HRESULT STDMETHODCALLTYPE Vertex(FakeDevice* device,IDirect3DVertexShader9** output) {
    if(!output)return D3DERR_INVALIDCALL;
    *output=reinterpret_cast<IDirect3DVertexShader9*>(device->vertex);
    if(device->vertex)AddRef(device->vertex);return S_OK;
}
HRESULT STDMETHODCALLTYPE Pixel(FakeDevice* device,IDirect3DPixelShader9** output) {
    if(!output)return D3DERR_INVALIDCALL;
    *output=reinterpret_cast<IDirect3DPixelShader9*>(device->pixel);
    if(device->pixel)AddRef(device->pixel);return S_OK;
}
HRESULT STDMETHODCALLTYPE Constants(FakeDevice* device,UINT index,float* output,UINT count) {
    if(!output || !count || count>4)return D3DERR_INVALIDCALL;
    ++device->uniform_reads;
    for(unsigned item=0;item<count;++item)for(unsigned component=0;component<4;++component)output[item*4+component]=static_cast<float>(index+item+component);
    if(device->nonfinite){const DWORD bits=0x7fc00000;std::memcpy(output,&bits,4);}
    if(device->unused_nan&&count==1&&(index==5||index==7||index==16)){const DWORD bits=0x7fc00000;std::memcpy(output+3,&bits,4);}
    return S_OK;
}
HRESULT STDMETHODCALLTYPE Texture(FakeDevice* device,DWORD index,IDirect3DBaseTexture9** output) {
    if(!output)return D3DERR_INVALIDCALL;
    *output=index==3?reinterpret_cast<IDirect3DBaseTexture9*>(device->texture):nullptr;
    if(*output)TextureAddRef(device->texture);return S_OK;
}
HRESULT STDMETHODCALLTYPE Target(FakeDevice* device,DWORD,IDirect3DSurface9** output){*output=reinterpret_cast<IDirect3DSurface9*>(device->surface);if(device->surface)SurfaceAddRef(device->surface);return device->surface?S_OK:E_FAIL;}
HRESULT STDMETHODCALLTYPE Viewport(FakeDevice*,D3DVIEWPORT9* output){*output={0,0,1920,1080,0,1};return S_OK;}
HRESULT STDMETHODCALLTYPE RenderState(FakeDevice* device,D3DRENDERSTATETYPE field,DWORD* output){
    using namespace MaterialShaderObserver;++device->render_state_queries;*output=0;
    switch(field){
    case D3DRS_ALPHABLENDENABLE:case D3DRS_ALPHATESTENABLE:*output=device->blob_state?1u:0u;break;
    case D3DRS_SRCBLEND:case D3DRS_DESTBLENDALPHA:*output=D3DBLEND_ZERO;break;
    case D3DRS_DESTBLEND:*output=D3DBLEND_INVSRCCOLOR;break;
    case D3DRS_SRCBLENDALPHA:*output=D3DBLEND_ONE;break;
    case D3DRS_BLENDOP:case D3DRS_BLENDOPALPHA:*output=D3DBLENDOP_ADD;break;
    case D3DRS_ZENABLE:*output=D3DZB_TRUE;break;
    case D3DRS_ZFUNC:*output=D3DCMP_EQUAL;break;
    case D3DRS_ALPHAFUNC:*output=D3DCMP_GREATER;break;
    case D3DRS_ALPHAREF:*output=100;break;
    case D3DRS_COLORWRITEENABLE:*output=15;break;
    case D3DRS_STENCILMASK:case D3DRS_STENCILWRITEMASK:*output=0xffffffffu;break;
    case D3DRS_STENCILFUNC:case D3DRS_CCW_STENCILFUNC:*output=D3DCMP_ALWAYS;break;
    case D3DRS_STENCILFAIL:case D3DRS_STENCILZFAIL:case D3DRS_STENCILPASS:
    case D3DRS_CCW_STENCILFAIL:case D3DRS_CCW_STENCILZFAIL:case D3DRS_CCW_STENCILPASS:*output=D3DSTENCILOP_KEEP;break;
    case D3DRS_CULLMODE:*output=D3DCULL_NONE;break;
    default:break;
    }
    for(size_t i=0;i<CapturedRenderStateCount;++i)if(CapturedRenderStateTypes[i]==field&&(device->fail_render_states&(1u<<i))){
        *output=0xdeadbeefu;return D3DERR_INVALIDCALL; // Poisoned failed output must never become a valid zero/default.
    }
    return S_OK;
}
void Write(const std::wstring& path,const void* data,size_t bytes) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file!=INVALID_HANDLE_VALUE,"new fixture output");DWORD count=0;
    const bool valid=WriteFile(file,data,static_cast<DWORD>(bytes),&count,nullptr) && count==bytes;
    CloseHandle(file);Check(valid,"fixture write");
}
Stats Snapshot(){Stats value{};Check(ReadStats(&value),"stats read");return value;}
std::string ReadText(const std::wstring& path){
    HANDLE file=CreateFileW(path.c_str(),GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    Check(file!=INVALID_HANDLE_VALUE,"read fixture report");LARGE_INTEGER size{};
    const bool sized=GetFileSizeEx(file,&size)&&size.QuadPart>=0&&size.QuadPart<=1024*1024;
    if(!sized){CloseHandle(file);Check(false,"bounded report size");}
    std::string text(static_cast<size_t>(size.QuadPart),'\0');DWORD got=0;
    const bool valid=ReadFile(file,text.data(),static_cast<DWORD>(text.size()),&got,nullptr)&&got==text.size();
    CloseHandle(file);Check(valid,"read complete report");return text;
}
}

int wmain(int argc,wchar_t** argv) {
    using namespace MaterialShaderObserver;
    try {
        Check(argc==2,"use a new fixture output directory");
        const std::wstring directory=argv[1];Check(CreateDirectoryW(directory.c_str(),nullptr)!=0,"new fixture directory");
        std::array<void*,5> shader_table{};
        shader_table[1]=reinterpret_cast<void*>(&AddRef);shader_table[2]=reinterpret_cast<void*>(&Release);
        shader_table[4]=reinterpret_cast<void*>(&GetFunction);
        FakeShader vertex{shader_table.data(),1,0,false,Program(0xfffe0300,0)};
        FakeShader pixel{shader_table.data(),1,0,false,Program(0xffff0300,0)};
        std::array<void*,134> device_table{};device_table[93]=reinterpret_cast<void*>(&Vertex);device_table[108]=reinterpret_cast<void*>(&Pixel);
        device_table[95]=reinterpret_cast<void*>(&Constants);device_table[110]=reinterpret_cast<void*>(&Constants);device_table[64]=reinterpret_cast<void*>(&Texture);
        device_table[38]=reinterpret_cast<void*>(&Target);device_table[48]=reinterpret_cast<void*>(&Viewport);device_table[58]=reinterpret_cast<void*>(&RenderState);
        device_table[36]=reinterpret_cast<void*>(&Offscreen);device_table[32]=reinterpret_cast<void*>(&Readback);
        std::array<void*,22> texture_table{};texture_table[1]=reinterpret_cast<void*>(&TextureAddRef);texture_table[2]=reinterpret_cast<void*>(&TextureRelease);
        texture_table[10]=reinterpret_cast<void*>(&TextureType);texture_table[13]=reinterpret_cast<void*>(&TextureLevels);texture_table[17]=reinterpret_cast<void*>(&TextureDesc);
        FakeTexture texture{texture_table.data(),1};
        std::array<void*,17> surface_table{};surface_table[1]=reinterpret_cast<void*>(&SurfaceAddRef);surface_table[2]=reinterpret_cast<void*>(&SurfaceRelease);surface_table[12]=reinterpret_cast<void*>(&SurfaceDesc);
        surface_table[13]=reinterpret_cast<void*>(&SurfaceLock);surface_table[14]=reinterpret_cast<void*>(&SurfaceUnlock);
        FakeSurface surface{surface_table.data(),1};
        FakeDevice owner{device_table.data(),&vertex,&pixel,&texture,false};
        auto* device=reinterpret_cast<IDirect3DDevice9*>(&owner);
        std::array<unsigned char,16+3*sizeof(CatalogEntry)> catalog{};
        std::memcpy(catalog.data(),"S7MSCAT1",8);DWORD version=1,rows=3;
        std::memcpy(catalog.data()+8,&version,4);std::memcpy(catalog.data()+12,&rows,4);
        std::array<CatalogEntry,3> entries{{{1,1,1,static_cast<std::uint32_t>(vertex.code.size()),Hash(vertex.code.data(),vertex.code.size())},
                                          {2,1,2,static_cast<std::uint32_t>(pixel.code.size()),Hash(pixel.code.data(),pixel.code.size())},
                                          {3,2,2,static_cast<std::uint32_t>(pixel.code.size()),Hash(pixel.code.data(),pixel.code.size())}}};
        std::memcpy(catalog.data()+16,entries.data(),sizeof(entries));
        const auto catalog_hash=Hash(catalog.data(),catalog.size());
        const std::wstring path=directory+L"/catalog.bin",report=directory+L"/observer.private.json";
        Write(path,catalog.data(),catalog.size());
        std::array<unsigned char,32> wrong{};
        Check(!Initialize(path.c_str(),wrong.data(),report.c_str()),"wrong catalog pin must fail");
        SetLastError(0x37570001);
        Check(Initialize(path.c_str(),catalog_hash.data(),report.c_str()),"catalog initialization");
        Check(GetLastError()==0x37570001,"initialization preserves LastError");
        Stats wrong_size{};wrong_size.size-=128;Check(!ReadStats(&wrong_size),"old or mismatched output-Stats size is refused before copying");
        CaptureConfig capture{};capture.target_count=1;capture.frame_stride=1;
        auto& target=capture.targets[0];target.vertex_id=1;target.pixel_id=2;target.sample_limit=2;
        target.constant_count=2;target.sampler_mask=1u<<3;target.constants[0]={1,68,15};target.constants[1]={2,100,7};
        Check(ConfigureCapture(&capture),"configure exact matched-pair capture");
        ObserveDraw(device,Draw,E_FAIL);Check(Snapshot().failed_draws==1 && !Snapshot().shader_queries,"failed draw is not binding evidence");
        const unsigned original_mxcsr=_mm_getcsr();_mm_setcsr(original_mxcsr^0x2000u);const unsigned observed_mxcsr=_mm_getcsr();
        SetLastError(0x37570002);
        ObserveDraw(device,Draw,S_OK);
        Check(GetLastError()==0x37570002,"draw observation preserves LastError");
        Check(_mm_getcsr()==observed_mxcsr,"observer preserves caller MXCSR");_mm_setcsr(original_mxcsr);
        auto first=Snapshot();Check(first.shader_reads==2 && first.cached_refs==2 && vertex.refs==2 && pixel.refs==2,"cache owns exact shader references");
        Check(sizeof(CaptureConfig)==952&&first.captures[0].render_states.available==AllCapturedRenderStates,"input ABI remains952 and successful state availability is complete");
        Check(first.captures[0].render_states.values[static_cast<size_t>(CapturedRenderState::SourceBlend)]==D3DBLEND_ZERO
            &&first.captures[0].render_states.values[static_cast<size_t>(CapturedRenderState::DepthWriteEnable)]==0,"successful zero state is observed, not missing");
        Check(first.records[0].draws==1 && first.records[1].draws==1 && first.records[2].draws==1,"all identity aliases counted");
        Check(first.pair_count==1 && first.pairs[0].pixel_aliases==2,"pair ambiguity retained");
        Check(first.capture_sample_count==1 && first.captures[0].constants[0].valid
            && first.captures[0].constants[0].value[0]==68.0f && first.captures[0].constants[1].value[3]==0.0f,
            "capture correct stage, bounds and selected components only");
        Check(first.captures[0].samplers[0].valid && first.captures[0].samplers[0].width==512
            && first.captures[0].samplers[0].height==256 && texture.refs==1,"sampler description only and balanced refs");
        Check(first.captures[0].viewport_width==1920 && first.captures[0].context_valid==6,"draw context validity distinguishes missing target");
        const auto first_state_queries=owner.render_state_queries;
        ObserveDraw(device,Indexed,S_OK);Check(Snapshot().shader_reads==2 && Snapshot().cache_hits==2,"immutable shader cache hit");
        Check(owner.render_state_queries==first_state_queries,"no new render-state queries outside selected-target stride eligibility");
        Check(Snapshot().capture_sample_count==1,"capture frame stride is bounded");
        EndFrame(device);Check(vertex.refs==1 && pixel.refs==1 && !Snapshot().cached_refs,"frame releases cache");
        pixel.code=Program(0xffff0300,1);ObserveDraw(device,Draw,S_OK);
        Check(Snapshot().unknown_count==1 && Snapshot().records[1].draws==2,"reused address gets fresh hash after frame");
        Check(Snapshot().capture_sample_count==1,"nonmatching pixel program is not sampled");
        Reset(device);Check(vertex.refs==1 && pixel.refs==1 && !Snapshot().cached_refs,"external device Release/reset cleanup");
        pixel.code=Program(0xffff0300,0);pixel.change_size=true;ObserveDraw(device,Draw,S_OK);
        Check(Snapshot().query_failures==1 && pixel.refs==1,"inconsistent GetFunction size refused without leaked ref");
        Reset(device);pixel.change_size=false;
        pixel.code.assign(256*1024+4,0);owner.vertex=nullptr;ObserveDraw(device,Draw,S_OK);
        Check(Snapshot().oversize_programs==1 && pixel.refs==1,"oversize program refused");Reset(device);
        pixel.code=Program(0xffff0300,0);owner.vertex=&vertex;
        ObserveDraw(device,Draw,S_OK);
        FakeDevice other{device_table.data(),&vertex,&pixel};
        ObserveDraw(reinterpret_cast<IDirect3DDevice9*>(&other),Draw,S_OK);
        std::thread worker([&]{ObserveDraw(device,Draw,S_OK);});worker.join();
        Check(Snapshot().context_rejections==2,"device/thread rejection");Reset(device);
        ObserveDraw(device,Draw,S_OK);Check(Snapshot().cached_refs==2,"prepare foreign-thread cleanup");
        bool foreign_error_preserved=false;
        std::thread cleanup([&]{SetLastError(0x37570003);Reset(device);foreign_error_preserved=GetLastError()==0x37570003;});cleanup.join();
        Check(foreign_error_preserved && !Snapshot().cached_refs && vertex.refs==1 && pixel.refs==1,
              "same-device foreign-thread Release cleanup preserves LastError and releases references");
        Check(Snapshot().capture_sample_count<=2,"per-target capture limit");
        auto invalid=capture;invalid.targets[0].constants[0].index=256;
        Check(!ConfigureCapture(&invalid),"out-of-range constant capture refused");
        invalid=capture;invalid.target_count=2;invalid.targets[1]=invalid.targets[0];
        Check(!ConfigureCapture(&invalid),"duplicate alias-pair capture refused");
        Check(ConfigureCapture(&capture),"reset capture series");owner.nonfinite=true;ObserveDraw(device,Draw,S_OK);
        Check(Snapshot().capture_sample_count==1 && !Snapshot().captures[0].constants[0].valid
            && Snapshot().capture_failures>0,"nonfinite constants are never exported as numbers");owner.nonfinite=false;Reset(device);
        // Partial failures remain individually unavailable even when a driver
        // writes a nonzero poison output on failure. Later fields are still read.
        owner.surface=&surface;owner.blob_state=true;
        Check(ConfigureCapture(&capture),"fresh partial render-state series");
        owner.fail_render_states=RenderStateBit(CapturedRenderState::SourceBlend)|RenderStateBit(CapturedRenderState::StencilReference);
        const auto queries_before_partial=owner.render_state_queries;SetLastError(0x37570031);ObserveDraw(device,Draw,S_OK);
        Check(GetLastError()==0x37570031,"extended state capture preserves LastError");
        const auto partial=Snapshot().captures[0];
        Check(owner.render_state_queries==queries_before_partial+CapturedRenderStateCount,"all bounded states queried independently after partial failure");
        Check(partial.context_valid==7&&partial.render_states.available==(AllCapturedRenderStates&~owner.fail_render_states),"legacy context remains separate from missing extended states");
        Check(partial.render_states.values[static_cast<size_t>(CapturedRenderState::SourceBlend)]==0
            &&partial.render_states.values[static_cast<size_t>(CapturedRenderState::StencilReference)]==0,"failed poisoned outputs are not stored as measurements");
        Check(Snapshot().render_state_query_failures==2&&Snapshot().render_state_partial_samples==1,"explicit query and partial-sample failure counters");
        Flush();auto partial_json=ReadText(report);
        Check(partial_json.find("\"source_blend\":null")!=std::string::npos
            &&partial_json.find("\"stencil_reference\":null")!=std::string::npos
            &&partial_json.find("\"depth_write_enable\":0")!=std::string::npos,"JSON distinguishes unavailable null from successful zero");
        Check(partial_json.find("\"capture_frame_basis\":\"completed_presents_before_draw\"")!=std::string::npos,"selected sample frame convention is explicit, never implicitly GPU-frame proof");
        Check(ConfigureCapture(&capture),"legacy alpha-state failure series");owner.fail_render_states=RenderStateBit(CapturedRenderState::AlphaTestEnable);ObserveDraw(device,Draw,S_OK);
        Check(Snapshot().captures[0].context_valid==3
            &&!(Snapshot().captures[0].render_states.available&RenderStateBit(CapturedRenderState::AlphaTestEnable)),"failed legacy alpha state never gets context-ready bit");
        EndFrame(device);owner.fail_render_states=0;ObserveDraw(device,Draw,S_OK);
        Check(Snapshot().captures[1].render_states.available==AllCapturedRenderStates
            &&Snapshot().captures[1].render_states.values[static_cast<size_t>(CapturedRenderState::AlphaTestEnable)]==1,"later success recovers without stale unavailable fields");
        const auto exhausted_queries=owner.render_state_queries;ObserveDraw(device,Draw,S_OK);EndFrame(device);ObserveDraw(device,Draw,S_OK);
        Check(owner.render_state_queries==exhausted_queries,"exhausted selected-target budget adds no hot per-draw state overhead");
        owner.blob_state=false;Reset(device);
        Check(sizeof(CaptureSample)==984&&sizeof(Stats)==80080,"UP provenance adds no public output ABI fields");
        Check(ConfigureCapture(&capture),"fresh UP-kind capture series");
        const auto queries_before_up=Snapshot().shader_queries;
        ObserveDraw(device,DrawUP,E_FAIL);Check(Snapshot().shader_queries==queries_before_up,"failed UP draw is never observed as successful binding");
        ObserveDraw(device,DrawUP,S_OK);EndFrame(device);ObserveDraw(device,IndexedUP,S_OK);Flush();
        const auto up_json=ReadText(report);
        Check(up_json.find("\"draw_kind\":3")!=std::string::npos&&up_json.find("\"draw_kind\":4")!=std::string::npos,
            "selected successful UP kinds retained as distinct JSON provenance");
        const auto before_invalid_kind=Snapshot().shader_queries;ObserveDraw(device,99,S_OK);
        Check(Snapshot().shader_queries==before_invalid_kind,"unknown draw kind refused before shader queries");
        Reset(device);
        // More than the full cache's population of SAME-SIZE wrong programs
        // must not consume positive refs or hide a later real known pair.
        owner.vertex=nullptr;
        std::vector<FakeShader> negatives;negatives.reserve(257);
        for(unsigned i=0;i<257;++i)negatives.push_back(FakeShader{shader_table.data(),1,0,false,Program(0xffff0300,i+100)});
        for(auto& shader:negatives){owner.pixel=&shader;ObserveDraw(device,DrawUP,S_OK);}
        Check(Snapshot().cached_refs==0,"negative same-sized programs never consume positive cache capacity");
        for(const auto& shader:negatives)Check(shader.refs==1,"negative shader query references balanced");
        owner.vertex=&vertex;owner.pixel=&pixel;Check(ConfigureCapture(&capture),"late known-pair capture series");
        const auto known_before=Snapshot().records[0].draws;ObserveDraw(device,IndexedUP,S_OK);
        Check(Snapshot().records[0].draws==known_before+1&&Snapshot().capture_sample_count==1,"known pair still counted/captured after257 wrong same-size programs");
        Reset(device);
        owner.vertex=nullptr;
        std::vector<FakeShader> many;many.reserve(300);
        for(unsigned index=0;index<300;++index)many.push_back(FakeShader{shader_table.data(),1,0,false,Program(0xffff0300,0)});
        for(auto& item:many){owner.pixel=&item;ObserveDraw(device,Draw,S_OK);}
        Check(Snapshot().cache_overflows==0 && Snapshot().cached_refs==256,"positive cache bounded without refusing full-capacity classification");
        owner.vertex=&vertex;owner.pixel=&pixel;Check(ConfigureCapture(&capture),"full positive-cache known-pair capture");
        const auto full_before=Snapshot().records[0].draws;ObserveDraw(device,DrawUP,S_OK);
        Check(Snapshot().records[0].draws==full_before+1&&Snapshot().capture_sample_count==1&&Snapshot().cached_refs==256,
            "full positive cache still counts/captures fresh known VS+PS through independent temporary metadata");
        Flush();Check(ReadText(report).find("\"cache_capacity_bypasses\":46,")!=std::string::npos,"44 full-cache known programs plus2 fresh stage identities bypass cache without query failure");
        Reset(device);for(const auto& item:many)Check(item.refs==1,"bounded cache released every shader");
        // Frame-coupled raw uniforms use authored fixture identities only. The
        // production observer's River hashes remain fixed when this macro is off.
        owner.vertex=&vertex;owner.pixel=&pixel;
        owner.surface=&surface;
        const auto vertex_hash=Hash(vertex.code.data(),vertex.code.size()),pixel_hash=Hash(pixel.code.data(),pixel.code.size());
        Check(FixtureFrameIdentity(vertex_hash.data(),static_cast<UINT>(vertex.code.size()),pixel_hash.data(),static_cast<UINT>(pixel.code.size())),"fixture-only frame identities");
        Check(ConfigureFrameSnapshot(1920,1080),"configure trusted frame target");owner.unused_nan=true;
        ObserveDraw(device,Draw,S_OK);const auto next=Snapshot().frames+1;FrameSnapshot frame{};
        Check(!CopyLatestFrameSnapshot(next,&frame)&&!frame.available,"unpresented frame unavailable");
        const unsigned reads=owner.uniform_reads;ObserveDraw(device,Draw,S_OK);Check(owner.uniform_reads==reads,"raw frame uniforms read only once per eligible frame");
        EndFrame(device);Check(CopyLatestFrameSnapshot(next,&frame)&&frame.available&&frame.frame==next,"exact presented frame snapshot");
        Check(frame.vs16_eye[0]==16&&frame.ps5_sun_direction[0]==5&&frame.ps7_sun_specular[0]==7&&surface.refs==1,"raw selected uniforms and balanced context references");
        Check(frame.vertex_id==1&&frame.pixel_id==2&&frame.pixel_aliases==2,"actual catalog IDs and aliases retained");
        Check(!CopyLatestFrameSnapshot(next-1,&frame)&&!frame.available,"stale frame unavailable");Reset(device);
        Check(CopyLatestFrameSnapshot(next,&frame),"ordinary ref cleanup retains copied frame data");
        EndFrame(device);Check(!CopyLatestFrameSnapshot(next+1,&frame),"frame without matching original geometry unavailable");
        owner.nonfinite=true;ObserveDraw(device,Draw,S_OK);const auto invalid_frame=Snapshot().frames+1;EndFrame(device);
        Check(!CopyLatestFrameSnapshot(invalid_frame,&frame),"nonfinite meaningful uniform rejected");owner.nonfinite=false;
        ObserveDraw(device,Draw,S_OK);const auto valid_frame=Snapshot().frames+1;EndFrame(device);Check(CopyLatestFrameSnapshot(valid_frame,&frame),"later valid frame recovers");
        InvalidateFrameSnapshot();Check(!CopyLatestFrameSnapshot(valid_frame,&frame),"actual Reset invalidates prior frame data");
        owner.unused_nan=false;
        CaptureConfig no_capture{};Check(ConfigureCapture(&no_capture),"disable fixed register capture for trace tests");
        DrawTraceConfig trace{};trace.sequence=1;
        auto bad_trace=trace;bad_trace.version=1;Check(!ConfigureDrawTrace(&bad_trace),"trace never reinterprets legacy version");
        bad_trace=trace;bad_trace.sequence=0;Check(!ConfigureDrawTrace(&bad_trace),"trace nonzero sequence");
        bad_trace=trace;bad_trace.width=1;Check(!ConfigureDrawTrace(&bad_trace),"inventory rejects dormant ROI fields");
        Check(ConfigureDrawTrace(&trace),"one frame trace armed");
        Check(!ConfigureDrawTrace(&trace),"pending trace cannot be replaced");
        ObserveDraw(device,Indexed,S_OK);Check(trace_readbacks==0,"no readback while armed");
        EndFrame(device);ObserveDraw(device,Draw,E_FAIL);ObserveDraw(device,IndexedUP,S_OK);ObserveDraw(device,DrawUP,S_OK);EndFrame(device);
        const auto trace1=ReadText(report+L".trace-1.json");
        Check(trace1.find("\"successful_draws\":2,")!=std::string::npos&&trace1.find("\"overflows\":0,")!=std::string::npos,"exact complete next-frame successful draw count");
        Check(trace1.find("\"kind\":4,")!=std::string::npos&&trace1.find("\"kind\":3,")!=std::string::npos,"trace covers both UP paths without buffer access");
        Check(trace1.find("\"target\":[1920,1080,21]")!=std::string::npos&&trace1.find("\"available\":15,")!=std::string::npos,"actual trace target and explicit availability");
        Check(trace_readbacks==0&&!ConfigureDrawTrace(&trace),"inventory never reads pixels and sequence never replays");
        trace.sequence=2;trace.first_draw=2;trace.draw_count=2;trace.target_width=1920;trace.target_height=1080;
        trace.x=7;trace.y=5;trace.width=3;trace.height=2;
        bad_trace=trace;bad_trace.draw_count=9;Check(!ConfigureDrawTrace(&bad_trace),"readback budget at most eight");
        bad_trace=trace;bad_trace.width=257;Check(!ConfigureDrawTrace(&bad_trace),"ROI width bounded");
        bad_trace=trace;bad_trace.x=1919;Check(!ConfigureDrawTrace(&bad_trace),"ROI bounds avoid overflow");
        bad_trace=trace;bad_trace.first_draw=32768;Check(!ConfigureDrawTrace(&bad_trace),"last consecutive ordinal bounded");
        trace_pixels.resize((1920*4+16)*1080);
        for(size_t i=0;i<trace_pixels.size();++i)trace_pixels[i]=static_cast<unsigned char>(i%251);
        Check(ConfigureDrawTrace(&trace),"bounded ROI trace armed");EndFrame(device);
        ObserveDraw(device,Indexed,S_OK);Check(trace_readbacks==0,"no readback outside selected ordinal");
        SetLastError(0x37573333);const auto trace_mxcsr=_mm_getcsr();ObserveDraw(device,Indexed,S_OK);
        Check(GetLastError()==0x37573333&&_mm_getcsr()==trace_mxcsr,"readback preserves LastError and FP state");
        trace_fail=true;ObserveDraw(device,DrawUP,S_OK);trace_fail=false;EndFrame(device);
        Check(trace_readbacks==2&&trace_locks==1&&trace_unlocks==1&&surface.refs==1,"success/failure readbacks balance surfaces and lock lifecycle");
        const auto raw_roi=ReadText(report+L".trace-2-draw-2.raw");
        std::string expected_roi;
        for(unsigned y=5;y<7;++y)expected_roi.append(reinterpret_cast<const char*>(trace_pixels.data())+y*(1920*4+16)+7*4,12);
        Check(raw_roi==expected_roi,"ROI copies exact source bytes with padded pitch and no recoloring");
        const auto trace2=ReadText(report+L".trace-2.json");
        Check(trace2.find("\"failures\":1,")!=std::string::npos&&trace2.find("\"roi_hresult\":2147500037")!=std::string::npos,"failed readback reported explicitly");
        trace.sequence=3;trace.first_draw=300;Check(ConfigureDrawTrace(&trace),"unreached selected ordinal trace armed");
        EndFrame(device);ObserveDraw(device,Indexed,S_OK);EndFrame(device);
        Check(ReadText(report+L".trace-3.json").find("\"missing_readbacks\":2")!=std::string::npos,"an unreached requested draw is never an implicit readback success");
        trace=DrawTraceConfig{};trace.sequence=4;Check(ConfigureDrawTrace(&trace),"cancellation trace armed");
        SetLastError(0x37570004);Flush();Check(GetLastError()==0x37570004,"report flush preserves LastError");
        Shutdown();Check(!Snapshot().enabled,"shutdown disables observer");
        Check(ReadText(report+L".trace-4.json").find("\"cancelled\":true")!=std::string::npos,"shutdown cancels pending trace with explicit incomplete receipt");
        Check(vertex.refs==1 && pixel.refs==1,"all original references balanced");
        std::printf("{\"format\":\"s7-observer-render-state-cpu-v1\",\"checks\":%u,\"passed\":true,\"capture_config_bytes\":%u,\"capture_sample_bytes\":%u,\"stats_bytes\":%u,\"render_state_fields\":%u,\"game_started\":false,\"gpu_started\":false}\n",
            checks.load(),static_cast<unsigned>(sizeof(CaptureConfig)),static_cast<unsigned>(sizeof(CaptureSample)),
            static_cast<unsigned>(sizeof(Stats)),static_cast<unsigned>(CapturedRenderStateCount));return 0;
    } catch(const std::exception& error) {std::fprintf(stderr,"material observer fixture failed: %s\n",error.what());return 1;}
}
