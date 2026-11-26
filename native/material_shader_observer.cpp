#include "material_shader_observer.h"
#include <bcrypt.h>
#include <algorithm>
#include <cstdio>
#include <cstring>
#include <limits>
#include <string>
#include <utility>
#include <intrin.h>
#include <vector>
#include <memory>

namespace MaterialShaderObserver {
namespace {
constexpr size_t MaxCache=256,MaxCodeBytes=256*1024;
constexpr size_t MaxCatalogBytes=16+MaxCatalog*sizeof(CatalogEntry);
struct Cached {
    IUnknown* object=nullptr;
    std::uint32_t stage=0,bytes=0,representative=0,aliases=0;
    std::array<unsigned char,32> sha256{};
};
constexpr size_t MaxTraceDraws=32768;
constexpr std::array<D3DSAMPLERSTATETYPE,8> UiSamplerStates{{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV,
    D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER,D3DSAMP_MIPFILTER,D3DSAMP_MAXANISOTROPY,D3DSAMP_MIPMAPLODBIAS,D3DSAMP_SRGBTEXTURE}};
struct UiTextureTrace {
    DWORD available=0,bound=0,type=0,width=0,height=0,format=0,levels=0,state_available=0;
    std::uintptr_t identity=0;
    std::array<DWORD,8> states{};
};
struct UiTrace {
    DWORD available=0,fvf=0,stride=0,offset=0,buffer_bytes=0,buffer_usage=0,buffer_pool=0;
    RECT scissor{};
    std::array<DWORD,16> matrix_bits{};
    RenderStateSample render_states{};
    std::array<UiTextureTrace,4> textures{};
};
struct TraceRow {
    std::uint32_t ordinal=0,kind=0,vertex=0,pixel=0,vertex_bytes=0,pixel_bytes=0;
    std::array<unsigned char,32> vertex_sha256{},pixel_sha256{},roi_sha256{};
    DWORD available=0,width=0,height=0,format=0,viewport_x=0,viewport_y=0,viewport_width=0,viewport_height=0;
    HRESULT readback=S_FALSE;
    std::uintptr_t target_identity=0;
    std::unique_ptr<UiTrace> ui;
};
struct TraceState {
    DrawTraceConfig config{};
    bool armed=false,active=false;
    DWORD last_sequence=0,draws=0,overflows=0,failures=0;
    std::uint64_t frame=0;
    std::vector<TraceRow> rows;
};
struct State {
    Stats stats{};
    std::array<CatalogEntry,MaxCatalog> catalog{};
    std::array<Cached,MaxCache> cache{};
    std::array<Cached,2> uncached{}; // Separate metadata lifetimes for this draw's VS and PS.
    std::uint64_t cache_capacity_bypasses=0;
    std::array<unsigned char,MaxCodeBytes> scratch{};
    BCRYPT_ALG_HANDLE algorithm=nullptr;
    HANDLE catalog_file=INVALID_HANDLE_VALUE;
    IDirect3DDevice9* device=nullptr;
    DWORD thread=0;
    size_t cache_count=0;
    std::wstring report_path;
    std::array<unsigned char,32> catalog_digest{};
    CaptureConfig capture{};
    // JSON-only provenance; public CaptureSample/Stats layout stays unchanged.
    std::array<std::uint32_t,MaxCaptureSamples> capture_kinds{};
    std::array<std::uint32_t,MaxCaptureTargets> captured{};
    std::array<std::uint64_t,MaxCaptureTargets> last_capture{};
    DWORD frame_width=0,frame_height=0;
    std::uint64_t frame_attempt=0;
    FrameSnapshot latest{};
    std::array<unsigned char,32> frame_vertex{},frame_pixel{};
    UINT frame_vertex_bytes=2592,frame_pixel_bytes=3740;
    TraceState trace{};
    std::array<unsigned char,32> ui_vertex{},ui_pixel{};
    UINT ui_vertex_bytes=484,ui_pixel_bytes=1108;
};
SRWLOCK mutex=SRWLOCK_INIT;
State current;
struct Lock {Lock(){AcquireSRWLockExclusive(&mutex);}~Lock(){ReleaseSRWLockExclusive(&mutex);}};
template<class T>struct Ref {
    T* value=nullptr;
    Ref()=default;Ref(const Ref&)=delete;Ref& operator=(const Ref&)=delete;
    ~Ref(){if(value)value->Release();}
    void reset(){auto* original=value;value=nullptr;if(original)original->Release();}
};
struct LastErrorState {
    DWORD value=GetLastError();
    ~LastErrorState(){SetLastError(value);}
};
struct FloatingState {
    alignas(16) std::array<unsigned char,512> bytes{};
    FloatingState(){_fxsave(bytes.data());}
    ~FloatingState(){_fxrstor(bytes.data());}
};
void Exception() noexcept {Lock lock;++current.stats.exceptions;}
void DigestLiteral(const char* text,std::array<unsigned char,32>& output) {
    const auto digit=[](char value){return value<='9'?value-'0':value-'a'+10;};
    for(size_t index=0;index<32;++index)output[index]=static_cast<unsigned char>((digit(text[index*2])<<4)|digit(text[index*2+1]));
}

bool Hash(const void* data,size_t bytes,std::array<unsigned char,32>& result) {
    if(!current.algorithm || bytes>std::numeric_limits<ULONG>::max())return false;
    BCRYPT_HASH_HANDLE hash=nullptr;
    bool valid=BCryptCreateHash(current.algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(valid)valid=BCryptHashData(hash,const_cast<PUCHAR>(static_cast<const unsigned char*>(data)),
                                static_cast<ULONG>(bytes),0)>=0;
    if(valid)valid=BCryptFinishHash(hash,result.data(),static_cast<ULONG>(result.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);
    return valid;
}

std::string Hex(const std::array<unsigned char,32>& digest) {
    constexpr char digits[]="0123456789abcdef";
    std::string value(64,'0');
    for(size_t index=0;index<digest.size();++index){value[index*2]=digits[digest[index]>>4];value[index*2+1]=digits[digest[index]&15];}
    return value;
}

std::wstring TracePath(DWORD sequence,DWORD draw=0) {
    return current.report_path+L".trace-"+std::to_wstring(sequence)
        +(draw?L"-draw-"+std::to_wstring(draw)+L".raw":L".json");
}
bool NewFile(const std::wstring& path,const void* data,DWORD bytes) {
    HANDLE file=CreateFileW(path.c_str(),GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;BY_HANDLE_FILE_INFORMATION info{};
    const bool valid=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        &&WriteFile(file,data,bytes,&written,nullptr)&&written==bytes&&FlushFileBuffers(file);
    CloseHandle(file);return valid;
}
std::unique_ptr<UiTrace> ReadUiTrace(IDirect3DDevice9* device,Cached* vertex,Cached* pixel) {
    if(!vertex||!pixel||vertex->bytes!=current.ui_vertex_bytes||pixel->bytes!=current.ui_pixel_bytes
        ||vertex->sha256!=current.ui_vertex||pixel->sha256!=current.ui_pixel)return {};
    auto s=std::make_unique<UiTrace>();
    RECT scissor{};
    if(SUCCEEDED(device->GetScissorRect(&scissor))){s->scissor=scissor;s->available|=1;}
    std::array<float,16> matrix{};
    if(SUCCEEDED(device->GetVertexShaderConstantF(64,matrix.data(),4))) {
        std::memcpy(s->matrix_bits.data(),matrix.data(),sizeof(matrix));s->available|=2;
    }
    DWORD fvf=0;if(SUCCEEDED(device->GetFVF(&fvf))){s->fvf=fvf;s->available|=4;}
    Ref<IDirect3DVertexBuffer9> buffer;UINT offset=0,stride=0;
    if(SUCCEEDED(device->GetStreamSource(0,&buffer.value,&offset,&stride))) {
        s->available|=8;s->offset=offset;s->stride=stride;
        D3DVERTEXBUFFER_DESC desc{};
        if(buffer.value&&SUCCEEDED(buffer.value->GetDesc(&desc))) {
            s->available|=16;s->buffer_bytes=desc.Size;s->buffer_usage=desc.Usage;s->buffer_pool=desc.Pool;
        }
    }
    for(size_t i=0;i<CapturedRenderStateCount;++i) {
        DWORD value=0;if(SUCCEEDED(device->GetRenderState(CapturedRenderStateTypes[i],&value))) {
            s->render_states.available|=1u<<i;s->render_states.values[i]=value;
        }
    }
    for(DWORD i=0;i<4;++i) {
        auto& t=s->textures[i];Ref<IDirect3DBaseTexture9> texture;
        if(SUCCEEDED(device->GetTexture(i,&texture.value))) {
            t.available|=1;
            if(texture.value) {
                t.bound=1;t.identity=reinterpret_cast<std::uintptr_t>(texture.value);t.type=texture.value->GetType();
                if(t.type==D3DRTYPE_TEXTURE) {
                    auto* image=static_cast<IDirect3DTexture9*>(texture.value);D3DSURFACE_DESC desc{};
                    if(SUCCEEDED(image->GetLevelDesc(0,&desc))) {
                        t.available|=2;t.width=desc.Width;t.height=desc.Height;t.format=desc.Format;t.levels=image->GetLevelCount();
                    }
                }
            }
        }
        for(size_t k=0;k<UiSamplerStates.size();++k) {
            DWORD value=0;if(SUCCEEDED(device->GetSamplerState(i,UiSamplerStates[k],&value))) {
                t.state_available|=1u<<k;t.states[k]=value;
            }
        }
    }
    return s;
}
std::string UiJson(const UiTrace& s) {
    std::string j="{\"schema\":1,\"available\":"+std::to_string(s.available)
        +",\"scissor\":["+std::to_string(s.scissor.left)+","+std::to_string(s.scissor.top)+","+std::to_string(s.scissor.right)+","+std::to_string(s.scissor.bottom)
        +"],\"gui_matrix_c64_67_bits\":[";
    for(size_t i=0;i<s.matrix_bits.size();++i){if(i)j+=',';j+=std::to_string(s.matrix_bits[i]);}
    j+="],\"fvf\":"+std::to_string(s.fvf)+",\"stream_offset\":"+std::to_string(s.offset)+",\"stream_stride\":"+std::to_string(s.stride)
        +",\"buffer_bytes\":"+std::to_string(s.buffer_bytes)+",\"buffer_usage\":"+std::to_string(s.buffer_usage)+",\"buffer_pool\":"+std::to_string(s.buffer_pool)
        +",\"render_state_available\":"+std::to_string(s.render_states.available)+",\"render_states\":{"
        ;
    for(size_t i=0;i<CapturedRenderStateCount;++i) {
        if(i)j+=',';j+='\"';j+=CapturedRenderStateNames[i];j+="\":";
        j+=(s.render_states.available&(1u<<i))?std::to_string(s.render_states.values[i]):"null";
    }
    j+="},\"textures\":[";
    for(size_t i=0;i<s.textures.size();++i) {
        const auto& t=s.textures[i];if(i)j+=',';
        j+="{\"sampler\":"+std::to_string(i)+",\"available\":"+std::to_string(t.available)+",\"bound\":"+std::to_string(t.bound)
            +",\"identity\":"+std::to_string(t.identity)+",\"type\":"+std::to_string(t.type)+",\"width\":"+std::to_string(t.width)
            +",\"height\":"+std::to_string(t.height)+",\"format\":"+std::to_string(t.format)+",\"levels\":"+std::to_string(t.levels)
            +",\"sampler_state_available\":"+std::to_string(t.state_available)+",\"sampler_states\":[";
        for(size_t k=0;k<t.states.size();++k){if(k)j+=',';j+=(t.state_available&(1u<<k))?std::to_string(t.states[k]):"null";}
        j+="]}";
    }
    return j+"]}";
}
// No device state changes. Read back the current target with its original
// format, then retain only the explicitly bounded 32-bit ROI, row-major.
HRESULT TracePixels(IDirect3DDevice9* device,IDirect3DSurface9* target,const D3DSURFACE_DESC& desc,TraceRow& row) {
    const auto& c=current.trace.config;
    if(desc.Width!=c.target_width||desc.Height!=c.target_height
        ||(desc.Format!=D3DFMT_A8R8G8B8&&desc.Format!=D3DFMT_X8R8G8B8&&desc.Format!=D3DFMT_R32F
           &&desc.Format!=D3DFMT_A2R10G10B10)||desc.MultiSampleType!=D3DMULTISAMPLE_NONE)return D3DERR_INVALIDCALL;
    Ref<IDirect3DSurface9> staging;
    HRESULT result=device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&staging.value,nullptr);
    if(FAILED(result)||!staging.value)return FAILED(result)?result:E_FAIL;
    result=device->GetRenderTargetData(target,staging.value);if(FAILED(result))return result;
    D3DLOCKED_RECT locked{};
    result=staging.value->LockRect(&locked,nullptr,D3DLOCK_READONLY);if(FAILED(result))return result;
    std::vector<unsigned char> pixels;
    bool copied=false;
    try {
        if(locked.pBits&&locked.Pitch>0&&static_cast<UINT>(locked.Pitch)>=desc.Width*4) {
            pixels.resize(static_cast<size_t>(c.width)*c.height*4);
            for(DWORD y=0;y<c.height;++y)std::memcpy(pixels.data()+static_cast<size_t>(y)*c.width*4,
                static_cast<const unsigned char*>(locked.pBits)+static_cast<size_t>(c.y+y)*locked.Pitch+c.x*4,c.width*4);
            copied=true;
        }
    }catch(...) {staging.value->UnlockRect();throw;}
    result=staging.value->UnlockRect();if(FAILED(result))return result;
    if(!copied||!Hash(pixels.data(),pixels.size(),row.roi_sha256))return E_FAIL;
    return NewFile(TracePath(c.sequence,row.ordinal),pixels.data(),static_cast<DWORD>(pixels.size()))?S_OK:E_FAIL;
}
void TraceDraw(IDirect3DDevice9* device,Cached* vertex,Cached* pixel,unsigned kind) {
    auto& t=current.trace;if(!t.active)return;
    ++t.draws;if(t.rows.size()>=MaxTraceDraws){++t.overflows;return;}
    TraceRow row{};row.ordinal=t.draws;row.kind=kind;
    if(vertex){row.vertex=vertex->representative;row.vertex_bytes=vertex->bytes;row.vertex_sha256=vertex->sha256;row.available|=1;}
    if(pixel){row.pixel=pixel->representative;row.pixel_bytes=pixel->bytes;row.pixel_sha256=pixel->sha256;row.available|=2;}
    Ref<IDirect3DSurface9> target;D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};
    if(SUCCEEDED(device->GetRenderTarget(0,&target.value))&&target.value&&SUCCEEDED(target.value->GetDesc(&desc))) {
        row.available|=4;row.width=desc.Width;row.height=desc.Height;row.format=static_cast<DWORD>(desc.Format);
        row.target_identity=reinterpret_cast<std::uintptr_t>(target.value);
    }else ++t.failures;
    if(SUCCEEDED(device->GetViewport(&viewport))) {
        row.available|=8;row.viewport_x=viewport.X;row.viewport_y=viewport.Y;
        row.viewport_width=viewport.Width;row.viewport_height=viewport.Height;
    }else ++t.failures;
    if(t.config.draw_count&&row.ordinal>=t.config.first_draw&&row.ordinal-t.config.first_draw<t.config.draw_count) {
        try {row.ui=ReadUiTrace(device,vertex,pixel);}catch(...){++t.failures;}
        try {row.readback=(row.available&4)?TracePixels(device,target.value,desc,row):E_FAIL;}
        catch(...) {row.readback=E_FAIL;}
        if(FAILED(row.readback))++t.failures;
    }
    t.rows.push_back(std::move(row));
}
void FinishTrace(bool cancelled) {
    auto& t=current.trace;
    if(!t.active&&!t.armed)return;
    t.active=t.armed=false;
    DWORD readbacks=0;for(const auto& row:t.rows)if(row.readback==S_OK)++readbacks;
    std::string json="{\"format\":\"s7-original-draw-trace-v2\",\"read_only\":true,\"complete\":";
    json+=cancelled?"false":"true";
    json+=",\"cancelled\":"+std::string(cancelled?"true":"false")+",\"sequence\":"+std::to_string(t.config.sequence)
        +",\"frame_basis\":\"completed_presents_before_draw\",\"frame\":"+std::to_string(t.frame)
        +",\"successful_draws\":"+std::to_string(t.draws)+",\"overflows\":"+std::to_string(t.overflows)
        +",\"failures\":"+std::to_string(t.failures)+",\"catalog_sha256\":\""+Hex(current.catalog_digest)+"\""
        +",\"bound_programs_observed_after_draw\":true,\"requested_first_draw\":"+std::to_string(t.config.first_draw)
        +",\"requested_readbacks\":"+std::to_string(t.config.draw_count)+",\"successful_readbacks\":"+std::to_string(readbacks)
        +",\"missing_readbacks\":"+std::to_string(t.config.draw_count-readbacks)
        +",\"roi\":["+std::to_string(t.config.x)+","+std::to_string(t.config.y)+","+std::to_string(t.config.width)+","+std::to_string(t.config.height)+"],\"draws\":[";
    for(size_t i=0;i<t.rows.size();++i) {
        const auto& r=t.rows[i];if(i)json+=',';
        json+="{\"ordinal\":"+std::to_string(r.ordinal)+",\"kind\":"+std::to_string(r.kind)
            +",\"available\":"+std::to_string(r.available)+",\"vertex_id\":"+std::to_string(r.vertex)+",\"pixel_id\":"+std::to_string(r.pixel)
            +",\"vertex_bytes\":"+std::to_string(r.vertex_bytes)+",\"pixel_bytes\":"+std::to_string(r.pixel_bytes)
            +",\"vertex_sha256\":\""+Hex(r.vertex_sha256)+"\",\"pixel_sha256\":\""+Hex(r.pixel_sha256)+"\""
            +",\"target_identity\":"+std::to_string(r.target_identity)
            +",\"target\":["+std::to_string(r.width)+","+std::to_string(r.height)+","+std::to_string(r.format)+"]"
            +",\"viewport\":["+std::to_string(r.viewport_x)+","+std::to_string(r.viewport_y)+","+std::to_string(r.viewport_width)+","+std::to_string(r.viewport_height)+"]"
            +",\"roi_hresult\":"+std::to_string(static_cast<DWORD>(r.readback))+",\"roi_sha256\":\""+Hex(r.roi_sha256)+"\""
            +",\"ui_state\":"+(r.ui?UiJson(*r.ui):"null")+"}";
    }
    json+="]}\n";
    if(json.size()>MAXDWORD||!NewFile(TracePath(t.config.sequence),json.data(),static_cast<DWORD>(json.size())))++current.stats.report_failures;
    t.rows.clear();
}

void ClearCache() {
    for(size_t index=0;index<current.cache_count;++index) {
        IUnknown* object=current.cache[index].object;
        current.cache[index]=Cached{};
        if(object)object->Release();
    }
    current.cache_count=0;current.stats.cached_refs=0;
    current.uncached.fill(Cached{});
}

bool Context(IDirect3DDevice9* device) {
    if(!device || !current.stats.enabled)return false;
    const DWORD thread=GetCurrentThreadId();
    if((current.thread && current.thread!=thread) || (current.device && current.device!=device)) {
        ++current.stats.context_rejections;return false;
    }
    current.thread=thread;current.device=device;return true;
}

void Report() {
    if(current.report_path.empty())return;
    const auto& s=current.stats;
    char buffer[2048]{};
    const int count=std::snprintf(buffer,sizeof(buffer),
        "{\"format\":\"s7-material-shader-observer-v1\",\"read_only\":true,\"replacement_enabled\":false,"
        "\"enabled\":%u,\"catalog_count\":%u,\"successful_draws\":%llu,\"failed_draws\":%llu,"
        "\"frames\":%llu,\"resets\":%llu,\"cached_refs\":%u,\"peak_cached_refs\":%u,"
        "\"shader_queries\":%llu,\"shader_reads\":%llu,\"cache_hits\":%llu,\"query_failures\":%llu,"
        "\"oversize_programs\":%llu,\"cache_overflows\":%llu,\"context_rejections\":%llu,"
        "\"unknown_overflows\":%llu,\"pair_overflows\":%llu,\"exceptions\":%llu,\"report_failures\":%llu,"
        "\"known_vertex_draws\":%llu,\"known_pixel_draws\":%llu,\"known_pair_draws\":%llu,",
        s.enabled,s.catalog_count,static_cast<unsigned long long>(s.successful_draws),
        static_cast<unsigned long long>(s.failed_draws),static_cast<unsigned long long>(s.frames),
        static_cast<unsigned long long>(s.resets),s.cached_refs,s.peak_cached_refs,
        static_cast<unsigned long long>(s.shader_queries),static_cast<unsigned long long>(s.shader_reads),
        static_cast<unsigned long long>(s.cache_hits),static_cast<unsigned long long>(s.query_failures),
        static_cast<unsigned long long>(s.oversize_programs),static_cast<unsigned long long>(s.cache_overflows),
        static_cast<unsigned long long>(s.context_rejections),static_cast<unsigned long long>(s.unknown_overflows),
        static_cast<unsigned long long>(s.pair_overflows),static_cast<unsigned long long>(s.exceptions),
        static_cast<unsigned long long>(s.report_failures),static_cast<unsigned long long>(s.known_vertex_draws),
        static_cast<unsigned long long>(s.known_pixel_draws),static_cast<unsigned long long>(s.known_pair_draws));
    if(count<=0 || static_cast<size_t>(count)>=sizeof(buffer)){++current.stats.report_failures;return;}
    std::string json(buffer,static_cast<size_t>(count));
    json+="\"cache_capacity_bypasses\":"+std::to_string(current.cache_capacity_bypasses)+",";
    json+="\"catalog_sha256\":\""+Hex(current.catalog_digest)+"\",\"records\":[";
    bool first=true;
    for(size_t index=0;index<s.catalog_count;++index) {
        const auto& row=s.records[index];if(!row.draws)continue;
        if(!first)json+=',';first=false;
        std::snprintf(buffer,sizeof(buffer),"{\"id\":%u,\"family\":%u,\"stage\":%u,\"bytes\":%u,\"draws\":%llu}",
                      row.id,row.family,row.stage,row.bytes,static_cast<unsigned long long>(row.draws));json+=buffer;
    }
    json+="],\"unknown\":[";
    for(size_t index=0;index<s.unknown_count;++index) {
        const auto& row=s.unknown[index];if(index)json+=',';
        std::snprintf(buffer,sizeof(buffer),"{\"stage\":%u,\"bytes\":%u,\"draws\":%llu,\"sha256\":\"",
                      row.stage,row.bytes,static_cast<unsigned long long>(row.draws));
        json+=buffer;json+=Hex(row.sha256);json+="\"}";
    }
    json+="],\"pairs\":[";
    for(size_t index=0;index<s.pair_count;++index) {
        const auto& row=s.pairs[index];if(index)json+=',';
        std::snprintf(buffer,sizeof(buffer),"{\"vertex_id\":%u,\"pixel_id\":%u,\"vertex_aliases\":%u,\"pixel_aliases\":%u,\"draws\":%llu}",
                      row.vertex_id,row.pixel_id,row.vertex_aliases,row.pixel_aliases,static_cast<unsigned long long>(row.draws));json+=buffer;
    }
    std::snprintf(buffer,sizeof(buffer),"],\"capture_target_count\":%u,\"capture_failures\":%llu,\"capture_frame_basis\":\"completed_presents_before_draw\",\"render_state_schema\":1,\"render_state_query_failures\":%llu,\"render_state_partial_samples\":%llu,\"captures\":[",
                  s.capture_target_count,static_cast<unsigned long long>(s.capture_failures),
                  static_cast<unsigned long long>(s.render_state_query_failures),
                  static_cast<unsigned long long>(s.render_state_partial_samples));json+=buffer;
    for(size_t index=0;index<s.capture_sample_count;++index) {
        const auto& row=s.captures[index];if(index)json+=',';
        std::snprintf(buffer,sizeof(buffer),"{\"frame\":%llu,\"successful_draw\":%llu,\"draw_kind\":%u,\"target\":%u,\"requested_vertex_id\":%u,\"requested_pixel_id\":%u,\"vertex_aliases\":%u,\"pixel_aliases\":%u,\"constants\":[",
                      static_cast<unsigned long long>(row.frame),static_cast<unsigned long long>(row.successful_draw),
                      current.capture_kinds[index],row.target,row.vertex_id,row.pixel_id,row.vertex_aliases,row.pixel_aliases);json+=buffer;
        for(size_t constant=0;constant<row.constant_count;++constant) {
            const auto& value=row.constants[constant];if(constant)json+=',';
            std::snprintf(buffer,sizeof(buffer),"{\"stage\":%u,\"index\":%u,\"component_mask\":%u,\"valid\":%u,\"value\":[%.9g,%.9g,%.9g,%.9g]}",
                          value.stage,value.index,value.component_mask,value.valid,
                          static_cast<double>(value.value[0]),static_cast<double>(value.value[1]),
                          static_cast<double>(value.value[2]),static_cast<double>(value.value[3]));json+=buffer;
        }
        json+="],\"samplers\":[";
        for(size_t sampler=0;sampler<row.sampler_count;++sampler) {
            const auto& value=row.samplers[sampler];if(sampler)json+=',';
            std::snprintf(buffer,sizeof(buffer),"{\"index\":%u,\"valid\":%u,\"type\":%u,\"format\":%u,\"width\":%u,\"height\":%u,\"depth\":%u,\"levels\":%u}",
                          value.index,value.valid,value.type,value.format,value.width,value.height,value.depth,value.levels);json+=buffer;
        }
        std::snprintf(buffer,sizeof(buffer),"],\"context_valid\":%u,\"target_width\":%u,\"target_height\":%u,\"target_format\":%u,\"viewport_width\":%u,\"viewport_height\":%u,\"alpha_blend\":%u,\"alpha_test\":%u,\"srgb_write\":%u,\"viewport_x\":%u,\"viewport_y\":%u,\"render_state_available\":%u,\"render_states\":{",
                      row.context_valid,row.target_width,row.target_height,row.target_format,row.viewport_width,row.viewport_height,
                      row.alpha_blend,row.alpha_test,row.srgb_write,row.viewport_x,row.viewport_y,row.render_states.available);json+=buffer;
        for(size_t field=0;field<CapturedRenderStateCount;++field) {
            if(field)json+=',';json+='\"';json+=CapturedRenderStateNames[field];json+="\":";
            if(row.render_states.available&(1u<<field))json+=std::to_string(row.render_states.values[field]);
            else json+="null";
        }
        json+="}}";
    }
    json+="]}\n";
    const std::wstring temporary=current.report_path+L".tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,
                            FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE){++current.stats.report_failures;return;}
    BY_HANDLE_FILE_INFORMATION info{};DWORD written=0;
    bool valid=GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        && json.size()<=MAXDWORD && WriteFile(file,json.data(),static_cast<DWORD>(json.size()),&written,nullptr)
        && written==json.size();
    CloseHandle(file);
    if(valid)valid=MoveFileExW(temporary.c_str(),current.report_path.c_str(),MOVEFILE_REPLACE_EXISTING|MOVEFILE_WRITE_THROUGH)!=0;
    if(!valid)++current.stats.report_failures;
}

template<class Shader> Cached* Query(Shader* shader,unsigned stage) {
    // Borrow the caller's live query ref. Cache only known positive identities;
    // full capacity changes reuse cost, never known-program counting/capture.
    if(!shader)return nullptr;
    for(size_t index=0;index<current.cache_count;++index) {
        auto& item=current.cache[index];
        if(item.object==static_cast<IUnknown*>(shader) && item.stage==stage) {
            ++current.stats.cache_hits;return &item;
        }
    }
    UINT bytes=0;
    if(FAILED(shader->GetFunction(nullptr,&bytes)) || !bytes || bytes%4) {
        ++current.stats.query_failures;return nullptr;
    }
    if(bytes>MaxCodeBytes){++current.stats.oversize_programs;return nullptr;}
    const UINT expected=bytes;
    if(FAILED(shader->GetFunction(current.scratch.data(),&bytes)) || bytes!=expected) {
        ++current.stats.query_failures;return nullptr;
    }
    ++current.stats.shader_reads;
    Cached item{};item.stage=stage;item.bytes=bytes;
    if(!Hash(current.scratch.data(),bytes,item.sha256)) {
        ++current.stats.query_failures;return nullptr;
    }
    for(size_t index=0;index<current.stats.catalog_count;++index) {
        const auto& row=current.catalog[index];
        if(row.stage==stage && row.bytes==bytes && row.sha256==item.sha256) {
            if(!item.representative)item.representative=row.id;
            ++item.aliases;
        }
    }
    if(!item.aliases || current.cache_count==MaxCache){
        if(item.aliases)++current.cache_capacity_bypasses;
        current.uncached[stage-1]=item;return &current.uncached[stage-1];
    }
    shader->AddRef();item.object=shader;
    current.cache[current.cache_count]=item;
    Cached* result=&current.cache[current.cache_count++];
    current.stats.cached_refs=static_cast<std::uint32_t>(current.cache_count);
    current.stats.peak_cached_refs=std::max(current.stats.peak_cached_refs,current.stats.cached_refs);
    return result;
}

void Count(Cached* item) {
    if(!item)return;
    if(item->aliases) {
        if(item->stage==1)++current.stats.known_vertex_draws;else ++current.stats.known_pixel_draws;
        for(size_t index=0;index<current.stats.catalog_count;++index) {
            const auto& row=current.catalog[index];
            if(row.stage==item->stage && row.bytes==item->bytes && row.sha256==item->sha256)
                ++current.stats.records[index].draws;
        }
        return;
    }
    for(size_t index=0;index<current.stats.unknown_count;++index) {
        auto& row=current.stats.unknown[index];
        if(row.stage==item->stage && row.bytes==item->bytes && row.sha256==item->sha256){++row.draws;return;}
    }
    if(current.stats.unknown_count>=MaxUnknown){++current.stats.unknown_overflows;return;}
    auto& row=current.stats.unknown[current.stats.unknown_count++];
    row.stage=item->stage;row.bytes=item->bytes;row.sha256=item->sha256;row.draws=1;
}

void Pair(Cached* vertex,Cached* pixel) {
    if(!vertex || !pixel || !vertex->aliases || !pixel->aliases)return;
    ++current.stats.known_pair_draws;
    for(size_t index=0;index<current.stats.pair_count;++index) {
        auto& row=current.stats.pairs[index];
        if(row.vertex_id==vertex->representative && row.pixel_id==pixel->representative){++row.draws;return;}
    }
    if(current.stats.pair_count>=MaxPairs){++current.stats.pair_overflows;return;}
    auto& row=current.stats.pairs[current.stats.pair_count++];
    row={vertex->representative,pixel->representative,vertex->aliases,pixel->aliases,1};
}

bool Finite(float value) {
    std::uint32_t bits=0;std::memcpy(&bits,&value,sizeof(bits));return (bits&0x7f800000u)!=0x7f800000u;
}

void CaptureFrame(IDirect3DDevice9* device,Cached* vertex,Cached* pixel) {
    if(!current.frame_width || !vertex || !pixel || !vertex->aliases || !pixel->aliases)return;
    const std::uint64_t frame=current.stats.frames+1;
    if(current.frame_attempt==frame || vertex->bytes!=current.frame_vertex_bytes || pixel->bytes!=current.frame_pixel_bytes
        || vertex->sha256!=current.frame_vertex || pixel->sha256!=current.frame_pixel)return;
    FrameSnapshot sample{};sample.frame=frame;sample.successful_draw=current.stats.successful_draws;
    sample.vertex_id=vertex->representative;sample.pixel_id=pixel->representative;
    sample.vertex_aliases=vertex->aliases;sample.pixel_aliases=pixel->aliases;
    sample.catalog_sha256=current.catalog_digest;sample.vertex_sha256=vertex->sha256;sample.pixel_sha256=pixel->sha256;
    IDirect3DSurface9* target=nullptr;D3DSURFACE_DESC desc{};D3DVIEWPORT9 viewport{};
    bool valid=SUCCEEDED(device->GetRenderTarget(0,&target)) && target && SUCCEEDED(target->GetDesc(&desc));
    if(target)target->Release();
    if(!valid || desc.Width!=current.frame_width || desc.Height!=current.frame_height || desc.Format!=D3DFMT_A8R8G8B8)return;
    if(FAILED(device->GetViewport(&viewport)) || viewport.X || viewport.Y
        || viewport.Width!=desc.Width || viewport.Height!=desc.Height)return;
    DWORD blend=0,alpha=0,srgb=0;
    if(FAILED(device->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend)) || blend
        || FAILED(device->GetRenderState(D3DRS_ALPHATESTENABLE,&alpha)) || alpha
        || FAILED(device->GetRenderState(D3DRS_SRGBWRITEENABLE,&srgb)) || srgb)return;
    // At most one raw-uniform attempt for an eligible original full-scene draw.
    // A failed/nonfinite attempt makes this frame unavailable, never stale.
    current.frame_attempt=frame;
    sample.target_width=desc.Width;sample.target_height=desc.Height;sample.target_format=desc.Format;
    sample.viewport_width=viewport.Width;sample.viewport_height=viewport.Height;
    sample.alpha_blend=blend;sample.alpha_test=alpha;sample.srgb_write=srgb;sample.context_valid=7;
    std::array<float,4> eye{},sun{},specular{};
    if(FAILED(device->GetVertexShaderConstantF(12,sample.vs_constants_12_15.data(),4))
        || FAILED(device->GetVertexShaderConstantF(16,eye.data(),1))
        || FAILED(device->GetPixelShaderConstantF(5,sun.data(),1))
        || FAILED(device->GetPixelShaderConstantF(7,specular.data(),1)))return;
    for(float value:sample.vs_constants_12_15)if(!Finite(value))return;
    for(size_t index=0;index<3;++index) {
        if(!Finite(eye[index]) || !Finite(sun[index]) || !Finite(specular[index]))return;
        sample.vs16_eye[index]=eye[index];sample.ps5_sun_direction[index]=sun[index];sample.ps7_sun_specular[index]=specular[index];
    }
    sample.available=1;current.latest=sample;
}

void Capture(IDirect3DDevice9* device,Cached* vertex,Cached* pixel,unsigned kind) {
    if(!vertex || !pixel || !current.capture.target_count || current.stats.frames<current.capture.first_frame)return;
    for(size_t index=0;index<current.capture.target_count;++index) {
        const auto& target=current.capture.targets[index];
        if(current.captured[index]>=target.sample_limit || current.stats.capture_sample_count>=MaxCaptureSamples)continue;
        if(current.captured[index] && current.stats.frames-current.last_capture[index]<current.capture.frame_stride)continue;
        const auto& expected_vertex=current.catalog[target.vertex_id-1];
        const auto& expected_pixel=current.catalog[target.pixel_id-1];
        if(vertex->bytes!=expected_vertex.bytes || vertex->sha256!=expected_vertex.sha256
            || pixel->bytes!=expected_pixel.bytes || pixel->sha256!=expected_pixel.sha256)continue;
        CaptureSample sample{};sample.frame=current.stats.frames;sample.successful_draw=current.stats.successful_draws;
        sample.target=static_cast<std::uint32_t>(index);sample.vertex_id=target.vertex_id;sample.pixel_id=target.pixel_id;
        sample.vertex_aliases=vertex->aliases;sample.pixel_aliases=pixel->aliases;
        sample.constant_count=target.constant_count;
        IDirect3DSurface9* render_target=nullptr;D3DSURFACE_DESC render_desc{};
        if(SUCCEEDED(device->GetRenderTarget(0,&render_target)) && render_target && SUCCEEDED(render_target->GetDesc(&render_desc))) {
            sample.target_width=render_desc.Width;sample.target_height=render_desc.Height;sample.target_format=render_desc.Format;
            sample.context_valid|=1;
        }
        if(render_target)render_target->Release();
        D3DVIEWPORT9 viewport{};
        if(SUCCEEDED(device->GetViewport(&viewport))){sample.viewport_x=viewport.X;sample.viewport_y=viewport.Y;
            sample.viewport_width=viewport.Width;sample.viewport_height=viewport.Height;sample.context_valid|=2;}
        // Only reached after exact target identity, stride and sample-budget
        // checks. No additional render-state queries on ordinary hot draws.
        for(size_t field=0;field<CapturedRenderStateCount;++field) {
            DWORD value=0;
            if(SUCCEEDED(device->GetRenderState(CapturedRenderStateTypes[field],&value))) {
                sample.render_states.values[field]=value;sample.render_states.available|=1u<<field;
            } else ++current.stats.render_state_query_failures;
        }
        if(sample.render_states.available!=AllCapturedRenderStates) {
            ++current.stats.render_state_partial_samples;++current.stats.capture_failures;
        }
        const auto get=[&](CapturedRenderState field){return sample.render_states.values[static_cast<size_t>(field)];};
        constexpr std::uint32_t legacy=RenderStateBit(CapturedRenderState::AlphaBlendEnable)
            |RenderStateBit(CapturedRenderState::AlphaTestEnable)|RenderStateBit(CapturedRenderState::SrgbWriteEnable);
        if((sample.render_states.available&legacy)==legacy) {
            sample.alpha_blend=get(CapturedRenderState::AlphaBlendEnable);sample.alpha_test=get(CapturedRenderState::AlphaTestEnable);
            sample.srgb_write=get(CapturedRenderState::SrgbWriteEnable);sample.context_valid|=4;
        }
        if(sample.context_valid!=7)++current.stats.capture_failures;
        for(size_t constant=0;constant<target.constant_count;++constant) {
            const auto& request=target.constants[constant];auto& value=sample.constants[constant];
            value.stage=request.stage;value.index=request.index;value.component_mask=request.component_mask;
            std::array<float,4> values{};
            const HRESULT result=request.stage==1?device->GetVertexShaderConstantF(request.index,values.data(),1)
                :device->GetPixelShaderConstantF(request.index,values.data(),1);
            bool valid=SUCCEEDED(result);
            for(unsigned component=0;component<4;++component)
                if((request.component_mask&(1u<<component)) && !Finite(values[component]))valid=false;
            value.valid=valid?1u:0u;
            if(valid) {
                for(unsigned component=0;component<4;++component)
                    if(request.component_mask&(1u<<component))value.value[component]=values[component];
            } else ++current.stats.capture_failures;
        }
        for(unsigned sampler=0;sampler<16;++sampler) {
            if(!(target.sampler_mask&(1u<<sampler)))continue;
            auto& value=sample.samplers[sample.sampler_count++];value.index=sampler;
            IDirect3DBaseTexture9* texture=nullptr;
            if(FAILED(device->GetTexture(sampler,&texture)) || !texture) {
                ++current.stats.capture_failures;if(texture)texture->Release();continue;
            }
            value.type=static_cast<std::uint32_t>(texture->GetType());value.levels=texture->GetLevelCount();
            D3DSURFACE_DESC desc{};D3DVOLUME_DESC volume{};HRESULT result=D3DERR_INVALIDCALL;
            if(value.type==D3DRTYPE_TEXTURE)result=static_cast<IDirect3DTexture9*>(texture)->GetLevelDesc(0,&desc);
            else if(value.type==D3DRTYPE_CUBETEXTURE)result=static_cast<IDirect3DCubeTexture9*>(texture)->GetLevelDesc(0,&desc);
            else if(value.type==D3DRTYPE_VOLUMETEXTURE)result=static_cast<IDirect3DVolumeTexture9*>(texture)->GetLevelDesc(0,&volume);
            if(SUCCEEDED(result) && value.levels) {
                value.valid=1;
                if(value.type==D3DRTYPE_VOLUMETEXTURE){value.format=volume.Format;value.width=volume.Width;value.height=volume.Height;value.depth=volume.Depth;}
                else {value.format=desc.Format;value.width=desc.Width;value.height=desc.Height;value.depth=1;}
            } else ++current.stats.capture_failures;
            texture->Release();
        }
        current.capture_kinds[current.stats.capture_sample_count]=kind;
        current.stats.captures[current.stats.capture_sample_count++]=sample;
        ++current.captured[index];current.last_capture[index]=current.stats.frames;
    }
}
}

bool Initialize(const wchar_t* catalog_path,const unsigned char expected[32],const wchar_t* report_path) noexcept {
    LastErrorState last_error;
    FloatingState floating;
    try {
        Lock lock;
        if(current.stats.enabled || !catalog_path || !expected || !report_path
            || wcsnlen_s(catalog_path,512)>=512 || wcsnlen_s(report_path,512)>=508)return false;
        std::wstring pending_report(report_path);
        HANDLE file=CreateFileW(catalog_path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
                                FILE_ATTRIBUTE_NORMAL|FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE)return false;
        BY_HANDLE_FILE_INFORMATION info{};LARGE_INTEGER size{};
        std::array<unsigned char,MaxCatalogBytes> bytes{};DWORD count=0;
        bool valid=GetFileInformationByHandle(file,&info) && !(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
            && GetFileSizeEx(file,&size) && size.QuadPart>=16 && size.QuadPart<=static_cast<LONGLONG>(bytes.size())
            && ReadFile(file,bytes.data(),static_cast<DWORD>(size.QuadPart),&count,nullptr)
            && static_cast<LONGLONG>(count)==size.QuadPart;
        if(valid && !current.algorithm)valid=BCryptOpenAlgorithmProvider(&current.algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
        std::array<unsigned char,32> actual{};
        if(valid)valid=Hash(bytes.data(),count,actual) && !std::memcmp(actual.data(),expected,32);
        std::uint32_t version=0,rows=0;
        if(valid){std::memcpy(&version,bytes.data()+8,4);std::memcpy(&rows,bytes.data()+12,4);}
        if(valid)valid=!std::memcmp(bytes.data(),"S7MSCAT1",8) && version==1 && rows>0 && rows<=MaxCatalog
            && count==16+rows*sizeof(CatalogEntry);
        std::array<CatalogEntry,MaxCatalog> catalog{};
        if(valid)std::memcpy(catalog.data(),bytes.data()+16,rows*sizeof(CatalogEntry));
        for(size_t index=0;valid && index<rows;++index) {
            const auto& row=catalog[index];
            valid=row.id==index+1 && row.family>=1 && row.family<=3 && row.stage>=1 && row.stage<=2
                && row.bytes>=8 && row.bytes<=MaxCodeBytes && !(row.bytes%4);
        }
        if(!valid){CloseHandle(file);return false;}
        current.trace=TraceState{};
        DigestLiteral("0fafd8564b812ef5d8609329052df4be741f015c3c3b51b0946420e0fe6880f3",current.ui_vertex);
        DigestLiteral("69b5a31786c16faf4c06b1592c6925f745230ac27d7729db78d330fdf084aae6",current.ui_pixel);
        current.ui_vertex_bytes=484;current.ui_pixel_bytes=1108;
        current.stats=Stats{};current.stats.enabled=1;current.stats.catalog_count=rows;
        current.cache_capacity_bypasses=0;current.uncached.fill(Cached{});
        current.capture=CaptureConfig{};current.captured.fill(0);current.last_capture.fill(0);
        current.frame_width=0;current.frame_height=0;current.frame_attempt=0;current.latest=FrameSnapshot{};
        DigestLiteral("f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0",current.frame_vertex);
        DigestLiteral("dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b",current.frame_pixel);
        current.frame_vertex_bytes=2592;current.frame_pixel_bytes=3740;
        current.catalog=catalog;current.catalog_digest=actual;current.catalog_file=file;
        current.report_path=std::move(pending_report);current.thread=0;current.device=nullptr;
        for(size_t index=0;index<rows;++index) {
            const auto& row=catalog[index];current.stats.records[index]={row.id,row.family,row.stage,row.bytes,0};
        }
        try {Report();}catch(...){++current.stats.exceptions;}
        return true;
    } catch(...) {return false;}
}

bool ConfigureCapture(const CaptureConfig* config) noexcept {
    LastErrorState last_error;
    FloatingState floating;
    try {
        Lock lock;
        if(!current.stats.enabled || !config || config->size!=sizeof(CaptureConfig) || config->version!=1
            || config->target_count>MaxCaptureTargets || !config->frame_stride || config->frame_stride>36000)return false;
        for(size_t index=0;index<config->target_count;++index) {
            const auto& target=config->targets[index];
            if(!target.vertex_id || target.vertex_id>current.stats.catalog_count || !target.pixel_id
                || target.pixel_id>current.stats.catalog_count || !target.sample_limit || target.sample_limit>8
                || target.constant_count>MaxCaptureConstants || target.sampler_mask>0xffffu)return false;
            const auto& vertex=current.catalog[target.vertex_id-1];const auto& pixel=current.catalog[target.pixel_id-1];
            if(vertex.stage!=1 || pixel.stage!=2)return false;
            for(size_t previous=0;previous<index;++previous) {
                const auto& other=config->targets[previous];
                if(current.catalog[other.vertex_id-1].sha256==vertex.sha256
                    && current.catalog[other.pixel_id-1].sha256==pixel.sha256)return false;
            }
            for(size_t constant=0;constant<target.constant_count;++constant) {
                const auto& request=target.constants[constant];
                if((request.stage!=1 && request.stage!=2) || request.index>(request.stage==1?255u:223u)
                    || !request.component_mask || request.component_mask>15)return false;
                for(size_t previous=0;previous<constant;++previous)
                    if(target.constants[previous].stage==request.stage && target.constants[previous].index==request.index)return false;
            }
        }
        current.capture=*config;current.captured.fill(0);current.last_capture.fill(0);
        current.stats.capture_target_count=config->target_count;current.stats.capture_sample_count=0;current.stats.capture_failures=0;
        current.stats.render_state_query_failures=0;current.stats.render_state_partial_samples=0;
        current.stats.captures.fill(CaptureSample{});current.capture_kinds.fill(0);return true;
    }catch(...){Exception();return false;}
}
bool ConfigureDrawTrace(const DrawTraceConfig* config) noexcept {
    LastErrorState last_error;FloatingState floating;
    try {
        Lock lock;
        if(!current.stats.enabled||!config||config->size!=sizeof(DrawTraceConfig)||config->version!=2
            ||!config->sequence||config->sequence<=current.trace.last_sequence||current.trace.active||current.trace.armed
            ||current.report_path.empty()||current.report_path.size()+64>=512)return false;
        const auto& c=*config;
        if(c.draw_count) {
            if(!c.first_draw||c.draw_count>8||c.first_draw>MaxTraceDraws-c.draw_count+1
                ||!c.target_width||c.target_width>3840||!c.target_height||c.target_height>2160
                ||!c.width||c.width>256||!c.height||c.height>256||c.width>c.target_width||c.height>c.target_height
                ||c.x>c.target_width-c.width||c.y>c.target_height-c.height)return false;
        }else if(c.first_draw||c.target_width||c.target_height||c.x||c.y||c.width||c.height)return false;
        const auto absent=[](const std::wstring& path) {
            SetLastError(ERROR_SUCCESS);
            return GetFileAttributesW(path.c_str())==INVALID_FILE_ATTRIBUTES&&GetLastError()==ERROR_FILE_NOT_FOUND;
        };
        if(!absent(TracePath(c.sequence)))return false;
        for(DWORD i=0;i<c.draw_count;++i)if(!absent(TracePath(c.sequence,c.first_draw+i)))return false;
        current.trace.rows.reserve(MaxTraceDraws);
        current.trace.config=c;current.trace.last_sequence=c.sequence;
        current.trace.draws=current.trace.overflows=current.trace.failures=0;
        current.trace.rows.clear();current.trace.armed=true;return true;
    }catch(...){Exception();return false;}
}
bool ConfigureFrameSnapshot(DWORD width,DWORD height) noexcept {
    LastErrorState last_error;
    try {Lock lock;if(!current.stats.enabled || width<320 || height<200 || width>8192 || height>8192
        || static_cast<std::uint64_t>(width)*height>32u*1024u*1024u)return false;
        current.frame_width=width;current.frame_height=height;current.frame_attempt=0;current.latest=FrameSnapshot{};return true;
    }catch(...){Exception();return false;}
}
bool CopyLatestFrameSnapshot(std::uint64_t expected,FrameSnapshot* output) noexcept {
    LastErrorState last_error;
    try {Lock lock;if(!output || output->size!=sizeof(FrameSnapshot) || output->version!=1)return false;
        *output=FrameSnapshot{};
        if(!current.stats.enabled || expected!=current.stats.frames || !current.latest.available || current.latest.frame!=expected)return false;
        *output=current.latest;return true;
    }catch(...){Exception();return false;}
}
void InvalidateFrameSnapshot() noexcept {LastErrorState last_error;try{Lock lock;current.latest=FrameSnapshot{};}catch(...){Exception();}}

void ObserveDraw(IDirect3DDevice9* device,unsigned kind,HRESULT result) noexcept {
    LastErrorState last_error;
    FloatingState floating;
    try {
        Lock lock;if(!Context(device))return;
        if(kind!=Draw && kind!=Indexed && kind!=DrawUP && kind!=IndexedUP){++current.stats.context_rejections;return;}
        if(FAILED(result)){++current.stats.failed_draws;return;}
        ++current.stats.successful_draws;
        Ref<IDirect3DVertexShader9> vertex;Ref<IDirect3DPixelShader9> pixel;
        current.stats.shader_queries+=2;
        if(FAILED(device->GetVertexShader(&vertex.value))){++current.stats.query_failures;vertex.reset();}
        if(FAILED(device->GetPixelShader(&pixel.value))){++current.stats.query_failures;pixel.reset();}
        Cached* vs=Query(vertex.value,1);Cached* ps=Query(pixel.value,2);Count(vs);Count(ps);Pair(vs,ps);Capture(device,vs,ps,kind);CaptureFrame(device,vs,ps);
        TraceDraw(device,vs,ps,kind);
    } catch(...) {Exception();}
}

void EndFrame(IDirect3DDevice9* device) noexcept {
    LastErrorState last_error;
    FloatingState floating;
    try {Lock lock;if(!Context(device))return;
        const bool start=current.trace.armed;
        if(current.trace.active)FinishTrace(false);
        ClearCache();++current.stats.frames;
        if(start){current.trace.armed=false;current.trace.active=true;current.trace.frame=current.stats.frames;}
        if(current.stats.frames%120==0)Report();}
    catch(...) {Exception();}
}
void Reset(IDirect3DDevice9* device) noexcept {
    LastErrorState last_error;
    FloatingState floating;
    try {
        Lock lock;if(!current.stats.enabled || !device)return;
        // Release/Reset may come from another thread. Serialize cleanup of the
        // same owned device rather than retaining shader->device references;
        // observation/capture itself remains restricted to its render thread.
        if(current.device && current.device!=device){++current.stats.context_rejections;return;}
        ClearCache();++current.stats.resets;current.device=nullptr;
    }
    catch(...) {Exception();}
}
bool ReadStats(Stats* output) noexcept {
    LastErrorState last_error;
    try {Lock lock;if(!output || output->size!=sizeof(Stats) || output->version!=1)return false;*output=current.stats;return true;}catch(...){return false;}
}
void Flush() noexcept {LastErrorState last_error;FloatingState floating;try{Lock lock;if(current.stats.enabled)Report();}catch(...){Exception();}}
void Shutdown() noexcept {
    LastErrorState last_error;
    FloatingState floating;
    try {
        Lock lock;FinishTrace(true);ClearCache();Report();current.stats.enabled=0;
        if(current.catalog_file!=INVALID_HANDLE_VALUE){CloseHandle(current.catalog_file);current.catalog_file=INVALID_HANDLE_VALUE;}
        if(current.algorithm){BCryptCloseAlgorithmProvider(current.algorithm,0);current.algorithm=nullptr;}
        current.device=nullptr;current.thread=0;current.report_path.clear();
        current.latest=FrameSnapshot{};current.frame_width=0;current.frame_height=0;
    } catch(...) {Exception();}
}
#ifdef S7_MATERIAL_OBSERVER_FIXTURE
bool FixtureFrameIdentity(const unsigned char vertex[32],UINT vertex_bytes,const unsigned char pixel[32],UINT pixel_bytes) noexcept {
    LastErrorState last_error;try {Lock lock;if(!current.stats.enabled || !vertex || !pixel)return false;
        std::memcpy(current.frame_vertex.data(),vertex,32);std::memcpy(current.frame_pixel.data(),pixel,32);
        current.frame_vertex_bytes=vertex_bytes;current.frame_pixel_bytes=pixel_bytes;current.latest=FrameSnapshot{};current.frame_attempt=0;return true;
    }catch(...){return false;}
}
#endif
#if defined(S7_MATERIAL_OBSERVER_FIXTURE) || defined(S7_SUPERSAMPLING_FIXTURE)
bool FixtureUiIdentity(const unsigned char vertex[32],UINT vertex_bytes,const unsigned char pixel[32],UINT pixel_bytes) noexcept {
    LastErrorState saved;try {Lock lock;
        if(!current.stats.enabled||!vertex||!pixel||!vertex_bytes||!pixel_bytes||vertex_bytes>MaxCodeBytes||pixel_bytes>MaxCodeBytes)return false;
        std::memcpy(current.ui_vertex.data(),vertex,32);std::memcpy(current.ui_pixel.data(),pixel,32);
        current.ui_vertex_bytes=vertex_bytes;current.ui_pixel_bytes=pixel_bytes;return true;
    }catch(...){return false;}
}
#endif
}
