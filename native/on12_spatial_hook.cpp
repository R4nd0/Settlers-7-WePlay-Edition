// Opt-in laboratory host for native D3D12 AA and scene/UI separation.
#include "on12_frame_context.h"
#include "on12_output_window.h"
#include "on12_scene_targets.h"
#include "on12_scene_boundary.h"
#include "on12_temporal_inputs.h"
#include "on12_depth_capture.h"
#include "water_on12_identity.h"
#include "on12_spatial_shader.generated.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <sstream>
#include <atomic>

namespace {
using namespace WaterOn12Identity;
using Microsoft::WRL::ComPtr;
constexpr char GetterName[] = "?GetD3DDevice@VVideo@@SAPAUIDirect3DDevice9@@XZ";
struct Config { DWORD size, version, flags; wchar_t report[512]; };
struct UpscaleConfig { DWORD size, version, flags, sw, sh, ow, oh; wchar_t report[512]; };
static_assert(sizeof(Config) == 1036, "Spatial host ABI");
static_assert(sizeof(UpscaleConfig) == 1052, "Upscale host ABI");
using Getter = IDirect3DDevice9* (__cdecl*)();
using Present = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
using DeinitD3D = void (__cdecl*)();
using DeinitScreen = bool (__cdecl*)(void*);
struct Slot { void** address; void* original; void* replacement; };
Getter original_getter = nullptr;
Present original_present = nullptr; Reset original_reset = nullptr;
DeinitD3D original_deinit_d3d = nullptr; DeinitScreen original_deinit_screen = nullptr;
HMODULE wrapper = nullptr, apphelp = nullptr;
IDirect3DDevice9* admitted = nullptr; DWORD owner = 0;
// Process-lifetime host: never run a COM destructor after renderer DLL teardown.
On12Graphics::FrameContext* context = nullptr;
On12Graphics::SceneTargets* scene = nullptr;
On12Graphics::SceneBoundary* boundary = nullptr;
std::array<void*,119> scene_original{};
bool scene_mode = false;
std::uint64_t ui_draw_calls = 0, composite_draw_calls = 0;
On12Graphics::SpatialSettings settings;
On12Graphics::OutputWindow output_window;
// Explicit owner-thread release; no COM destructor during DLL teardown.
IDirect3DSwapChain9* output_chain = nullptr;
void DropOutput() { auto* previous = output_chain; output_chain = nullptr; if (previous) previous->Release(); }
UINT render_width = 0, render_height = 0, output_width = 0, output_height = 0;
bool stopped = false;
bool stopping = false, retired_valid = false;
On12Graphics::FrameStats retired_stats;
std::atomic_bool stop_pending{false};
std::atomic_uint render_calls{0};
bool InsideRender() { return render_calls.load(std::memory_order_acquire) != 0; }
bool Stop();
struct RenderCall { RenderCall() { ++render_calls; } ~RenderCall(); };
On12Graphics::FrameStats Stats() { return retired_valid ? retired_stats : context ? context->Stats() : On12Graphics::FrameStats{}; }
std::vector<Slot> getter_slots, device_slots;
std::wstring report_path, directory;
On12Graphics::TemporalInputProbe temporal_inputs;
DWORD failure = 0, sequence = 0; HRESULT last_result = S_OK;
UINT width = 0, height = 0;
DWORD render_stage = 0, source_present_flags = 0; BOOL source_auto_depth = FALSE;
std::uint64_t presents = 0, resets = 0, errors = 0, original_calls = 0;
std::uint64_t output_calls = 0;
thread_local bool internal = false;
#ifdef S7_ON12_SPATIAL_GPU_FIXTURE
bool (*validate_output)(IDirect3DDevice9*, IDirect3DSurface9*) = nullptr;
bool output_validated = false;
#endif
struct Scope { Scope() { internal = true; } ~Scope() { internal = false; } };

bool Patch(const Slot& s, bool forward) {
    DWORD previous = 0, unused = 0;
    if (!VirtualProtect(s.address, sizeof(void*), PAGE_READWRITE, &previous)) return false;
    void* before = forward ? s.original : s.replacement; void* after = forward ? s.replacement : s.original;
    const bool changed = InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(s.address), after, before) == before;
    const bool restored = VirtualProtect(s.address, sizeof(void*), previous, &unused) != FALSE;
    if (changed && !restored) { InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(s.address), before, after); VirtualProtect(s.address, sizeof(void*), previous, &unused); }
    return changed && restored;
}
void Live() noexcept {
    try {
    if (report_path.empty()) return;
    const auto stats = Stats();
    std::ostringstream text;
    text << "{\"format\":\"s7-on12-spatial-live-v1\",\"failure\":" << failure
        << ",\"initialized\":" << (context ? "true" : "false") << ",\"stopped\":" << (stopped ? "true" : "false") << ",\"enabled\":" << settings.enabled
        << ",\"sequence\":" << sequence << ",\"width\":" << width << ",\"height\":" << height
        << ",\"presents\":" << presents << ",\"original_present_calls\":" << original_calls
        << ",\"output_present_calls\":" << output_calls << ",\"output_width\":" << output_width << ",\"output_height\":" << output_height
        << ",\"resets\":" << resets << ",\"errors\":" << errors << ",\"last_hr\":" << static_cast<long>(last_result)
        << ",\"render_stage\":" << render_stage << ",\"source_present_flags\":" << source_present_flags << ",\"source_auto_depth\":" << (source_auto_depth ? "true" : "false")
        << ",\"scene_only\":" << (scene_mode ? "true" : "false") << ",\"scene_width\":" << render_width << ",\"scene_height\":" << render_height
        << ",\"ui_draw_calls\":" << ui_draw_calls << ",\"composite_draw_calls\":" << composite_draw_calls
        << ",\"native_ui_width\":" << (scene?scene->Stats().ui_width:0) << ",\"native_ui_height\":" << (scene?scene->Stats().ui_height:0)
        << ",\"scene_resources\":" << (scene?scene->Stats().resources:0) << ",\"scene_failure\":" << static_cast<long>(scene?scene->Stats().failure:S_OK)
        << ",\"applied\":" << stats.applied << ",\"busy\":" << stats.busy
        << ",\"checkouts\":" << stats.checkouts << ",\"returns\":" << stats.returns
        << ",\"failed_returns\":" << stats.failed_returns << ",\"safe_for_d3d9\":" << (!context || context->SafeForD3D9() ? "true" : "false") << "}";
    const auto bytes = text.str(); const auto temporary = report_path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD count = 0; const bool okay = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) && count == bytes.size(); CloseHandle(file);
    if (okay) MoveFileExW(temporary.c_str(), report_path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) { /* Optional telemetry must never escape into the game. */ }
}
void Command() {
    struct Value { DWORD size, version, sequence, enabled; } value{};
    HANDLE file = CreateFileW((directory + L"\\on12-spatial.private.command").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{}; BY_HANDLE_FILE_INFORMATION info{}; DWORD count = 0;
    const bool okay = GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        && GetFileSizeEx(file, &size) && size.QuadPart == sizeof(value) && ReadFile(file, &value, sizeof(value), &count, nullptr) && count == sizeof(value);
    CloseHandle(file);
    if (!okay || value.size != sizeof(value) || value.version != 1 || !value.sequence || value.enabled > 1) { ++errors; return; }
    if (value.sequence > sequence) { sequence = value.sequence; settings.enabled = value.enabled; }
}

// Explicit, bounded, read-only observation. V1 is metadata only. V2 explicitly
// requests one synchronizing R32F readback; never use V2 for FPS measurements.
void TemporalInputCommand() noexcept {
    if(!scene_mode)return;
    try {
    Guard guard;
    HANDLE file=CreateFileW((directory+L"\\on12-temporal-inputs.private.command").c_str(),GENERIC_READ,
        FILE_SHARE_READ|FILE_SHARE_WRITE|FILE_SHARE_DELETE,nullptr,OPEN_EXISTING,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    std::array<DWORD,6> words{};LARGE_INTEGER size{};BY_HANDLE_FILE_INFORMATION info{};DWORD count=0;
    const bool valid=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
        &&GetFileSizeEx(file,&size)&&(size.QuadPart==16||size.QuadPart==24)&&ReadFile(file,words.data(),static_cast<DWORD>(size.QuadPart),&count,nullptr)&&count==size.QuadPart;
    CloseHandle(file);
    if(valid&&words[2]!=temporal_inputs.Sequence()){
        if(size.QuadPart==16){On12Graphics::TemporalInputCommand value;std::memcpy(&value,words.data(),sizeof(value));temporal_inputs.Arm(value);}
        else{On12Graphics::TemporalInputDepthCommand value;std::memcpy(&value,words.data(),sizeof(value));temporal_inputs.Arm(value);}
    }
    }catch(...){temporal_inputs.Invalidate();}
}
void TemporalDepthCapture(){
    auto result=temporal_inputs.Depth();
    ComPtr<IDirect3DBaseTexture9> base;ComPtr<IDirect3DTexture9> texture;
    auto* native=context->NativeDevice();result.result=native->GetTexture(0,&base);
    if(SUCCEEDED(result.result))result.result=base?base.As(&texture):D3DERR_NOTFOUND;
    On12Graphics::R32DepthCapture pixels;
    if(SUCCEEDED(result.result))result.result=On12Graphics::ReadR32Depth(native,texture.Get(),pixels);
    if(SUCCEEDED(result.result)){
        result.width=pixels.width;result.height=pixels.height;result.finite=pixels.finite;result.interior=pixels.interior;result.minimum=pixels.minimum;result.maximum=pixels.maximum;
        On12Graphics::TemporalDepthHeader header;header.width=pixels.width;header.height=pixels.height;header.frame=result.frame;header.normalized_depth_scale=result.scale;
        const auto path=directory+L"\\on12-temporal-depth-"+std::to_wstring(temporal_inputs.Sequence())+L".private.bin";
        const bool parents=Parents(path.c_str());
        HANDLE file=parents?CreateFileW(path.c_str(),GENERIC_WRITE,0,nullptr,CREATE_NEW,FILE_FLAG_OPEN_REPARSE_POINT,nullptr):INVALID_HANDLE_VALUE;
        if(file==INVALID_HANDLE_VALUE)result.result=parents?HRESULT_FROM_WIN32(GetLastError()):E_ACCESSDENIED;
        else{DWORD bytes=0;const DWORD count=static_cast<DWORD>(pixels.pixels.size()*sizeof(float));
            const bool written=WriteFile(file,&header,sizeof(header),&bytes,nullptr)&&bytes==sizeof(header)&&WriteFile(file,pixels.pixels.data(),count,&bytes,nullptr)&&bytes==count;
            const DWORD error=GetLastError();CloseHandle(file);result.written=written;if(!written)result.result=HRESULT_FROM_WIN32(error?error:ERROR_WRITE_FAULT);
        }
    }
    temporal_inputs.DepthResult(result);
}
void TemporalInputCapture(IDirect3DDevice9* d,bool ui) noexcept {
    if(!temporal_inputs.Active())return;
    Guard guard;
    try {
        auto read=[&]{On12Graphics::TemporalBoundaryInputs value;value.logical=On12Graphics::ReadTemporalDeviceInputs(d);
            value.physical=On12Graphics::ReadTemporalDeviceInputs(context->NativeDevice());return value;};
        if(!ui){if(temporal_inputs.Composite(presents+1,read)&&temporal_inputs.BeginDepth(presents+1))TemporalDepthCapture();return;}
        if(!temporal_inputs.Ui(presents+1,read))return;
        std::ostringstream out;temporal_inputs.Json(out);const auto bytes=out.str();
        const auto path=directory+L"\\on12-temporal-inputs.private.json",temporary=path+L".tmp";
        if(!Parents(temporary.c_str()))return;
        HANDLE file=CreateFileW(temporary.c_str(),GENERIC_WRITE,0,nullptr,CREATE_ALWAYS,FILE_FLAG_OPEN_REPARSE_POINT,nullptr);
        if(file==INVALID_HANDLE_VALUE)return;
        BY_HANDLE_FILE_INFORMATION info{};DWORD count=0;
        const bool written=GetFileInformationByHandle(file,&info)&&!(info.dwFileAttributes&FILE_ATTRIBUTE_REPARSE_POINT)
            &&bytes.size()<128*1024&&WriteFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)&&count==bytes.size();CloseHandle(file);
        if(written)MoveFileExW(temporary.c_str(),path.c_str(),MOVEFILE_REPLACE_EXISTING);
    }catch(...){temporal_inputs.Invalidate();}
}
template<class T> T SceneOriginal(UINT slot){return reinterpret_cast<T>(scene_original[slot]);}
bool SceneOwn(IDirect3DDevice9* d){return scene&&d==admitted&&!internal&&!stopped&&GetCurrentThreadId()==owner;}
template<class Call> HRESULT SceneAccess(Call call){Guard guard;Scope scope;try{return call();}catch(...){failure=403;return E_OUTOFMEMORY;}}
HRESULT STDMETHODCALLTYPE SceneSetTarget(IDirect3DDevice9* d,DWORD i,IDirect3DSurface9* s){return SceneOwn(d)?SceneAccess([&]{return scene->SetTarget(i,s);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*)>(37)(d,i,s);}
HRESULT STDMETHODCALLTYPE SceneGetTarget(IDirect3DDevice9* d,DWORD i,IDirect3DSurface9** s){return SceneOwn(d)?SceneAccess([&]{return scene->GetTarget(i,s);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9**)>(38)(d,i,s);}
HRESULT STDMETHODCALLTYPE SceneSetDepth(IDirect3DDevice9* d,IDirect3DSurface9* s){return SceneOwn(d)?SceneAccess([&]{return scene->SetDepth(s);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*)>(39)(d,s);}
HRESULT STDMETHODCALLTYPE SceneGetDepth(IDirect3DDevice9* d,IDirect3DSurface9** s){return SceneOwn(d)?SceneAccess([&]{return scene->GetDepth(s);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9**)>(40)(d,s);}
HRESULT STDMETHODCALLTYPE SceneSetViewport(IDirect3DDevice9* d,const D3DVIEWPORT9* v){return SceneOwn(d)?SceneAccess([&]{return scene->SetViewport(v);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const D3DVIEWPORT9*)>(47)(d,v);}
HRESULT STDMETHODCALLTYPE SceneGetViewport(IDirect3DDevice9* d,D3DVIEWPORT9* v){return SceneOwn(d)?SceneAccess([&]{return scene->GetViewport(v);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DVIEWPORT9*)>(48)(d,v);}
HRESULT STDMETHODCALLTYPE SceneSetTexture(IDirect3DDevice9* d,DWORD i,IDirect3DBaseTexture9* t){return SceneOwn(d)&&scene->MapsTexture(t)?SceneAccess([&]{return scene->SetTexture(i,t);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9*)>(65)(d,i,t);}
HRESULT STDMETHODCALLTYPE SceneGetTexture(IDirect3DDevice9* d,DWORD i,IDirect3DBaseTexture9** t){return SceneOwn(d)?SceneAccess([&]{return scene->GetTexture(i,t);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DBaseTexture9**)>(64)(d,i,t);}
HRESULT STDMETHODCALLTYPE SceneSetScissor(IDirect3DDevice9* d,const RECT* r){return SceneOwn(d)?SceneAccess([&]{return scene->SetScissor(r);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*)>(75)(d,r);}
HRESULT STDMETHODCALLTYPE SceneGetScissor(IDirect3DDevice9* d,RECT* r){return SceneOwn(d)?SceneAccess([&]{return scene->GetScissor(r);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,RECT*)>(76)(d,r);}
HRESULT STDMETHODCALLTYPE SceneStretch(IDirect3DDevice9* d,IDirect3DSurface9* s,const RECT* sr,IDirect3DSurface9* t,const RECT* tr,D3DTEXTUREFILTERTYPE f){return SceneOwn(d)?SceneAccess([&]{return scene->Stretch(s,sr,t,tr,f);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,IDirect3DSurface9*,const RECT*,D3DTEXTUREFILTERTYPE)>(34)(d,s,sr,t,tr,f);}
HRESULT STDMETHODCALLTYPE SceneFill(IDirect3DDevice9* d,IDirect3DSurface9* s,const RECT* r,D3DCOLOR c){return SceneOwn(d)?SceneAccess([&]{return scene->Fill(s,r,c);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DSurface9*,const RECT*,D3DCOLOR)>(35)(d,s,r,c);}
HRESULT STDMETHODCALLTYPE SceneClear(IDirect3DDevice9* d,DWORD n,const D3DRECT* r,DWORD flags,D3DCOLOR c,float z,DWORD stencil){return SceneOwn(d)?SceneAccess([&]{return scene->Clear(n,r,flags,c,z,stencil);}):SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,const D3DRECT*,DWORD,D3DCOLOR,float,DWORD)>(43)(d,n,r,flags,c,z,stencil);}
HRESULT STDMETHODCALLTYPE SceneVertex(IDirect3DDevice9* d,IDirect3DVertexShader9* shader){const HRESULT hr=SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DVertexShader9*)>(92)(d,shader);if(SUCCEEDED(hr)&&SceneOwn(d)&&!boundary->CachedVertex(shader))SceneAccess([&]{boundary->Vertex(shader);return S_OK;});return hr;}
HRESULT STDMETHODCALLTYPE ScenePixel(IDirect3DDevice9* d,IDirect3DPixelShader9* shader){const HRESULT hr=SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DPixelShader9*)>(107)(d,shader);if(SUCCEEDED(hr)&&SceneOwn(d)&&!boundary->CachedPixel(shader))SceneAccess([&]{boundary->Pixel(shader);return S_OK;});return hr;}
HRESULT SceneBeforeDraw(IDirect3DDevice9* d){
    if(!SceneOwn(d)||!scene->Active()||scene->Suspended())return S_OK;
    if(!scene->Safe()||!context->SafeForD3D9())return D3DERR_DEVICELOST;
    const auto role=boundary->Current();
    if(role==On12Graphics::SceneBoundary::Role::Composite){TemporalInputCapture(d,false);return S_OK;}
    if(role!=On12Graphics::SceneBoundary::Role::Ui)return S_OK;
    if(scene->Ui()){++ui_draw_calls;return S_OK;}
    const HRESULT result=SceneAccess([&]{if(!boundary->NativeUi(d,output_width,output_height)){failure=404;return D3DERR_INVALIDCALL;}
        ++ui_draw_calls;auto quality=settings;quality.enabled=1;quality.antialiasing=settings.enabled?1.0f:0.0f;
        HRESULT hr=scene->BeginUi(quality);if(FAILED(hr)){last_result=hr;failure=405;}return hr;});
    // Outside SceneAccess's internal scope so wrapped getters retain the
    // engine's virtual native view. Physical getters use the raw On12 device.
    if(SUCCEEDED(result))TemporalInputCapture(d,true);return result;
}
HRESULT STDMETHODCALLTYPE SceneDraw(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT first,UINT count){const HRESULT before=SceneBeforeDraw(d);if(FAILED(before))return before;return SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT)>(81)(d,t,first,count);}
HRESULT STDMETHODCALLTYPE SceneDrawIndexed(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,INT base,UINT minimum,UINT vertices,UINT first,UINT count){const HRESULT before=SceneBeforeDraw(d);if(FAILED(before))return before;
    const HRESULT hr=SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT)>(82)(d,t,base,minimum,vertices,first,count);
    if(SUCCEEDED(hr)&&SceneOwn(d)&&boundary->Current()==On12Graphics::SceneBoundary::Role::Composite)++composite_draw_calls;return hr;}
HRESULT STDMETHODCALLTYPE SceneDrawUp(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT n,const void* data,UINT stride){const HRESULT before=SceneBeforeDraw(d);if(FAILED(before))return before;return SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,const void*,UINT)>(83)(d,t,n,data,stride);}
HRESULT STDMETHODCALLTYPE SceneDrawIndexedUp(IDirect3DDevice9* d,D3DPRIMITIVETYPE t,UINT minimum,UINT vertices,UINT n,const void* indices,D3DFORMAT format,const void* data,UINT stride){const HRESULT before=SceneBeforeDraw(d);if(FAILED(before))return before;return SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT,UINT,const void*,D3DFORMAT,const void*,UINT)>(84)(d,t,minimum,vertices,n,indices,format,data,stride);}
using BlockCall=HRESULT(STDMETHODCALLTYPE*)(IDirect3DStateBlock9*);
BlockCall block_capture=nullptr,block_apply=nullptr;
bool SceneBlockOwn(IDirect3DStateBlock9* block){if(!scene||internal||stopped||GetCurrentThreadId()!=owner)return false;
    Guard guard;ComPtr<IDirect3DDevice9> device;return SUCCEEDED(block->GetDevice(&device))&&device.Get()==admitted;}
HRESULT STDMETHODCALLTYPE SceneCapture(IDirect3DStateBlock9* block){
    if(!SceneBlockOwn(block))return block_capture(block);
    HRESULT before=SceneAccess([&]{return scene->BeforeCapture();});if(FAILED(before))return before;
    const HRESULT hr=block_capture(block);HRESULT after=SceneAccess([&]{return scene->AfterCapture();});return FAILED(after)?after:hr;
}
HRESULT STDMETHODCALLTYPE SceneApply(IDirect3DStateBlock9* block){const HRESULT hr=block_apply(block);
    if(SUCCEEDED(hr)&&SceneBlockOwn(block)){HRESULT after=SceneAccess([&]{boundary->Refresh(admitted);return scene->AfterApply();});if(FAILED(after))return after;}return hr;}
HRESULT STDMETHODCALLTYPE SceneBeginBlock(IDirect3DDevice9* d){
    using Call=HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*);if(!SceneOwn(d)||!scene->Active())return SceneOriginal<Call>(60)(d);
    HRESULT hr=SceneAccess([&]{return scene->BeforeCapture();});if(FAILED(hr))return hr;const HRESULT original=SceneOriginal<Call>(60)(d);
    if(FAILED(original))SceneAccess([&]{return scene->AfterCapture();});return original;
}
HRESULT STDMETHODCALLTYPE SceneEndBlock(IDirect3DDevice9* d,IDirect3DStateBlock9** block){
    const HRESULT original=SceneOriginal<HRESULT(STDMETHODCALLTYPE*)(IDirect3DDevice9*,IDirect3DStateBlock9**)>(61)(d,block);
    if(SceneOwn(d)&&scene->Suspended()){HRESULT after=SceneAccess([&]{boundary->Refresh(d);return scene->AfterCapture();});if(FAILED(after))return after;}return original;
}
HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* d, const RECT* a, const RECT* b, HWND window, const RGNDATA* region) {
    RenderCall render_call; bool use_output = false;
    if (!internal && d == admitted && GetCurrentThreadId() == owner) {
        {
            Guard guard; Scope scope; ++presents;
            try {
                if (presents == 1 || presents % 60 == 0) {Command();TemporalInputCommand();}
                if(scene_mode&&scene&&!stopped&&!failure){
                    if(scene->Active()&&!scene->Ui()){auto quality=settings;quality.enabled=1;quality.antialiasing=settings.enabled?1.0f:0.0f;last_result=scene->BeginUi(quality);if(FAILED(last_result))failure=406;}
                    width=output_width;height=output_height;
                } else if (!scene_mode && context && !stopped && !failure && (settings.enabled || output_width)) {
                    ComPtr<IDirect3DSurface9> backbuffer;
                    render_stage = 1;
                    last_result = context->NativeDevice()->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
                    if (SUCCEEDED(last_result)) {
                        render_stage = 2;
                        D3DSURFACE_DESC desc{}; last_result = backbuffer->GetDesc(&desc);
                        if (SUCCEEDED(last_result)) {
                            width = desc.Width; height = desc.Height;
                            if (output_width) {
                                if (a || b || window || region || width != render_width || height != render_height) last_result = E_INVALIDARG;
                                else if (output_window.Ready()) {
                                    if (!output_chain) {
                                        ComPtr<IDirect3DSwapChain9> original; D3DPRESENT_PARAMETERS source_pp{}, pp{};
                                        render_stage = 3;
                                        last_result = context->NativeDevice()->GetSwapChain(0, &original);
                                        if (SUCCEEDED(last_result)) { render_stage = 4; last_result = original->GetPresentParameters(&source_pp); }
                                        if (SUCCEEDED(last_result)) {
                                            source_present_flags = source_pp.Flags; source_auto_depth = source_pp.EnableAutoDepthStencil;
                                            // The additional color-only chain must not inherit primary
                                            // depth-stencil/discard/fullscreen flags from the game.
                                            pp.Windowed = TRUE; pp.hDeviceWindow = source_pp.hDeviceWindow; pp.BackBufferFormat = desc.Format;
                                            pp.BackBufferWidth = output_width; pp.BackBufferHeight = output_height; pp.BackBufferCount = 1;
                                            pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
                                            render_stage = 5;
                                            last_result = context->NativeDevice()->CreateAdditionalSwapChain(&pp, &output_chain);
                                        }
                                    }
                                    if (SUCCEEDED(last_result) && output_chain) {
                                        render_stage = 6;
                                        ComPtr<IDirect3DSurface9> target; last_result = output_chain->GetBackBuffer(0, D3DBACKBUFFER_TYPE_MONO, &target);
                                        auto pass_settings = settings; pass_settings.enabled = 1; pass_settings.antialiasing = settings.enabled ? 1.0f : 0.0f;
                                        if (SUCCEEDED(last_result)) { render_stage = 7; last_result = context->Render(backbuffer.Get(), target.Get(), pass_settings); }
                                        use_output = last_result == S_OK;
#ifdef S7_ON12_SPATIAL_GPU_FIXTURE
                                        if (use_output && validate_output) {
                                            output_validated = validate_output(context->NativeDevice(), target.Get());
                                            if (!output_validated) { last_result = E_FAIL; use_output = false; }
                                            // Reproduce engine teardown requested while Present is
                                            // still active. Release must happen after that call.
                                            else Stop();
                                        }
#endif
                                    }
                                }
                            } else last_result = context->Render(backbuffer.Get(), backbuffer.Get(), settings);
                        }
                    }
                    if (FAILED(last_result)) { ++errors; failure = 301; }
                }
            } catch (...) { ++errors; failure = 302; }
            // Never let D3D9 touch a resource still checked out after a device
            // or fence failure. A safe optional-pass failure retains Present.
            if (context && !context->SafeForD3D9()) { Live(); return D3DERR_DEVICELOST; }
            if (use_output) ++output_calls; else ++original_calls;
            if (presents == 1 || presents % 60 == 0 || failure) Live();
        }
    }
    // Exactly one presentation per frame, including fallback when the native
    // queue's optional slot is busy. Never present a stale output texture.
    const HRESULT presented=use_output?output_chain->Present(nullptr,nullptr,nullptr,nullptr,0):original_present(d,a,b,window,region);
    if(SUCCEEDED(presented)&&SceneOwn(d)&&!failure)SceneAccess([&]{last_result=scene->BeginFrame();if(FAILED(last_result))failure=407;return last_result;});
    return presented;
}
HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* parameters) {
    RenderCall render_call;
    if (!internal && d == admitted) {
        Guard guard; Scope scope;
        if (GetCurrentThreadId() != owner) return D3DERR_INVALIDCALL;
        if (context) { const HRESULT drained = context->Drain(5000); if (FAILED(drained)) { ++errors; last_result = drained; Live(); return D3DERR_DEVICELOST; } }
        temporal_inputs.Invalidate();
        if(scene){HRESULT released=scene->ReleaseForReset();if(FAILED(released))return D3DERR_DEVICELOST;boundary->Reset();}
        DropOutput();
        ++resets;
    }
    const HRESULT result=original_reset(d,parameters);
    if(SUCCEEDED(result)&&SceneOwn(d))SceneAccess([&]{scene->ResetSucceeded();boundary->Refresh(d);failure=0;last_result=S_OK;return S_OK;});
    return result;
}
bool Stop() {
    if (stopped || !context) return true;
    if (InsideRender() || GetCurrentThreadId() != owner || stopping) { stop_pending.store(true, std::memory_order_release); return true; }
    struct Stopping { Stopping() { stopping = true; } ~Stopping() { stopping = false; } } stopping_scope;
    if (FAILED(context->Drain(5000))) { failure = 501; Live(); return false; }
    retired_stats = context->Stats(); retired_valid = true;
    if(scene&&FAILED(scene->Shutdown())){failure=504;Live();return false;}
    temporal_inputs.Invalidate();if(boundary)boundary->Reset();
    DropOutput();
    if (context && FAILED(context->Shutdown(5000))) { failure = 502; Live(); return false; }
    if (output_width&&!scene_mode && FAILED(output_window.Shutdown())) { failure = 503; Live(); return false; }
    stopped = true; stop_pending.store(false, std::memory_order_release); Live(); return true;
}
RenderCall::~RenderCall() {
    if (--render_calls == 0 && stop_pending.load(std::memory_order_acquire) && GetCurrentThreadId() == owner) {
        Guard guard; Stop(); // Preserve the just-returned original API's state.
    }
}
void __cdecl HookDeinitD3D() { { Guard guard; if (!Stop()) return; } original_deinit_d3d(); }
bool __cdecl HookDeinitScreen(void* config) { { Guard guard; if (!Stop()) return false; } return original_deinit_screen(config); }
HRESULT Attach(IDirect3DDevice9* device) {
    if (!device || admitted || failure || !Range(device, sizeof(void*))) return E_UNEXPECTED;
    auto** table = *reinterpret_cast<void***>(device);
    if (!Range(table, 119 * sizeof(void*)) || !Belongs(table, wrapper)) return E_NOINTERFACE;
    // The water snapshot must have detached first. No chaining an unknown
    // hook, foreign renderer, modified table, or unqualified apphelp shim.
    for (size_t i = 0; i < 119; ++i) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!table[i] || VirtualQuery(table[i], &memory, sizeof(memory)) != sizeof(memory) || memory.Type != MEM_IMAGE || memory.State != MEM_COMMIT
            || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !(memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
            || (memory.AllocationBase != wrapper && !(apphelp && memory.AllocationBase == apphelp && (i == 0 || i == 2 || i == 13 || i == 16)))) return E_NOINTERFACE;
    }
    if (!Module(GetModuleHandleW(L"d3d9on12.dll"), "24aceac4aa280d09f1ec2c4c9a2b3d784bf158fe1f0f23292ab1072db9187364")
        && !Module(GetModuleHandleW(L"d3d9on12.dll"), "8d79e366f97ea0236e57b3d597626c7072b67adc182a9780548e9e3926aaff9a")) return E_NOINTERFACE;
    ComPtr<ID3DBlob> vs, ps, compiler_error;
    constexpr UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(On12SpatialShader, sizeof(On12SpatialShader) - 1, "on12_spatial_pass.hlsl", nullptr, nullptr, "VS", "vs_5_0", flags, 0, &vs, &compiler_error);
    if (FAILED(hr)) return hr;
    compiler_error.Reset(); hr = D3DCompile(On12SpatialShader, sizeof(On12SpatialShader) - 1, "on12_spatial_pass.hlsl", nullptr, nullptr, "PS", "ps_5_0", flags, 0, &ps, &compiler_error);
    if (FAILED(hr)) return hr;
    auto candidate = std::unique_ptr<On12Graphics::FrameContext>(new (std::nothrow) On12Graphics::FrameContext); if (!candidate) return E_OUTOFMEMORY;
    hr = candidate->Initialize(device, vs->GetBufferPointer(), vs->GetBufferSize(), ps->GetBufferPointer(), ps->GetBufferSize()); if (FAILED(hr)) return hr;
    device_slots.reserve(24);
    std::unique_ptr<On12Graphics::SceneTargets> next_scene;
    std::unique_ptr<On12Graphics::SceneBoundary> next_boundary;
    if(scene_mode){
        std::memcpy(scene_original.data(),table,sizeof(scene_original));
        next_scene=std::make_unique<On12Graphics::SceneTargets>();next_boundary=std::make_unique<On12Graphics::SceneBoundary>();
        hr=next_scene->Initialize(device,candidate.get(),scene_original,render_width,render_height,output_width,output_height);if(FAILED(hr))return hr;
        next_boundary->Refresh(device);
    }
    if (output_width&&!scene_mode) {
        ComPtr<IDirect3DSwapChain9> chain; D3DPRESENT_PARAMETERS pp{};
        hr = candidate->NativeDevice()->GetSwapChain(0, &chain); if (FAILED(hr)) return hr;
        hr = chain->GetPresentParameters(&pp); if (FAILED(hr)) return hr;
        if (!pp.Windowed || pp.BackBufferWidth != render_width || pp.BackBufferHeight != render_height) return E_INVALIDARG;
        HMODULE modules[]{GetModuleHandleW(nullptr), GetModuleHandleW(L"vbase90.dll"), GetModuleHandleW(L"vision90.dll")};
        hr = output_window.Initialize(pp.hDeviceWindow, render_width, render_height, output_width, output_height, modules,
#ifdef S7_ON12_SPATIAL_GPU_FIXTURE
            1,
#else
            3,
#endif
            InsideRender);
#ifdef S7_ON12_SPATIAL_GPU_FIXTURE
        if (FAILED(hr)) std::fprintf(stderr, "output window initialize=%08lx\n", hr);
#endif
        if (FAILED(hr)) return hr;
    }
    device_slots = {{&table[17], table[17], reinterpret_cast<void*>(HookPresent)}, {&table[16], table[16], reinterpret_cast<void*>(HookReset)}};
    if(scene_mode){
        const std::pair<UINT,void*> hooks[]{
            {34,reinterpret_cast<void*>(SceneStretch)},{35,reinterpret_cast<void*>(SceneFill)},
            {37,reinterpret_cast<void*>(SceneSetTarget)},{38,reinterpret_cast<void*>(SceneGetTarget)},
            {39,reinterpret_cast<void*>(SceneSetDepth)},{40,reinterpret_cast<void*>(SceneGetDepth)},
            {43,reinterpret_cast<void*>(SceneClear)},{47,reinterpret_cast<void*>(SceneSetViewport)},{48,reinterpret_cast<void*>(SceneGetViewport)},
            {60,reinterpret_cast<void*>(SceneBeginBlock)},{61,reinterpret_cast<void*>(SceneEndBlock)},
            {65,reinterpret_cast<void*>(SceneSetTexture)},{64,reinterpret_cast<void*>(SceneGetTexture)},
            {75,reinterpret_cast<void*>(SceneSetScissor)},{76,reinterpret_cast<void*>(SceneGetScissor)},
            {81,reinterpret_cast<void*>(SceneDraw)},{82,reinterpret_cast<void*>(SceneDrawIndexed)},
            {83,reinterpret_cast<void*>(SceneDrawUp)},{84,reinterpret_cast<void*>(SceneDrawIndexedUp)},
            {92,reinterpret_cast<void*>(SceneVertex)},{107,reinterpret_cast<void*>(ScenePixel)}};
        for(const auto& hook:hooks)device_slots.push_back({&table[hook.first],table[hook.first],hook.second});
        ComPtr<IDirect3DStateBlock9> block;hr=device->CreateStateBlock(D3DSBT_ALL,&block);if(FAILED(hr))return hr;
        auto** block_table=*reinterpret_cast<void***>(block.Get());
        if(!Range(block_table,6*sizeof(void*))||!Belongs(block_table,wrapper))return E_NOINTERFACE;
        for(UINT i=0;i<6;++i)if(!Belongs(block_table[i],wrapper))return E_NOINTERFACE;
        block_capture=reinterpret_cast<BlockCall>(block_table[4]);block_apply=reinterpret_cast<BlockCall>(block_table[5]);
        device_slots.push_back({&block_table[4],block_table[4],reinterpret_cast<void*>(SceneCapture)});
        device_slots.push_back({&block_table[5],block_table[5],reinterpret_cast<void*>(SceneApply)});
    }
    original_present = reinterpret_cast<Present>(table[17]); original_reset = reinterpret_cast<Reset>(table[16]);
    size_t patched = 0;
    for (const auto& slot : device_slots) { if (!Patch(slot, true)) { while (patched) Patch(device_slots[--patched], false); device_slots.clear(); if (output_width&&!scene_mode) output_window.Shutdown(); return E_FAIL; } ++patched; }
    admitted = device; owner = GetCurrentThreadId(); context = candidate.release(); settings.enabled = 1;
    scene=next_scene.release();boundary=next_boundary.release();
    for (auto it = getter_slots.rbegin(); it != getter_slots.rend(); ++it) if (!Patch(*it, false)) { failure = 203; return E_FAIL; }
    Live(); return S_OK;
}
IDirect3DDevice9* __cdecl HookGetter() {
    IDirect3DDevice9* device = original_getter();
    if (!internal && !admitted && !failure) {
        Guard guard; Scope scope;
        try { last_result = Attach(device); if (FAILED(last_result)) failure = 201; Live(); }
        catch (...) { failure = 202; Live(); }
    }
    return device;
}
bool Import(HMODULE module, std::vector<Slot>& slots, const char* requested, void* original, void* replacement) {
    auto* image = reinterpret_cast<unsigned char*>(module); const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if (!Range(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 || dos->e_lfanew > 4096) return false;
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (!Range(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    const DWORD size = nt->OptionalHeader.SizeOfImage;
    auto valid = [&](DWORD rva, size_t bytes) { return rva < size && bytes <= size - rva && Range(image + rva, bytes); };
    const auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; if (!valid(imports.VirtualAddress, imports.Size)) return false;
    auto* entries = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + imports.VirtualAddress);
    for (size_t n = 0; n < imports.Size / sizeof(*entries) && entries[n].Name; ++n) {
        const auto& e = entries[n]; if (!valid(e.Name, 12)) return false;
        if (_stricmp(reinterpret_cast<char*>(image + e.Name), "vbase90.dll")) continue;
        if (!e.OriginalFirstThunk) return false;
        for (DWORD i = 0; ; ++i) {
            const DWORD offset = i * sizeof(IMAGE_THUNK_DATA32);
            if (!valid(e.OriginalFirstThunk + offset, 4) || !valid(e.FirstThunk + offset, 4)) return false;
            const DWORD thunk = reinterpret_cast<IMAGE_THUNK_DATA32*>(image + e.OriginalFirstThunk + offset)->u1.AddressOfData;
            if (!thunk) break; if (IMAGE_SNAP_BY_ORDINAL32(thunk)) continue;
            if (!valid(thunk, sizeof(WORD) + strlen(requested) + 1)) return false;
            const auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image + thunk);
            if (std::memcmp(name->Name, requested, strlen(requested) + 1)) continue;
            auto** slot = reinterpret_cast<void**>(image + e.FirstThunk + offset);
            if (*slot != original) return false;
            slots.push_back({slot, *slot, replacement});
        }
    }
    return true;
}
}

extern "C" DWORD WINAPI WePlayOn12SpatialInitialize(void* opaque) {
    Guard guard;
    try {
        Config c{}; SIZE_T read = 0;
        if (!opaque || !ReadProcessMemory(GetCurrentProcess(), opaque, &c, sizeof(c), &read) || read != sizeof(c)
            || c.size != sizeof(c) || c.version != 1 || c.flags != 1) return 0;
        const size_t length = wcsnlen_s(c.report, 512); constexpr wchar_t suffix[] = L"\\on12-spatial-live.private.json";
        if (length < 40 || length >= 512 || c.report[1] != L':' || c.report[2] != L'\\'
            || std::wstring(c.report).find(L"\\..") != std::wstring::npos || std::wstring(c.report).find(L"\\.\\") != std::wstring::npos
            || length < wcslen(suffix) || wcscmp(c.report + length - wcslen(suffix), suffix) || !Parents(c.report)
            || GetFileAttributesW(c.report) != INVALID_FILE_ATTRIBUTES) return 101;
        if (original_getter || !Module(GetModuleHandleW(nullptr), Game) || !Module(GetModuleHandleW(L"vbase90.dll"), Base)
            || !Module(GetModuleHandleW(L"vision90.dll"), Vision)) return 102;
        wchar_t image[512]{}; const DWORD count = GetModuleFileNameW(nullptr, image, 512); if (!count || count >= 512) return 103;
        const auto split = std::wstring(image).find_last_of(L'\\'); if (split == std::wstring::npos) return 103;
        const auto bin = std::wstring(image).substr(0, split);
        wrapper = GetModuleHandleW((bin + L"\\dxwrapper.dll").c_str());
        if (!Module(wrapper, Wrapper) || !Module(GetModuleHandleW((bin + L"\\d3d9.dll").c_str()), Stub)) return 104;
        apphelp = GetModuleHandleW(L"apphelp.dll");
        if (apphelp && !Module(apphelp, "34655b97cbcaa5b93733da1d9118e0663735c442eb0016671a98389f3cde863e")) return 105;
        original_getter = reinterpret_cast<Getter>(GetProcAddress(GetModuleHandleW(L"vbase90.dll"), GetterName)); if (!original_getter) return 106;
        std::vector<Slot> slots;
        if (!Import(GetModuleHandleW(nullptr), slots, GetterName, reinterpret_cast<void*>(original_getter), reinterpret_cast<void*>(HookGetter))
            || !Import(GetModuleHandleW(L"vision90.dll"), slots, GetterName, reinterpret_cast<void*>(original_getter), reinterpret_cast<void*>(HookGetter)) || slots.empty()) return 107;
        getter_slots = slots;
        constexpr char deinit_d3d[] = "?DeInitializeD3D@VVideo@@SAXXZ";
        constexpr char deinit_screen[] = "?DeInitializeScreen@VVideo@@SA_NAAVVVideoConfig@@@Z";
        original_deinit_d3d = reinterpret_cast<DeinitD3D>(GetProcAddress(GetModuleHandleW(L"vbase90.dll"), deinit_d3d));
        original_deinit_screen = reinterpret_cast<DeinitScreen>(GetProcAddress(GetModuleHandleW(L"vbase90.dll"), deinit_screen));
        if (!original_deinit_d3d || !original_deinit_screen
            || !Import(GetModuleHandleW(L"vision90.dll"), slots, deinit_d3d, reinterpret_cast<void*>(original_deinit_d3d), reinterpret_cast<void*>(HookDeinitD3D))
            || !Import(GetModuleHandleW(L"vision90.dll"), slots, deinit_screen, reinterpret_cast<void*>(original_deinit_screen), reinterpret_cast<void*>(HookDeinitScreen))) return 111;
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<const wchar_t*>(WePlayOn12SpatialInitialize), &self)) return 108;
        report_path = c.report; directory = report_path.substr(0, report_path.find_last_of(L'\\'));
        size_t patched = 0;
        for (const auto& slot : slots) { if (!Patch(slot, true)) { while (patched) Patch(slots[--patched], false); return 109; } ++patched; }
        return 1; // Only the owner thread publishes live renderer state.
    } catch (...) { return 110; }
}

extern "C" DWORD WINAPI WePlayOn12UpscaleInitialize(void* opaque) {
    Guard guard; UpscaleConfig c{}; SIZE_T read = 0;
    if (original_getter || !opaque || !ReadProcessMemory(GetCurrentProcess(), opaque, &c, sizeof(c), &read) || read != sizeof(c)
        || c.size != sizeof(c) || c.version != 2 || c.flags != 2 || c.sw < 640 || c.sh < 360 || c.ow <= c.sw || c.oh <= c.sh
        || c.ow > 4096 || c.oh > 4096 || c.ow > c.sw * 2 || c.oh > c.sh * 2
        || static_cast<UINT64>(c.sw) * c.oh != static_cast<UINT64>(c.sh) * c.ow) return 0;
    render_width = c.sw; render_height = c.sh; output_width = c.ow; output_height = c.oh;
    Config legacy{sizeof(Config), 1, 1, {}}; memcpy(legacy.report, c.report, sizeof(c.report));
    return WePlayOn12SpatialInitialize(&legacy);
}

extern "C" DWORD WINAPI WePlayOn12SceneInitialize(void* opaque) {
    Guard guard; UpscaleConfig c{}; SIZE_T read=0;
    if(original_getter||!opaque||!ReadProcessMemory(GetCurrentProcess(),opaque,&c,sizeof(c),&read)||read!=sizeof(c)
        ||c.size!=sizeof(c)||c.version!=3||c.flags!=3||c.sw<640||c.sh<360||c.ow<=c.sw||c.oh<=c.sh
        ||c.ow>4096||c.oh>4096||c.ow>c.sw*2||c.oh>c.sh*2||static_cast<UINT64>(c.sw)*c.oh!=static_cast<UINT64>(c.sh)*c.ow)return 0;
    scene_mode=true;render_width=c.sw;render_height=c.sh;output_width=c.ow;output_height=c.oh;
    Config legacy{sizeof(Config),1,1,{}};memcpy(legacy.report,c.report,sizeof(c.report));return WePlayOn12SpatialInitialize(&legacy);
}

#ifdef S7_ON12_SPATIAL_GPU_FIXTURE
extern "C" HRESULT On12SpatialFixtureAttach(IDirect3DDevice9* device) {
    Guard guard; wrapper = GetModuleHandleW(L"dxwrapper.dll");
    if (!Module(wrapper, Wrapper)) return E_NOINTERFACE;
    apphelp = GetModuleHandleW(L"apphelp.dll"); return Attach(device);
}
extern "C" bool On12SpatialFixtureClose() {
    Guard guard; const UINT64 expected_presents=scene_mode?2u:1u;
    if(scene_mode)std::fprintf(stderr,"scene close: presents=%llu original=%llu output=%llu resets=%llu failure=%lu errors=%llu applied=%llu ui_calls=%llu upscaled=%llu ui_width=%u scene_hr=%08lx\n",
        presents,original_calls,output_calls,resets,failure,errors,Stats().applied,ui_draw_calls,scene?scene->Stats().upscaled:0,scene?scene->Stats().ui_width:0,scene?scene->Stats().failure:E_FAIL);
    if (!context || presents != expected_presents || original_calls + output_calls != expected_presents || resets != 1 || failure || errors || Stats().applied != 1
        || (output_width&&!scene_mode && (output_calls != 1 || !output_validated))
        || (scene_mode&&(!scene||scene->Stats().upscaled!=1||scene->Stats().ui_width!=32||!ui_draw_calls))) return false;
    if (!Stop()) return false;
    for (auto it = device_slots.rbegin(); it != device_slots.rend(); ++it) if (!Patch(*it, false)) return false;
    delete context; context = nullptr; admitted = nullptr; owner = 0; presents = original_calls = output_calls = resets = 0; stopped = false;
    delete scene;scene=nullptr;delete boundary;boundary=nullptr;scene_mode=false;ui_draw_calls=composite_draw_calls=0;
    retired_valid = false; retired_stats = {}; stop_pending = false;
    render_width = render_height = output_width = output_height = 0; return true;
}
extern "C" HRESULT On12SpatialFixtureUpscaleAttach(IDirect3DDevice9* device, bool (*validate)(IDirect3DDevice9*, IDirect3DSurface9*)) {
    Guard guard; render_width = render_height = 32; output_width = output_height = 64; validate_output = validate;
    return Attach(device);
}
extern "C" HRESULT On12SpatialFixtureSceneAttach(IDirect3DDevice9* device,IDirect3DVertexShader9* v,IDirect3DPixelShader9* p){
    Guard guard;scene_mode=true;render_width=render_height=16;output_width=output_height=32;
    const HRESULT hr=Attach(device);if(SUCCEEDED(hr))boundary->FixtureUi(v,p);return hr;
}
extern "C" bool On12SpatialFixtureTemporalRead(bool ui){
    if(!scene||!context||!SceneOwn(admitted)||scene->Ui()!=ui)return false;
    Guard guard;
    const auto logical=On12Graphics::ReadTemporalDeviceInputs(admitted);
    const auto physical=On12Graphics::ReadTemporalDeviceInputs(context->NativeDevice());
    const UINT expected=ui?32u:16u;
    return SUCCEEDED(logical.viewport_result)&&SUCCEEDED(physical.viewport_result)
        &&logical.viewport.Width==32&&physical.viewport.Width==expected
        &&SUCCEEDED(logical.targets[0].result)&&SUCCEEDED(physical.targets[0].result)
        &&logical.targets[0].width==32&&physical.targets[0].width==expected;
}
#endif
