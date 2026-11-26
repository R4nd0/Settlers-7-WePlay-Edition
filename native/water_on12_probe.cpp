// Exact River input capture; explicit flag 1 enables two finite POW caps only
// on the proven On12 device. Original draw once, original VS restored afterward.
#include "water_on12_identity.h"
#include "water_on12_pow_cap.h"
#include "water_on12_shader_cache.h"
#include "water_on12_constants.h"
#include <d3d9.h>
#include <d3d12.h>
#include <d3d9on12.h>
#include <array>
#include <atomic>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <sstream>
#include <vector>

namespace {
using namespace WaterOn12Identity;
constexpr char GetterName[]="?GetD3DDevice@VVideo@@SAPAUIDirect3DDevice9@@XZ";
constexpr char Vertex[]="f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0";
constexpr char Pixel[]="dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b";
struct Config {DWORD size,version,flags;wchar_t report[512];};
static_assert(sizeof(Config)==1036,"Water observer ABI changed");
using Getter=IDirect3DDevice9* (__cdecl*)();
using DrawIndexed=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);
using Present=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using Reset=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
template<class T> struct Ref {T* value=nullptr;~Ref(){if(value)value->Release();}};
struct Slot {void** address;void* original;void* replacement;};
Getter original_getter=nullptr;DrawIndexed original_draw=nullptr;Present original_present=nullptr;
Reset original_reset=nullptr;
HMODULE wrapper=nullptr,apphelp=nullptr;IDirect3DDevice9* admitted=nullptr;DWORD owner=0;
thread_local bool internal=false;
thread_local std::uint64_t getter_calls=0,getter_guard_calls=0,getter_samples=0,getter_ticks=0;
struct Scope {bool previous=internal;Scope(){internal=true;}~Scope(){internal=previous;}};
Config config{};std::wstring directory;std::atomic_bool snapshot{false};DWORD failure=0;
bool failure_reported=false;
struct DeviceInfo {
    HRESULT creation_hr=E_FAIL,presentation_hr=E_FAIL,ex_hr=E_NOINTERFACE,latency_hr=E_FAIL;
    D3DDEVICE_CREATION_PARAMETERS creation{};D3DPRESENT_PARAMETERS presentation{};
    BOOL software=FALSE;UINT latency=0;
} device_info;
void ObserveDevice(IDirect3DDevice9* device) {
    device_info.creation_hr=device->GetCreationParameters(&device_info.creation);
    device_info.software=device->GetSoftwareVertexProcessing();
    Ref<IDirect3DSwapChain9> chain;
    device_info.presentation_hr=device->GetSwapChain(0,&chain.value);
    if(SUCCEEDED(device_info.presentation_hr)&&chain.value)
        device_info.presentation_hr=chain.value->GetPresentParameters(&device_info.presentation);
    Ref<IDirect3DDevice9Ex> extended;
    device_info.ex_hr=device->QueryInterface(__uuidof(IDirect3DDevice9Ex),reinterpret_cast<void**>(&extended.value));
    if(SUCCEEDED(device_info.ex_hr)&&extended.value)device_info.latency_hr=extended.value->GetMaximumFrameLatency(&device_info.latency);
}
std::uint64_t draws=0,matches=0,failed_draws=0,presents=0;HRESULT on12_hr=E_NOINTERFACE;
// One device-owned program for this pinned process lifetime. Do not Release a
// COM object from a CRT global destructor after renderer DLL teardown.
IDirect3DVertexShader9* corrected=nullptr;
WaterOn12ShaderCache::Cache<IDirect3DPixelShader9,512> pixel_cache;
WaterOn12ShaderCache::Cache<IDirect3DVertexShader9,32> vertex_cache;
std::uint64_t cache_resets=0;
std::uint64_t world_frames=0,last_present_draws=0;
std::atomic_bool benchmark_legacy{false},benchmark_constants{false},benchmark_shadow{false},benchmark_shader_state{false};DWORD benchmark_sequence=0,benchmark_errors=0;
CRITICAL_SECTION constants_lock{};bool constants_lock_ready=false,constants_recording=false,constants_armed=false,constants_foreign=false;
std::uint64_t constants_checked=0,constants_queries=0,constants_skipped=0,constants_forwarded=0,constants_recorded=0;
WaterOn12Constants::Hint constants_hint;
using BlockApplyFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DStateBlock9*);
BlockApplyFn original_block_apply=nullptr;
std::uint64_t constants_invalidations=0;
bool pixel_state_known=false,pixel_state_target=false;
std::uint64_t pixel_gets_avoided=0;
bool PixelTarget(IDirect3DPixelShader9* shader,bool* verified=nullptr);
struct ConstantsLock {
    ConstantsLock(){const DWORD error=GetLastError();EnterCriticalSection(&constants_lock);SetLastError(error);}
    ~ConstantsLock(){const DWORD error=GetLastError();LeaveCriticalSection(&constants_lock);SetLastError(error);}
};
std::uint64_t camera_hash=0,camera_changes=0,camera_errors=0,camera_frame=~std::uint64_t(0);
struct BenchmarkControl {DWORD size,version,sequence,legacy;};
bool BenchmarkShape(const BenchmarkControl& value){return value.size==16&&value.version==1&&value.sequence>benchmark_sequence&&value.legacy<=4;}
void BenchmarkCommand() {
    if(config.flags!=3)return;
    HANDLE file=CreateFileW((directory+L"\\water-benchmark.private.command").c_str(),GENERIC_READ,FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    BenchmarkControl value{};DWORD bytes=0;LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION info{};
    const bool valid=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)&&GetFileSizeEx(file,&size)&&size.QuadPart==16
        &&ReadFile(file,&value,sizeof(value),&bytes,nullptr)&&bytes==sizeof(value);CloseHandle(file);
    if(!valid){++benchmark_errors;return;}
    if(value.sequence==benchmark_sequence)return;
    if(!BenchmarkShape(value)){++benchmark_errors;return;}
    // The original IDirect3DDevice9 path uses Reset (slot 16). A 9Ex client
    // could use ResetEx instead, so do not admit a shadow on that interface.
    if(value.legacy>=3&&device_info.ex_hr!=E_NOINTERFACE){++benchmark_errors;return;}
    ConstantsLock lock;constants_hint.Clear();pixel_state_known=false;
    benchmark_legacy=value.legacy==1;benchmark_constants=value.legacy==2||value.legacy==3;benchmark_shadow=value.legacy==3;
    benchmark_shader_state=value.legacy==4;benchmark_sequence=value.sequence;
}
void ObserveCamera(IDirect3DDevice9* device) {
    if(!(config.flags&1)||camera_frame==world_frames)return;
    camera_frame=world_frames;std::array<float,24> value{};
    if(FAILED(device->GetVertexShaderConstantF(12,value.data(),6))){++camera_errors;return;}
    const auto* bytes=reinterpret_cast<const unsigned char*>(value.data());std::uint64_t hash=14695981039346656037ull;
    for(size_t i=0;i<sizeof(value);++i){hash^=bytes[i];hash*=1099511628211ull;}
    if(camera_hash&&camera_hash!=hash)++camera_changes;camera_hash=hash;
}
std::uint64_t caps_applied=0,caps_restored=0;
std::uint64_t pixel_gets=0,pixel_reads=0,pixel_hashes=0,vertex_reads=0,vertex_hashes=0,vertex_allocations=0;
std::uint64_t profile_samples=0,profile_total=0,profile_lookup=0,profile_original=0,profile_bind=0;
LONGLONG clock_frequency=0;
std::uint64_t Clock(){LARGE_INTEGER value{};QueryPerformanceCounter(&value);return static_cast<std::uint64_t>(value.QuadPart);}
template<class T> HRESULT ReadProgram(T* shader,void* bytes,UINT* size,std::uint64_t& count){++count;return shader->GetFunction(bytes,size);}
struct ApiStat {std::uint64_t calls=0,samples=0,ticks=0;};
std::array<ApiStat,14> api_stats{};
constexpr const char* ApiNames[]{"SetRenderState","SetTexture","SetSamplerState","SetVSConstantF","SetPSConstantF","SetVS","SetPS","SetStream","SetIndices","EndScene","GetRenderTargetData","StretchRect","Clear","BeginScene"};
using RenderStateFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DRENDERSTATETYPE,DWORD);
using TextureFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*);
using SamplerFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,D3DSAMPLERSTATETYPE,DWORD);
using ConstantFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,const float*,UINT);
using VertexFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DVertexShader9*);
using PixelFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DPixelShader9*);
using StreamFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,UINT,IDirect3DVertexBuffer9*,UINT,UINT);
using IndicesFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DIndexBuffer9*);
using SceneFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using SoftwareFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,BOOL);
using EndBlockFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DStateBlock9**);
using ReadbackFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*,IDirect3DSurface9*);
using StretchFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE);
using ClearFn=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD);
RenderStateFn api_render=nullptr;TextureFn api_texture=nullptr;SamplerFn api_sampler=nullptr;
ConstantFn api_vsconst=nullptr,api_psconst=nullptr;VertexFn api_vs=nullptr;PixelFn api_ps=nullptr;
StreamFn api_stream=nullptr;IndicesFn api_indices=nullptr;SceneFn api_end=nullptr,api_begin=nullptr;
ReadbackFn api_readback=nullptr;StretchFn api_stretch=nullptr;ClearFn api_clear=nullptr;
SceneFn api_begin_block=nullptr;EndBlockFn api_end_block=nullptr;
SoftwareFn api_software=nullptr;
template<class Call> HRESULT MeasureApi(IDirect3DDevice9* device,unsigned index,Call call) {
    if(config.flags!=3||internal||device!=admitted||GetCurrentThreadId()!=owner)return call();
    auto& value=api_stats[index];++value.calls;const bool sampled=index>=9||value.calls%128==0;
    if(!sampled)return call();
    const DWORD error=GetLastError();const auto begin=Clock();SetLastError(error);
    const HRESULT result=call();const DWORD returned=GetLastError();const auto end=Clock();SetLastError(returned);
    ++value.samples;value.ticks+=end-begin;return result;
}
HRESULT STDMETHODCALLTYPE ApiRender(IDirect3DDevice9* d,D3DRENDERSTATETYPE a,DWORD b){return MeasureApi(d,0,[&]{return api_render(d,a,b);});}
HRESULT STDMETHODCALLTYPE ApiTexture(IDirect3DDevice9* d,DWORD a,IDirect3DBaseTexture9* b){return MeasureApi(d,1,[&]{return api_texture(d,a,b);});}
HRESULT STDMETHODCALLTYPE ApiSampler(IDirect3DDevice9* d,DWORD a,D3DSAMPLERSTATETYPE b,DWORD c){return MeasureApi(d,2,[&]{return api_sampler(d,a,b,c);});}
HRESULT STDMETHODCALLTYPE ApiVSConst(IDirect3DDevice9* d,UINT a,const float* b,UINT c){return MeasureApi(d,3,[&]{
    if(!benchmark_constants||internal||d!=admitted||on12_hr!=S_OK||failure)return api_vsconst(d,a,b,c);
    ConstantsLock lock;const DWORD error=GetLastError();
    if(GetCurrentThreadId()!=owner)constants_foreign=true;
    if(constants_armed&&!constants_foreign&&!constants_recording){++constants_checked;
        if(constants_hint.Candidate(a,b,c)){
            // A hint may be stale after state-block Apply or another writer.
            // Shadow mode is admitted only with the pinned wrapper's shared
            // StateBlock::Apply vtable intercepted, including preexisting blocks.
            if(!benchmark_shadow)++constants_queries;
            if(benchmark_shadow||WaterOn12Constants::Same(d,a,b,c)){++constants_skipped;SetLastError(error);return static_cast<HRESULT>(D3D_OK);}}}
    if(constants_recording)++constants_recorded;++constants_forwarded;SetLastError(error);
    const HRESULT result=api_vsconst(d,a,b,c);const DWORD returned=GetLastError();
    if(SUCCEEDED(result)&&!constants_recording&&!constants_foreign)constants_hint.Remember(a,b,c);
    if(FAILED(result))constants_hint.Clear();
    SetLastError(returned);return result;
});}
HRESULT STDMETHODCALLTYPE ApiPSConst(IDirect3DDevice9* d,UINT a,const float* b,UINT c){return MeasureApi(d,4,[&]{return api_psconst(d,a,b,c);});}
HRESULT STDMETHODCALLTYPE ApiVS(IDirect3DDevice9* d,IDirect3DVertexShader9* a){return MeasureApi(d,5,[&]{return api_vs(d,a);});}
HRESULT STDMETHODCALLTYPE ApiPS(IDirect3DDevice9* d,IDirect3DPixelShader9* a){return MeasureApi(d,6,[&]{
    if(internal||d!=admitted||!benchmark_shader_state)return api_ps(d,a);
    ConstantsLock lock;pixel_state_known=false;
    if(GetCurrentThreadId()!=owner)constants_foreign=true;
    const HRESULT result=api_ps(d,a);
    if(SUCCEEDED(result)&&!constants_recording&&!constants_foreign){
        Guard guard;Scope scope;
        try{bool verified=!a;pixel_state_target=a&&PixelTarget(a,&verified);pixel_state_known=verified;}catch(...){pixel_state_known=false;}}
    return result;
});}
HRESULT STDMETHODCALLTYPE ApiStream(IDirect3DDevice9* d,UINT a,IDirect3DVertexBuffer9* b,UINT c,UINT e){return MeasureApi(d,7,[&]{return api_stream(d,a,b,c,e);});}
HRESULT STDMETHODCALLTYPE ApiIndices(IDirect3DDevice9* d,IDirect3DIndexBuffer9* a){return MeasureApi(d,8,[&]{return api_indices(d,a);});}
HRESULT STDMETHODCALLTYPE ApiEnd(IDirect3DDevice9* d){const HRESULT result=MeasureApi(d,9,[&]{return api_end(d);});
    if(d==admitted&&SUCCEEDED(result)){ConstantsLock lock;if(GetCurrentThreadId()!=owner)constants_foreign=true;if(!constants_recording&&!constants_foreign)constants_armed=true;}return result;}
HRESULT STDMETHODCALLTYPE ApiReadback(IDirect3DDevice9* d,IDirect3DSurface9* a,IDirect3DSurface9* b){return MeasureApi(d,10,[&]{return api_readback(d,a,b);});}
HRESULT STDMETHODCALLTYPE ApiStretch(IDirect3DDevice9* d,IDirect3DSurface9* a,const RECT* b,IDirect3DSurface9* c,const RECT* e,D3DTEXTUREFILTERTYPE f){return MeasureApi(d,11,[&]{return api_stretch(d,a,b,c,e,f);});}
HRESULT STDMETHODCALLTYPE ApiClear(IDirect3DDevice9* d,DWORD a,const D3DRECT* b,DWORD c,D3DCOLOR e,float f,DWORD g){return MeasureApi(d,12,[&]{return api_clear(d,a,b,c,e,f,g);});}
HRESULT STDMETHODCALLTYPE ApiBegin(IDirect3DDevice9* d){return MeasureApi(d,13,[&]{return api_begin(d);});}
HRESULT STDMETHODCALLTYPE ApiBeginBlock(IDirect3DDevice9* d){if(d!=admitted)return api_begin_block(d);ConstantsLock lock;
    if(GetCurrentThreadId()!=owner)constants_foreign=true;const HRESULT result=api_begin_block(d);if(SUCCEEDED(result)){constants_recording=true;constants_hint.Clear();pixel_state_known=false;}return result;}
HRESULT STDMETHODCALLTYPE ApiEndBlock(IDirect3DDevice9* d,IDirect3DStateBlock9** block){if(d!=admitted)return api_end_block(d,block);ConstantsLock lock;
    if(GetCurrentThreadId()!=owner)constants_foreign=true;const HRESULT result=api_end_block(d,block);if(SUCCEEDED(result)){constants_recording=false;constants_hint.Clear();pixel_state_known=false;}return result;}
HRESULT STDMETHODCALLTYPE ApiBlockApply(IDirect3DStateBlock9* block){
    ConstantsLock lock;const DWORD error=GetLastError();
    // Conservatively invalidate for every block of this pinned wrapper class,
    // even one belonging to another device. Never inspect private object fields.
    constants_hint.Clear();pixel_state_known=false;++constants_invalidations;
    if(GetCurrentThreadId()!=owner)constants_foreign=true;
    SetLastError(error);return original_block_apply(block);
}
HRESULT STDMETHODCALLTYPE ApiSoftware(IDirect3DDevice9* d,BOOL enabled){
    if(d!=admitted)return api_software(d,enabled);ConstantsLock lock;
    const DWORD error=GetLastError();constants_hint.Clear();pixel_state_known=false;++constants_invalidations;
    if(GetCurrentThreadId()!=owner)constants_foreign=true;
    SetLastError(error);return api_software(d,enabled);
}

bool Shape(const Config& c) {
    const size_t n=wcsnlen_s(c.report,512);constexpr wchar_t suffix[]=L"\\water-probe.private.json";
    if(c.size!=sizeof(c)||c.version!=1||(c.flags!=0&&c.flags!=1&&c.flags!=3)||n>=512||n<wcslen(suffix)+3||c.report[1]!=L':'||c.report[2]!=L'\\'
        ||_wcsicmp(c.report+n-wcslen(suffix),suffix))return false;
    for(size_t i=3;i<n;++i)if(c.report[i]<32||c.report[i]==L':'||c.report[i]==L'/')return false;
    wchar_t full[512]{};const DWORD count=GetFullPathNameW(c.report,512,full,nullptr);
    return count&&count<512&&!_wcsicmp(c.report,full);
}
bool WriteNew(const wchar_t* path,const void* data,DWORD bytes) {
    HANDLE file=CreateFileW(path,GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    DWORD written=0;const bool okay=WriteFile(file,data,bytes,&written,nullptr)&&written==bytes&&FlushFileBuffers(file);
    CloseHandle(file);return okay;
}
bool Artifact(const wchar_t* name,const void* bytes,DWORD count,std::string& hash) {
    return Hash(bytes,count,hash)&&WriteNew((directory+L"\\"+name).c_str(),bytes,count);
}
void Live() {
    std::uint64_t checked=0,queries=0,skipped=0,forwarded=0,recorded=0;bool armed=false,recording=false,foreign=false;
    if(constants_lock_ready){ConstantsLock lock;checked=constants_checked;queries=constants_queries;skipped=constants_skipped;forwarded=constants_forwarded;recorded=constants_recorded;armed=constants_armed;recording=constants_recording;foreign=constants_foreign;}
    std::ostringstream text;text<<"{\"format\":\"s7-on12-water-live-v1\",\"attached\":"<<(admitted?"true":"false")
        <<",\"draws\":"<<draws<<",\"exact_pair_matches\":"<<matches<<",\"failed_original_draws\":"<<failed_draws
        <<",\"presents\":"<<presents<<",\"snapshot_done\":"<<(snapshot?"true":"false")
        <<",\"failure\":"<<failure<<",\"on12_query_hr\":"<<static_cast<long>(on12_hr)
        <<",\"render_thread_id\":"<<owner
        <<",\"device\":{\"creation_hr\":"<<static_cast<long>(device_info.creation_hr)<<",\"behavior_flags\":"<<device_info.creation.BehaviorFlags
        <<",\"software_vertex_processing\":"<<(device_info.software?"true":"false")<<",\"presentation_hr\":"<<static_cast<long>(device_info.presentation_hr)
        <<",\"presentation_interval\":"<<device_info.presentation.PresentationInterval<<",\"swap_effect\":"<<device_info.presentation.SwapEffect
        <<",\"backbuffer_count\":"<<device_info.presentation.BackBufferCount<<",\"windowed\":"<<(device_info.presentation.Windowed?"true":"false")
        <<",\"ex_query_hr\":"<<static_cast<long>(device_info.ex_hr)<<",\"latency_hr\":"<<static_cast<long>(device_info.latency_hr)<<",\"maximum_frame_latency\":"<<device_info.latency<<"}"
        <<",\"pow_cap_requested\":"<<(config.flags?"true":"false")<<",\"caps_applied\":"<<caps_applied<<",\"caps_restored\":"<<caps_restored
        <<",\"world_frames\":"<<world_frames<<",\"pixel_gets\":"<<pixel_gets<<",\"pixel_gets_avoided\":"<<pixel_gets_avoided<<",\"pixel_program_reads\":"<<pixel_reads
        <<",\"pixel_hashes\":"<<pixel_hashes<<",\"vertex_program_reads\":"<<vertex_reads<<",\"vertex_hashes\":"<<vertex_hashes
        <<",\"vertex_staging_allocations\":"<<vertex_allocations<<",\"getter_calls\":"<<getter_calls<<",\"getter_guard_calls\":"<<getter_guard_calls
        <<",\"getter_profile_samples\":"<<getter_samples<<",\"getter_profile_ticks\":"<<getter_ticks
        <<",\"fp_modes_reused\":"<<fp_modes_reused<<",\"fp_modes_changed\":"<<fp_modes_changed<<",\"profile\":{\"sample_stride\":128,\"samples\":"<<profile_samples
        <<",\"frequency\":"<<clock_frequency<<",\"total_ticks\":"<<profile_total<<",\"lookup_ticks\":"<<profile_lookup
        <<",\"original_draw_ticks\":"<<profile_original<<",\"binding_ticks\":"<<profile_bind<<"}"
        <<",\"cache\":{\"pixel_entries\":"<<pixel_cache.count<<",\"pixel_hits\":"<<pixel_cache.hits<<",\"pixel_misses\":"<<pixel_cache.misses
        <<",\"pixel_full\":"<<pixel_cache.full<<",\"vertex_entries\":"<<vertex_cache.count<<",\"vertex_hits\":"<<vertex_cache.hits
        <<",\"vertex_misses\":"<<vertex_cache.misses<<",\"vertex_full\":"<<vertex_cache.full<<",\"resets\":"<<cache_resets<<"}"
        <<",\"benchmark\":{\"enabled\":"<<(config.flags==3?"true":"false")<<",\"legacy\":"<<(benchmark_legacy?"true":"false")
        <<",\"constant_dedupe\":"<<(benchmark_constants?"true":"false")<<",\"sequence\":"<<benchmark_sequence<<",\"command_errors\":"<<benchmark_errors<<",\"camera_hash\":"<<camera_hash
        <<",\"constant_shadow\":"<<(benchmark_shadow?"true":"false")
        <<",\"shader_state\":"<<(benchmark_shader_state?"true":"false")
        <<",\"camera_changes\":"<<camera_changes<<",\"camera_errors\":"<<camera_errors<<"},\"api\":[";
    for(size_t i=0;i<api_stats.size();++i){if(i)text<<",";const auto& value=api_stats[i];text<<"{\"name\":\""<<ApiNames[i]<<"\",\"calls\":"<<value.calls<<",\"samples\":"<<value.samples<<",\"ticks\":"<<value.ticks<<"}";}
    text<<"],\"constants\":{\"checked\":"<<checked<<",\"device_queries\":"<<queries<<",\"skipped\":"<<skipped<<",\"forwarded\":"<<forwarded<<",\"recorded\":"<<recorded
        <<",\"stateblock_invalidations\":"<<constants_invalidations<<",\"armed\":"<<(armed?"true":"false")<<",\"recording\":"<<(recording?"true":"false")<<",\"foreign_thread\":"<<(foreign?"true":"false")<<"}}";
    const auto raw=text.str();const auto temporary=directory+L"\\water-probe-live.private.tmp";
    HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file!=INVALID_HANDLE_VALUE){DWORD count=0;const bool okay=WriteFile(file,raw.data(),static_cast<DWORD>(raw.size()),&count,nullptr)&&count==raw.size();CloseHandle(file);
        if(okay)MoveFileExW(temporary.c_str(),(directory+L"\\water-probe-live.private.json").c_str(),MOVEFILE_REPLACE_EXISTING);}
}
bool Patch(const Slot& s,bool forward) {
    DWORD previous=0,unused=0;if(!VirtualProtect(s.address,sizeof(void*),PAGE_READWRITE,&previous))return false;
    void* before=forward?s.original:s.replacement;void* after=forward?s.replacement:s.original;
    const bool changed=InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(s.address),after,before)==before;
    const bool restored=VirtualProtect(s.address,sizeof(void*),previous,&unused)!=FALSE;
    if(changed&&!restored){InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(s.address),before,after);VirtualProtect(s.address,sizeof(void*),previous,&unused);}
    return changed&&restored;
}
bool Import(HMODULE module,std::vector<Slot>& slots) {
    auto* image=reinterpret_cast<unsigned char*>(module);const auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if(!Range(dos,sizeof(*dos))||dos->e_magic!=IMAGE_DOS_SIGNATURE||dos->e_lfanew<64||dos->e_lfanew>4096)return false;
    const auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+dos->e_lfanew);
    if(!Range(nt,sizeof(*nt))||nt->Signature!=IMAGE_NT_SIGNATURE||nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return false;
    const DWORD size=nt->OptionalHeader.SizeOfImage;
    auto valid=[&](DWORD rva,size_t bytes){return rva<size&&bytes<=size-rva&&Range(image+rva,bytes);};
    const auto d=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];if(!valid(d.VirtualAddress,d.Size))return false;
    auto* entries=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image+d.VirtualAddress);
    for(size_t n=0;n<d.Size/sizeof(*entries)&&entries[n].Name;++n) {
        const auto& e=entries[n];if(!valid(e.Name,12))return false;
        if(_stricmp(reinterpret_cast<char*>(image+e.Name),"vbase90.dll"))continue;
        if(!e.OriginalFirstThunk)return false;
        for(DWORD i=0;;++i) {
            const DWORD offset=i*sizeof(IMAGE_THUNK_DATA32);
            if(!valid(e.OriginalFirstThunk+offset,4)||!valid(e.FirstThunk+offset,4))return false;
            const DWORD thunk=reinterpret_cast<IMAGE_THUNK_DATA32*>(image+e.OriginalFirstThunk+offset)->u1.AddressOfData;
            if(!thunk)break;if(IMAGE_SNAP_BY_ORDINAL32(thunk))continue;
            if(!valid(thunk,sizeof(WORD)+sizeof(GetterName)))return false;
            const auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image+thunk);
            if(std::memcmp(name->Name,GetterName,sizeof(GetterName)))continue;
            auto** slot=reinterpret_cast<void**>(image+e.FirstThunk+offset);
            if(*slot!=reinterpret_cast<void*>(original_getter))return false;
            slots.push_back({slot,*slot,nullptr});
        }
    }
    return true;
}

std::string Texture(IDirect3DDevice9* device,DWORD sampler,DWORD& budget) {
    std::ostringstream out;out<<"{\"sampler\":"<<sampler;Ref<IDirect3DBaseTexture9> base;
    const HRESULT got=device->GetTexture(sampler,&base.value);out<<",\"get_hr\":"<<static_cast<long>(got);
    if(FAILED(got)||!base.value){out<<",\"bound\":false}";return out.str();}
    const auto type=base.value->GetType();out<<",\"bound\":true,\"type\":"<<type;
    if(type==D3DRTYPE_TEXTURE) {
        auto* texture=static_cast<IDirect3DTexture9*>(base.value);D3DSURFACE_DESC desc{};
        if(SUCCEEDED(texture->GetLevelDesc(0,&desc))) {
            out<<",\"width\":"<<desc.Width<<",\"height\":"<<desc.Height<<",\"format\":"<<desc.Format
                <<",\"usage\":"<<desc.Usage<<",\"pool\":"<<desc.Pool;
            const DWORD bpp=desc.Format==D3DFMT_A32B32G32R32F?16:desc.Format==D3DFMT_A16B16G16R16F?8:
                (desc.Format==D3DFMT_A8R8G8B8||desc.Format==D3DFMT_X8R8G8B8||desc.Format==D3DFMT_R32F)?4:0;
            if(bpp&&desc.Width&&desc.Height&&desc.Width<=4096&&desc.Height<=4096
                &&static_cast<std::uint64_t>(desc.Width)*desc.Height*bpp<=budget) {
                Ref<IDirect3DSurface9> source,staging;D3DLOCKED_RECT locked{};
                HRESULT copied=texture->GetSurfaceLevel(0,&source.value);
                if(SUCCEEDED(copied))copied=device->CreateOffscreenPlainSurface(desc.Width,desc.Height,desc.Format,D3DPOOL_SYSTEMMEM,&staging.value,nullptr);
                if(SUCCEEDED(copied))copied=device->GetRenderTargetData(source.value,staging.value);
                bool locked_staging=false;HRESULT lock=copied;
                if(SUCCEEDED(copied)){lock=staging.value->LockRect(&locked,nullptr,D3DLOCK_READONLY);locked_staging=SUCCEEDED(lock);}
                if(FAILED(lock))lock=texture->LockRect(0,&locked,nullptr,D3DLOCK_READONLY);
                if(SUCCEEDED(lock)) {
                    const DWORD row=desc.Width*bpp,bytes=row*desc.Height;
                    if(locked.Pitch>=static_cast<INT>(row)&&locked.pBits) {
                        std::vector<unsigned char> raw(bytes);
                        for(DWORD y=0;y<desc.Height;++y)std::memcpy(raw.data()+static_cast<size_t>(y)*row,static_cast<unsigned char*>(locked.pBits)+static_cast<size_t>(y)*locked.Pitch,row);
                        std::string hash;const auto name=L"water-sampler-"+std::to_wstring(sampler)+L".private.bin";
                        if(Artifact(name.c_str(),raw.data(),bytes,hash)){out<<",\"captured_bytes\":"<<bytes<<",\"sha256\":\""<<hash<<"\"";budget-=bytes;}
                    }
                    if(locked_staging)staging.value->UnlockRect();else texture->UnlockRect(0);
                }
                out<<",\"readback_hr\":"<<static_cast<long>(copied)<<",\"lock_hr\":"<<static_cast<long>(lock);
            }
        }
    }
    out<<",\"sampler_states\":[";
    for(DWORD state=1;state<=13;++state){DWORD value=0;const HRESULT hr=device->GetSamplerState(sampler,static_cast<D3DSAMPLERSTATETYPE>(state),&value);if(state>1)out<<",";out<<"["<<static_cast<long>(hr)<<","<<value<<"]";}
    out<<"]}";return out.str();
}

void Snapshot(IDirect3DDevice9* device,D3DPRIMITIVETYPE primitive,INT base,UINT minimum,UINT vertices,UINT start,UINT count) {
    if(snapshot||failure||!count)return;Ref<IDirect3DPixelShader9> ps;
    UINT pixel_bytes=0;if(FAILED(device->GetPixelShader(&ps.value))||!ps.value||FAILED(ps.value->GetFunction(nullptr,&pixel_bytes))||pixel_bytes!=3740)return;
    std::array<unsigned char,3740> pixel{};std::string hash;
    if(FAILED(ps.value->GetFunction(pixel.data(),&pixel_bytes))||pixel_bytes!=pixel.size()||!Hash(pixel.data(),pixel_bytes,hash)||hash!=Pixel)return;
    Ref<IDirect3DVertexShader9> vs;UINT vertex_bytes=0;
    if(FAILED(device->GetVertexShader(&vs.value))||!vs.value||FAILED(vs.value->GetFunction(nullptr,&vertex_bytes))||vertex_bytes!=2592)return;
    std::array<unsigned char,2592> vertex{};
    if(FAILED(vs.value->GetFunction(vertex.data(),&vertex_bytes))||vertex_bytes!=vertex.size()||!Hash(vertex.data(),vertex_bytes,hash)||hash!=Vertex)return;
    ++matches;
    struct Constants {DWORD size,version,vs_count,ps_count;float vs[256*4],ps[224*4];} constants{};
    constants.size=sizeof(constants);constants.version=1;constants.vs_count=256;constants.ps_count=224;
    const HRESULT vs_hr=device->GetVertexShaderConstantF(0,constants.vs,256),ps_hr=device->GetPixelShaderConstantF(0,constants.ps,224);
    if(FAILED(vs_hr)||FAILED(ps_hr)){failure=301;return;}
    std::string constants_hash,vs_hash,ps_hash;
    if(!Artifact(L"water-constants.private.bin",&constants,sizeof(constants),constants_hash)
        ||!Artifact(L"water-vertex.private.bin",vertex.data(),vertex_bytes,vs_hash)
        ||!Artifact(L"water-pixel.private.bin",pixel.data(),pixel_bytes,ps_hash)){failure=302;return;}
    unsigned vs_bad=0,ps_bad=0;for(float value:constants.vs)if(!std::isfinite(value))++vs_bad;for(float value:constants.ps)if(!std::isfinite(value))++ps_bad;
    std::ostringstream out;out<<"{\"format\":\"s7-on12-water-inputs-v1\",\"input_capture_uses_only_getters\":true,\"pow_cap_requested\":"<<(config.flags?"true":"false")<<",\"draw_replayed\":false,\"draw_skipped\":false,"
        <<"\"vertex_sha256\":\""<<vs_hash<<"\",\"pixel_sha256\":\""<<ps_hash<<"\",\"constants_sha256\":\""<<constants_hash
        <<"\",\"vs_nonfinite\":"<<vs_bad<<",\"ps_nonfinite\":"<<ps_bad<<",\"draw\":["<<primitive<<","<<base<<","<<minimum<<","<<vertices<<","<<start<<","<<count<<"],\"samplers\":[";
    DWORD budget=48*1024*1024;const DWORD samplers[]{0,1,4,5,6,7};
    for(size_t i=0;i<6;++i){if(i)out<<",";out<<Texture(device,samplers[i],budget);}
    out<<"]}";const auto raw=out.str();
    if(!WriteNew(config.report,raw.data(),static_cast<DWORD>(raw.size()))){failure=303;return;}
    snapshot=true;
}

bool PixelTarget(IDirect3DPixelShader9* shader,bool* verified) {
    if(verified)*verified=false;
    bool target=false;if(pixel_cache.Lookup(shader,target)){if(verified)*verified=true;return target;}
    UINT size=0;if(FAILED(ReadProgram(shader,nullptr,&size,pixel_reads)))return false;
    if(size==3740) {
        std::array<unsigned char,3740> program{};std::string hash;
        if(FAILED(ReadProgram(shader,program.data(),&size,pixel_reads))||size!=program.size())return false;
        ++pixel_hashes;if(!Hash(program.data(),size,hash))return false;target=hash==Pixel;
    }
    pixel_cache.Store(shader,target);if(verified)*verified=true;return target;
}
bool VertexTarget(IDirect3DDevice9* device,IDirect3DVertexShader9* shader) {
    bool target=false;if(vertex_cache.Lookup(shader,target))return target;
    UINT size=0;if(FAILED(ReadProgram(shader,nullptr,&size,vertex_reads)))return false;
    if(size==2592) {
        ++vertex_allocations;std::vector<std::uint32_t> source(648);std::string hash;
        if(FAILED(ReadProgram(shader,source.data(),&size,vertex_reads))||size!=2592)return false;
        ++vertex_hashes;if(!Hash(source.data(),size,hash))return false;target=hash==Vertex;
        if(target&&!corrected) {
            std::vector<std::uint32_t> program;
            if(!WaterOn12PowCap::Generate(source,program)||FAILED(device->CreateVertexShader(reinterpret_cast<const DWORD*>(program.data()),&corrected))){failure=305;return false;}
            std::string generated;Artifact(L"water-capped-vertex.private.bin",program.data(),static_cast<DWORD>(program.size()*4),generated);
        }
    }
    vertex_cache.Store(shader,target);return target;
}
void ClearPrograms() {
    if(constants_lock_ready){ConstantsLock lock;pixel_state_known=false;}
    pixel_cache.Clear();vertex_cache.Clear();
    if(corrected){corrected->Release();corrected=nullptr;}
    ++cache_resets;
    camera_frame=~std::uint64_t(0);
}
HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* d,D3DPRESENT_PARAMETERS* p) {
    if(!internal&&d==admitted&&GetCurrentThreadId()==owner){Guard guard;Scope scope;ClearPrograms();
        if(constants_lock_ready){ConstantsLock lock;constants_hint.Clear();const HRESULT result=original_reset(d,p);if(SUCCEEDED(result)){constants_armed=false;constants_recording=false;ObserveDevice(d);}return result;}
        const HRESULT result=original_reset(d,p);if(SUCCEEDED(result))ObserveDevice(d);return result;}
    return original_reset(d,p);
}
bool LegacyTarget(IDirect3DDevice9* d,Ref<IDirect3DPixelShader9>& ps,Ref<IDirect3DVertexShader9>& vs) {
    UINT size=0;std::array<unsigned char,3740> pixel{};std::string hash;++pixel_gets;
    bool target=SUCCEEDED(d->GetPixelShader(&ps.value))&&ps.value&&SUCCEEDED(ReadProgram(ps.value,nullptr,&size,pixel_reads))&&size==pixel.size();
    if(target)target=SUCCEEDED(ReadProgram(ps.value,pixel.data(),&size,pixel_reads))&&size==pixel.size();
    if(target){++pixel_hashes;target=Hash(pixel.data(),size,hash)&&hash==Pixel;}
    if(!target)return false;
    ++vertex_allocations;std::vector<std::uint32_t> source(648);size=0;
    target=SUCCEEDED(d->GetVertexShader(&vs.value))&&vs.value&&SUCCEEDED(ReadProgram(vs.value,nullptr,&size,vertex_reads))&&size==2592;
    if(target)target=SUCCEEDED(ReadProgram(vs.value,source.data(),&size,vertex_reads))&&size==2592;
    if(target){++vertex_hashes;target=Hash(source.data(),size,hash)&&hash==Vertex;}
    if(target){std::array<float,32> scattering{};target=SUCCEEDED(d->GetVertexShaderConstantF(87,scattering.data(),8));for(float value:scattering)if(value!=0.f)target=false;}
    if(target&&!corrected){std::vector<std::uint32_t> program;
        if(!WaterOn12PowCap::Generate(source,program)||FAILED(d->CreateVertexShader(reinterpret_cast<const DWORD*>(program.data()),&corrected))){failure=305;return false;}
        std::string generated;Artifact(L"water-capped-vertex.private.bin",program.data(),static_cast<DWORD>(program.size()*4),generated);}
    return target;
}
HRESULT DrawWithCap(IDirect3DDevice9* d,D3DPRIMITIVETYPE p,INT b,UINT m,UINT v,UINT s,UINT c,bool sample=false) {
    const auto lookup_begin=sample?Clock():0;
    Ref<IDirect3DPixelShader9> ps;Ref<IDirect3DVertexShader9> vs;
    bool target=(config.flags&1)&&on12_hr==S_OK&&!failure;
    try {if(target) {
        if(benchmark_legacy)target=LegacyTarget(d,ps,vs);
        else {
        bool known_non_target=false;
        if(benchmark_shader_state){ConstantsLock lock;known_non_target=!constants_foreign&&!constants_recording&&pixel_state_known&&!pixel_state_target;}
        if(known_non_target){++pixel_gets_avoided;target=false;}
        else {
        ++pixel_gets;
        const HRESULT got=d->GetPixelShader(&ps.value);
        bool verified=!ps.value;target=SUCCEEDED(got)&&ps.value&&PixelTarget(ps.value,&verified);
        if(benchmark_shader_state&&SUCCEEDED(got)&&verified){ConstantsLock lock;
            if(!constants_foreign&&!constants_recording){pixel_state_target=target;pixel_state_known=true;}}
        }
        if(target) {
            target=SUCCEEDED(d->GetVertexShader(&vs.value))&&vs.value;
            if(target) {
                std::array<float,32> scattering{};
                if(FAILED(d->GetVertexShaderConstantF(87,scattering.data(),8)))target=false;
                for(float value:scattering)if(value!=0.f)target=false;
            }
            if(target)target=VertexTarget(d,vs.value);
        }
        }
        if(target)ObserveCamera(d);
    }}catch(...){failure=308;target=false;}
    const auto lookup_end=sample?Clock():0;
    bool bound=false;
    if(target&&corrected){bound=SUCCEEDED(d->SetVertexShader(corrected));if(!bound)failure=306;else ++caps_applied;}
    const auto draw_begin=sample?Clock():0;
    const HRESULT hr=original_draw(d,p,b,m,v,s,c);
    const auto draw_end=sample?Clock():0;
    if(bound){if(SUCCEEDED(d->SetVertexShader(vs.value)))++caps_restored;else failure=307;}
    if(sample){const auto end=Clock();profile_lookup+=lookup_end-lookup_begin;profile_original+=draw_end-draw_begin;profile_bind+=(draw_begin-lookup_end)+(end-draw_end);}
    return hr;
}
HRESULT STDMETHODCALLTYPE HookDraw(IDirect3DDevice9* d,D3DPRIMITIVETYPE p,INT b,UINT m,UINT v,UINT s,UINT c) {
    if(!internal&&d==admitted&&GetCurrentThreadId()==owner) {
        ++draws;const bool sample=snapshot&&(draws%128==0);const auto begin=sample?Clock():0;HRESULT hr=E_FAIL;
        {Guard guard(benchmark_legacy);Scope scope;
        try{Snapshot(d,p,b,m,v,s,c);}catch(...){failure=304;}
        // Allocation/hash exceptions are handled before any temporary binding.
        hr=DrawWithCap(d,p,b,m,v,s,c,sample);}
        if(sample){++profile_samples;profile_total+=Clock()-begin;}
        if(FAILED(hr))++failed_draws;return hr;
    }
    return original_draw(d,p,b,m,v,s,c);
}
HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* d,const RECT* a,const RECT* b,HWND w,const RGNDATA* r) {
    if(!internal&&d==admitted&&GetCurrentThreadId()==owner){Guard guard;Scope scope;++presents;if(draws!=last_present_draws){++world_frames;last_present_draws=draws;}if(presents==1||presents%60==0){BenchmarkCommand();Live();}}
    return original_present(d,a,b,w,r);
}
void Attach(IDirect3DDevice9* device) {
    if(!device||admitted||failure)return;
    if(!Range(device,sizeof(void*)))return;auto** table=*reinterpret_cast<void***>(device);
    if(!Range(table,119*sizeof(void*))||!Belongs(table,wrapper)||!Belongs(table[82],wrapper)||!Belongs(table[17],wrapper)){failure=201;return;}
    for(size_t slot=0;slot<119;++slot) {
        MEMORY_BASIC_INFORMATION page{};
        if(!table[slot]||VirtualQuery(table[slot],&page,sizeof(page))!=sizeof(page)||page.State!=MEM_COMMIT||page.Type!=MEM_IMAGE
            ||(page.Protect&(PAGE_NOACCESS|PAGE_GUARD))
            ||!(page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))
            ||(page.AllocationBase!=wrapper&&!(apphelp&&page.AllocationBase==apphelp&&(slot==0||slot==2||slot==13||slot==16)))){failure=205;return;}
    }
    std::vector<Slot> slots{{&table[82],table[82],reinterpret_cast<void*>(HookDraw)},{&table[17],table[17],reinterpret_cast<void*>(HookPresent)},
                           {&table[16],table[16],reinterpret_cast<void*>(HookReset)}};
    original_draw=reinterpret_cast<DrawIndexed>(table[82]);original_present=reinterpret_cast<Present>(table[17]);original_reset=reinterpret_cast<Reset>(table[16]);
    if(config.flags==3) {
        if(!InitializeCriticalSectionAndSpinCount(&constants_lock,0)){failure=207;return;}constants_lock_ready=true;
        Ref<IDirect3DStateBlock9> block;
        if(FAILED(device->CreateStateBlock(D3DSBT_VERTEXSTATE,&block.value))||!block.value){failure=208;return;}
        auto** block_table=*reinterpret_cast<void***>(block.value);
        if(!Range(block_table,6*sizeof(void*))||!Belongs(block_table,wrapper)){failure=208;return;}
        for(size_t i=0;i<6;++i){MEMORY_BASIC_INFORMATION page{};
            if(!block_table[i]||VirtualQuery(block_table[i],&page,sizeof(page))!=sizeof(page)||page.Type!=MEM_IMAGE||page.State!=MEM_COMMIT
                ||page.AllocationBase!=wrapper||(page.Protect&(PAGE_GUARD|PAGE_NOACCESS))
                ||!(page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))){failure=208;return;}}
        original_block_apply=reinterpret_cast<BlockApplyFn>(block_table[5]);
        slots.push_back({&block_table[5],block_table[5],reinterpret_cast<void*>(ApiBlockApply)});
        api_render=reinterpret_cast<RenderStateFn>(table[57]);api_texture=reinterpret_cast<TextureFn>(table[65]);api_sampler=reinterpret_cast<SamplerFn>(table[69]);
        api_vsconst=reinterpret_cast<ConstantFn>(table[94]);api_psconst=reinterpret_cast<ConstantFn>(table[109]);api_vs=reinterpret_cast<VertexFn>(table[92]);api_ps=reinterpret_cast<PixelFn>(table[107]);
        api_stream=reinterpret_cast<StreamFn>(table[100]);api_indices=reinterpret_cast<IndicesFn>(table[104]);api_end=reinterpret_cast<SceneFn>(table[42]);api_begin=reinterpret_cast<SceneFn>(table[41]);
        api_readback=reinterpret_cast<ReadbackFn>(table[32]);api_stretch=reinterpret_cast<StretchFn>(table[34]);api_clear=reinterpret_cast<ClearFn>(table[43]);
        api_begin_block=reinterpret_cast<SceneFn>(table[60]);api_end_block=reinterpret_cast<EndBlockFn>(table[61]);
        api_software=reinterpret_cast<SoftwareFn>(table[77]);
        const std::pair<unsigned,void*> observed[]={{57,reinterpret_cast<void*>(ApiRender)},{65,reinterpret_cast<void*>(ApiTexture)},{69,reinterpret_cast<void*>(ApiSampler)},
            {94,reinterpret_cast<void*>(ApiVSConst)},{109,reinterpret_cast<void*>(ApiPSConst)},{92,reinterpret_cast<void*>(ApiVS)},{107,reinterpret_cast<void*>(ApiPS)},
            {100,reinterpret_cast<void*>(ApiStream)},{104,reinterpret_cast<void*>(ApiIndices)},{42,reinterpret_cast<void*>(ApiEnd)},{41,reinterpret_cast<void*>(ApiBegin)},
            {32,reinterpret_cast<void*>(ApiReadback)},{34,reinterpret_cast<void*>(ApiStretch)},{43,reinterpret_cast<void*>(ApiClear)},
            {60,reinterpret_cast<void*>(ApiBeginBlock)},{61,reinterpret_cast<void*>(ApiEndBlock)},{77,reinterpret_cast<void*>(ApiSoftware)}};
        for(const auto& item:observed)slots.push_back({&table[item.first],table[item.first],item.second});
    }
    size_t applied=0;
    for(const auto& slot:slots){if(!Patch(slot,true)){for(size_t n=applied;n>0;--n)Patch(slots[n-1],false);failure=202;return;}++applied;}
    admitted=device;owner=GetCurrentThreadId();LARGE_INTEGER frequency{};QueryPerformanceFrequency(&frequency);clock_frequency=frequency.QuadPart;Ref<IDirect3DDevice9On12> on12;
    on12_hr=device->QueryInterface(__uuidof(IDirect3DDevice9On12),reinterpret_cast<void**>(&on12.value));
    ObserveDevice(device);
    if(config.flags&&on12_hr!=S_OK)failure=206;Live();
}
IDirect3DDevice9* __cdecl HookGetter() {
    ++getter_calls;const bool sample=snapshot&&(getter_calls%128==0);const auto begin=sample?Clock():0;
    IDirect3DDevice9* device=original_getter();
    // After admission Attach() immediately returns. Do not save/restore FPU
    // state on every original engine getter just to discover that again.
    // The original getter is still called every time; its FP/LastError effects
    // remain untouched, exactly as before (the old guard also began afterward).
    if(!internal&&(benchmark_legacy||(!admitted&&!failure)||(failure&&!failure_reported))) {
        ++getter_guard_calls;Guard guard(benchmark_legacy);Scope scope;
        try{Attach(device);if(failure&&!failure_reported){Live();failure_reported=true;}}catch(...){failure=204;}
    }
    if(sample){++getter_samples;getter_ticks+=Clock()-begin;}
    return device;
}
}

extern "C" DWORD WINAPI WePlayWaterOn12ProbeInitialize(void* opaque) {
    using namespace WaterOn12Identity;Guard guard;
    try {Config c{};SIZE_T read=0;
    if(!opaque||!ReadProcessMemory(GetCurrentProcess(),opaque,&c,sizeof(c),&read)||read!=sizeof(c)||!Shape(c))return 0;
    if(original_getter||!Module(GetModuleHandleW(nullptr),Game)||!Module(GetModuleHandleW(L"vbase90.dll"),Base)
        ||!Module(GetModuleHandleW(L"vision90.dll"),Vision)||!Parents(c.report)||GetFileAttributesW(c.report)!=INVALID_FILE_ATTRIBUTES)return 101;
    wchar_t image[512]{};if(!GetModuleFileNameW(nullptr,image,512))return 102;
    const auto at=std::wstring(image).find_last_of(L'\\');if(at==std::wstring::npos)return 102;
    const auto bin=std::wstring(image).substr(0,at);wrapper=GetModuleHandleW((bin+L"\\dxwrapper.dll").c_str());
    if(!Module(GetModuleHandleW((bin+L"\\d3d9.dll").c_str()),Stub)||!Module(wrapper,Wrapper))return 103;
    apphelp=GetModuleHandleW(L"apphelp.dll");
    if(apphelp&&!Module(apphelp,"34655b97cbcaa5b93733da1d9118e0663735c442eb0016671a98389f3cde863e"))return 109;
    config=c;directory=std::wstring(c.report).substr(0,std::wstring(c.report).find_last_of(L'\\'));
    original_getter=reinterpret_cast<Getter>(GetProcAddress(GetModuleHandleW(L"vbase90.dll"),GetterName));if(!original_getter)return 104;
    std::vector<Slot> slots;
    if(!Import(GetModuleHandleW(nullptr),slots)||!Import(GetModuleHandleW(L"vision90.dll"),slots)||slots.empty())return 105;
    HMODULE self=nullptr;if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<const wchar_t*>(WePlayWaterOn12ProbeInitialize),&self))return 106;
    size_t applied=0;for(auto& slot:slots){slot.replacement=reinterpret_cast<void*>(HookGetter);if(!Patch(slot,true)){for(size_t i=applied;i>0;--i)Patch(slots[i-1],false);return 107;}++applied;}
    Live();return 1;
    }catch(...){return 108;}
}

#ifdef S7_WATER_ON12_PROBE_FIXTURE
IDirect3DDevice9* __cdecl FixtureGetter(){return admitted;}
HRESULT STDMETHODCALLTYPE FixtureBlockApply(IDirect3DStateBlock9*){SetLastError(0x9876);return D3DERR_INVALIDCALL;}
HRESULT fixture_pixel_result=D3D_OK;unsigned fixture_draws=0;
HRESULT STDMETHODCALLTYPE FixturePixel(IDirect3DDevice9*,IDirect3DPixelShader9*){SetLastError(0x4321);return fixture_pixel_result;}
HRESULT STDMETHODCALLTYPE FixtureDraw(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT){++fixture_draws;return D3D_OK;}
float fixture_constants[1024]{};unsigned fixture_uploads=0;
HRESULT STDMETHODCALLTYPE FixtureUpload(IDirect3DDevice9*,UINT start,const float* data,UINT count){
    ++fixture_uploads;if(!WaterOn12Constants::Valid(start,data,count))return D3DERR_INVALIDCALL;
    std::memcpy(fixture_constants+start*4,data,count*16);return D3D_OK;
}
DWORD WINAPI FixtureForeignUpload(void*){const float data[]={5,6,7,8};return static_cast<DWORD>(ApiVSConst(admitted,0,data,1));}
int main(){Config c{};c.size=sizeof(c);c.version=1;wcscpy_s(c.report,L"C:\\work\\case\\water-probe.private.json");unsigned checks=0;
    auto check=[&](bool okay){++checks;if(!okay)std::exit(1);};check(Shape(c));c.flags=1;check(Shape(c));c.flags=2;check(!Shape(c));c.flags=3;check(Shape(c));c.flags=0;c.version=2;check(!Shape(c));c.version=1;
    c.size--;check(!Shape(c));c.size++;wcscpy_s(c.report,L"C:\\work\\case\\..\\case\\water-probe.private.json");check(!Shape(c));
    std::string hash;check(Hash("abc",3,hash)&&hash=="ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    SYSTEM_INFO system{};GetSystemInfo(&system);const SIZE_T page=system.dwPageSize;
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,page*2,MEM_COMMIT|MEM_RESERVE,PAGE_READWRITE));check(memory!=nullptr);
    DWORD previous=0;check(VirtualProtect(memory+page,page,PAGE_READONLY,&previous)&&Range(memory+page-4,16));
    check(VirtualProtect(memory+page,page,PAGE_NOACCESS,&previous)&&!Range(memory+page-4,16));
    check(!Range(reinterpret_cast<const void*>(0xfffffff0),32));VirtualFree(memory,0,MEM_RELEASE);
    check(!WePlayWaterOn12ProbeInitialize(nullptr));check(!admitted);
    std::vector<std::uint32_t> empty(648),generated;check(!WaterOn12PowCap::Generate(empty,generated));
    empty.front()=0xfffe0300;empty.back()=0xffff;
    const std::uint32_t first[]={0x03000020,0x80080002,0x80ff0000,0xa0ff0077},second[]={0x03000020,0x80010005,0x80aa0000,0xa0ff0077};
    std::memcpy(empty.data()+469,first,sizeof(first));std::memcpy(empty.data()+486,second,sizeof(second));
    const std::uint32_t products[]={0x03000005,0x80040000,0x80ff0000,0x80000005,0x03000005,0x80080000,0x80ff0002,0x80ff0000};
    std::memcpy(empty.data()+490,products,sizeof(products));
    check(WaterOn12PowCap::Generate(empty,generated)&&generated.size()==662);
    check(generated[240]==0x05000051&&generated[242]==0x7f7fffff&&generated[504]==0x0300000a&&generated[508]==0x0300000a);
    empty[469]^=1;check(!WaterOn12PowCap::Generate(empty,generated));
    admitted=reinterpret_cast<IDirect3DDevice9*>(0x12340000);original_getter=FixtureGetter;
    alignas(16) unsigned char before[512]{},after[512]{};SetLastError(0x2468);_fxsave(before);
    for(unsigned n=0;n<10000;++n)if(HookGetter()!=admitted)std::exit(1);
    _fxsave(after);check(!std::memcmp(before,after,sizeof(before)));check(GetLastError()==0x2468);
    check(getter_calls==10000&&!getter_guard_calls);admitted=nullptr;original_getter=nullptr;
    alignas(16) unsigned char original_fp[512]{},inside[512]{};_fxsave(original_fp);unsigned ignored=0;
    _controlfp_s(&ignored,_MCW_EM|_RC_NEAR,_MCW_EM|_MCW_RC);
    const auto reused=fp_modes_reused;{Guard guard;_fxsave(inside);check(SafeControlState(inside));}check(fp_modes_reused==reused+1);
    _controlfp_s(&ignored,_RC_DOWN,_MCW_RC);_fxsave(before);const auto changed=fp_modes_changed;
    {Guard guard;_fxsave(inside);check(SafeControlState(inside));}_fxsave(after);
    check(fp_modes_changed==changed+1);check(!std::memcmp(before,after,sizeof(before)));
    _controlfp_s(&ignored,_RC_NEAR,_MCW_RC);_mm_setcsr((_mm_getcsr()&~0x6000u)|0x4000u);_fxsave(before);
    {Guard guard;_fxsave(inside);check(SafeControlState(inside));}_fxsave(after);check(!std::memcmp(before,after,sizeof(before)));
    unsigned char synthetic[512]{};const std::uint16_t control=0x3f;const std::uint32_t simd=0x1f80;
    std::memcpy(synthetic,&control,2);std::memcpy(synthetic+24,&simd,4);check(SafeControlState(synthetic));synthetic[0]=0;check(!SafeControlState(synthetic));
    _fxrstor(original_fp);
    BenchmarkControl control_case{16,1,1,0};check(BenchmarkShape(control_case));control_case.legacy=2;check(BenchmarkShape(control_case));control_case.legacy=3;check(BenchmarkShape(control_case));control_case.legacy=4;check(BenchmarkShape(control_case));control_case.legacy=5;check(!BenchmarkShape(control_case));control_case.legacy=1;control_case.sequence=0;check(!BenchmarkShape(control_case));
    check(InitializeCriticalSectionAndSpinCount(&constants_lock,0)!=FALSE);constants_lock_ready=true;owner=GetCurrentThreadId();
    const float uploaded[]={1,2,3,4};constants_hint.Remember(0,uploaded,1);check(constants_hint.Candidate(0,uploaded,1));
    original_block_apply=FixtureBlockApply;SetLastError(0x2468);
    check(ApiBlockApply(nullptr)==D3DERR_INVALIDCALL);check(GetLastError()==0x9876);
    check(constants_invalidations==1&&!constants_hint.Candidate(0,uploaded,1)&&!constants_foreign);
    config.flags=3;admitted=reinterpret_cast<IDirect3DDevice9*>(0x12340000);on12_hr=S_OK;api_vsconst=FixtureUpload;
    benchmark_constants=true;benchmark_shadow=true;constants_armed=true;
    check(ApiVSConst(admitted,0,uploaded,1)==D3D_OK&&fixture_uploads==1);
    const auto skipped=constants_skipped;SetLastError(0x5678);
    check(ApiVSConst(admitted,0,uploaded,1)==D3D_OK&&fixture_uploads==1&&constants_skipped==skipped+1&&GetLastError()==0x5678);
    check(!std::memcmp(fixture_constants,uploaded,16)&&constants_queries==0);
    api_ps=FixturePixel;original_draw=FixtureDraw;benchmark_shader_state=true;
    check(ApiPS(admitted,nullptr)==D3D_OK&&pixel_state_known&&!pixel_state_target&&GetLastError()==0x4321);
    check(DrawWithCap(admitted,D3DPT_TRIANGLELIST,0,0,3,0,1)==D3D_OK&&fixture_draws==1&&pixel_gets_avoided==1);
    fixture_pixel_result=E_FAIL;check(ApiPS(admitted,nullptr)==E_FAIL&&!pixel_state_known);fixture_pixel_result=D3D_OK;
    constants_recording=true;check(ApiPS(admitted,nullptr)==D3D_OK&&!pixel_state_known);constants_recording=false;
    pixel_state_known=true;
    check(ApiBlockApply(nullptr)==D3DERR_INVALIDCALL&&!constants_hint.Candidate(0,uploaded,1));
    check(!pixel_state_known);benchmark_shader_state=false;
    check(ApiVSConst(admitted,0,uploaded,1)==D3D_OK&&fixture_uploads==2);
    constants_recording=true;
    check(ApiVSConst(admitted,0,uploaded,1)==D3D_OK&&ApiVSConst(admitted,0,uploaded,1)==D3D_OK&&fixture_uploads==4);
    constants_recording=false;
    check(ApiVSConst(admitted,256,uploaded,1)==D3DERR_INVALIDCALL&&!constants_hint.Candidate(0,uploaded,1));
    check(ApiVSConst(admitted,0,uploaded,1)==D3D_OK&&fixture_uploads==6);
    HANDLE foreign=CreateThread(nullptr,0,FixtureForeignUpload,nullptr,0,nullptr);check(foreign!=nullptr);
    check(WaitForSingleObject(foreign,5000)==WAIT_OBJECT_0);CloseHandle(foreign);
    check(constants_foreign&&fixture_uploads==7);
    check(ApiVSConst(admitted,0,uploaded,1)==D3D_OK&&ApiVSConst(admitted,0,uploaded,1)==D3D_OK&&fixture_uploads==9);
    DeleteCriticalSection(&constants_lock);constants_lock_ready=false;
    std::printf("{\"cpu_checks\":%u,\"gpu_accessed\":false}\n",checks);return 0;}
#endif
