// Authored CPU-only COM-shaped dispatch. No D3D factory/device/game is created.
#define S7_SUPERSAMPLING_FIXTURE
#include "shadow_filter_effects.cpp"
#include "material_shader_observer.cpp"
#include "supersampling_hook.cpp"
#include <cstdio>
#include <thread>
#include <type_traits>

static_assert(std::is_same<decltype(&SupersamplingHook::HookDrawUP),UpDrawObserverProtocol::PrimitiveMethod>::value,"primitive stdcall ABI");
static_assert(std::is_same<decltype(&SupersamplingHook::HookDrawIndexedUP),UpDrawObserverProtocol::IndexedMethod>::value,"indexed stdcall ABI");
namespace {
unsigned checks=0;
void Check(bool ok,const char* label){++checks;if(!ok){std::fprintf(stderr,"FAIL: %s\n",label);ExitProcess(1);}}
constexpr DWORD PostError=0x73578384;
struct FakeDevice {
    void** table=nullptr;unsigned calls=0,queries=0;HRESULT returned=S_OK;bool throws=false,reenter=false;
    IDirect3DDevice9* reenter_device=nullptr;
    void* stream=reinterpret_cast<void*>(0x1111);void* index=reinterpret_cast<void*>(0x2222);
    D3DPRIMITIVETYPE type=D3DPT_POINTLIST;UINT min=0,vertices=0,count=0,stride=0;
    const void* vertex_data=nullptr;const void* index_data=nullptr;D3DFORMAT format=D3DFMT_UNKNOWN;
    unsigned post_mxcsr=0;unsigned short post_control=0;
};
unsigned short Control(){unsigned short value=0;__asm {fnstcw value}return value;}
void Post(FakeDevice* device){
    unsigned ignored=0;_controlfp_s(&ignored,_RC_UP,_MCW_RC);device->post_control=Control();
    _mm_setcsr(device->post_mxcsr);SetLastError(PostError);if(device->throws)throw 1;
}
HRESULT STDMETHODCALLTYPE OriginalPrimitive(IDirect3DDevice9* opaque,D3DPRIMITIVETYPE type,UINT count,const void* data,UINT stride){
    auto* device=reinterpret_cast<FakeDevice*>(opaque);++device->calls;device->type=type;device->count=count;
    device->vertex_data=data;device->stride=stride;device->stream=nullptr;
    if(device->reenter){device->reenter=false;SupersamplingHook::HookDrawIndexedUP(device->reenter_device?device->reenter_device:opaque,type,0,3,1,
        reinterpret_cast<void*>(3),D3DFMT_INDEX16,reinterpret_cast<void*>(1),12);}
    Post(device);return device->returned;
}
HRESULT STDMETHODCALLTYPE OriginalIndexed(IDirect3DDevice9* opaque,D3DPRIMITIVETYPE type,UINT min,UINT vertices,UINT count,
    const void* indices,D3DFORMAT format,const void* data,UINT stride){
    auto* device=reinterpret_cast<FakeDevice*>(opaque);++device->calls;device->type=type;device->min=min;
    device->vertices=vertices;device->count=count;device->index_data=indices;device->format=format;device->vertex_data=data;
    device->stride=stride;device->stream=device->index=nullptr;
    if(device->reenter){device->reenter=false;SupersamplingHook::HookDrawUP(device->reenter_device?device->reenter_device:opaque,
        type,1,reinterpret_cast<void*>(1),12);}
    Post(device);return device->returned;
}
HRESULT STDMETHODCALLTYPE Vertex(IDirect3DDevice9* opaque,IDirect3DVertexShader9** output){
    auto* device=reinterpret_cast<FakeDevice*>(opaque);++device->queries;*output=nullptr;
    Check(SupersamplingHook::internal&&SupersamplingHook::internal->device==opaque,"observer query retains exact TLS/device");
    SetLastError(1);_mm_setcsr(0x1f80);return S_OK;
}
HRESULT STDMETHODCALLTYPE Pixel(IDirect3DDevice9* opaque,IDirect3DPixelShader9** output){
    auto* device=reinterpret_cast<FakeDevice*>(opaque);++device->queries;*output=nullptr;
    Check(SupersamplingHook::internal&&SupersamplingHook::internal->device==opaque,"pixel query retains exact TLS/device");
    SetLastError(2);_mm_setcsr(0x1f80);return S_OK;
}
void Preserved(const FakeDevice& device){
    Check(GetLastError()==PostError,"original post-call LastError retained");
    Check(Control()==device.post_control&&_mm_getcsr()==device.post_mxcsr,"original post-call x87/MXCSR retained");
    Check(!device.stream,"original UP stream-reset side effect retained");
}
}
int main(){
    using namespace SupersamplingHook;
    alignas(16) unsigned char fp[512];_fxsave(fp);const DWORD entry_error=GetLastError();
    std::array<void*,119> table{};table[83]=reinterpret_cast<void*>(OriginalPrimitive);table[84]=reinterpret_cast<void*>(OriginalIndexed);
    table[93]=reinterpret_cast<void*>(Vertex);table[108]=reinterpret_cast<void*>(Pixel); // SDK offsets independently asserted below.
    FakeDevice fake{};fake.table=table.data();fake.post_mxcsr=0x3f80;auto* device=reinterpret_cast<IDirect3DDevice9*>(&fake);
    SupersamplingHook::State owned{};owned.device=device;owned.thread=GetCurrentThreadId();owned.original=table.data();state=&owned;
    saved_methods[83]=table[83];saved_methods[84]=table[84];game_modules[0]=GetModuleHandleW(nullptr);
    native_configuration.flags=NativeGraphicsConfig::Observe;configuration={};stats={sizeof(stats),1};
    MaterialShaderObserver::current.stats.enabled=1;MaterialShaderObserver::current.thread=GetCurrentThreadId();
    MaterialShaderObserver::current.device=device;
    const void* data=reinterpret_cast<void*>(1);const void* indices=reinterpret_cast<void*>(3);
    stats.effects_frame_has_geometry=0;stats.high_resolution_draws=37;stats.effects_drawn_revision=5;
    Check(HookDrawUP(device,D3DPT_TRIANGLESTRIP,2,data,48)==S_OK,"owned UP original HRESULT");
    Check(fake.calls==1&&fake.queries==2&&stats.up_observer_calls==1,"owned successful nonzero UP observes once");
    Check(fake.type==D3DPT_TRIANGLESTRIP&&fake.count==2&&fake.vertex_data==data&&fake.stride==48,"UP arguments bitwise unchanged");
    Preserved(fake);
    Check(HookDrawIndexedUP(device,D3DPT_TRIANGLELIST,5,17,3,indices,D3DFMT_INDEX32,data,44)==S_OK,"indexed UP original HRESULT");
    Check(fake.calls==2&&fake.queries==4&&stats.up_observer_calls==2,"indexed UP observes once");
    Check(fake.min==5&&fake.vertices==17&&fake.count==3&&fake.index_data==indices&&fake.format==D3DFMT_INDEX32
        &&fake.vertex_data==data&&fake.stride==44&&!fake.index,"indexed arguments/index reset unchanged");Preserved(fake);
    Check(MaterialShaderObserver::current.stats.successful_draws==2&&!MaterialShaderObserver::current.stats.context_rejections,
        "real observer accepts kinds3/4 without user-buffer reads");
    Check(stats.high_resolution_draws==37&&!stats.effects_frame_has_geometry&&stats.effects_drawn_revision==5,
        "UP observations cannot qualify high scene or fresh material epoch");
    unsigned observed=stats.up_observer_calls,calls=fake.calls;
    Check(HookDrawUP(device,D3DPT_TRIANGLELIST,0,data,48)==S_OK&&fake.calls==++calls,"zero count still original once");
    Check(HookDrawUP(device,D3DPT_TRIANGLELIST,1,nullptr,48)==S_OK&&fake.calls==++calls,"null data still original once");
    Check(HookDrawUP(device,D3DPT_TRIANGLELIST,1,data,0)==S_OK&&fake.calls==++calls,"zero stride still original once");
    Check(HookDrawIndexedUP(device,D3DPT_TRIANGLELIST,0,0,1,indices,D3DFMT_INDEX16,data,48)==S_OK&&fake.calls==++calls,
        "zero vertices still original once");
    Check(HookDrawIndexedUP(device,D3DPT_TRIANGLELIST,0,3,1,indices,D3DFMT_UNKNOWN,data,48)==S_OK&&fake.calls==++calls,
        "unknown index format still original once");
    Check(stats.up_observer_calls==observed,"invalid-looking UP input not observed");
    fake.returned=D3DERR_INVALIDCALL;
    Check(HookDrawUP(device,D3DPT_TRIANGLELIST,1,data,48)==D3DERR_INVALIDCALL&&fake.calls==++calls
        &&stats.up_observer_calls==observed,"failed HRESULT preserved without observer");Preserved(fake);
    fake.returned=S_OK;game_modules[0]=nullptr;
    Check(HookDrawUP(device,D3DPT_TRIANGLELIST,1,data,48)==S_OK&&fake.calls==++calls&&stats.up_observer_calls==observed,
        "unknown caller forwards once without observer");Preserved(fake);game_modules[0]=GetModuleHandleW(nullptr);
    std::thread other([&](){Check(HookDrawUP(device,D3DPT_TRIANGLELIST,1,data,48)==S_OK,"foreign thread original forwarded");Preserved(fake);});other.join();
    ++calls;Check(fake.calls==calls&&stats.up_observer_calls==observed,"foreign thread not observed");
    owned.shutdown_requested=true;
    Check(HookDrawUP(device,D3DPT_TRIANGLELIST,1,data,48)==S_OK&&fake.calls==++calls&&stats.up_observer_calls==observed,
        "shutdown context forwarded without observer");owned.shutdown_requested=false;
    FakeDevice foreign{};foreign.table=table.data();foreign.post_mxcsr=0x3f80;foreign.reenter=true;
    Check(HookDrawUP(reinterpret_cast<IDirect3DDevice9*>(&foreign),D3DPT_TRIANGLELIST,1,data,48)==S_OK&&foreign.calls==2
        &&stats.up_observer_calls==observed,"foreign-device reentry forwarded once per method without deadlock/observer");Preserved(foreign);
    fake.reenter=true;
    Check(HookDrawUP(device,D3DPT_TRIANGLELIST,1,data,48)==S_OK&&fake.calls==calls+2
        &&stats.up_observer_calls==observed+1,"owned nested original uses TLS passthrough without duplicate observation");calls+=2;observed++;
    fake.reenter=true;fake.reenter_device=reinterpret_cast<IDirect3DDevice9*>(&foreign);const unsigned foreign_calls=foreign.calls;
    Check(HookDrawUP(device,D3DPT_TRIANGLELIST,1,data,48)==S_OK&&fake.calls==++calls&&foreign.calls==foreign_calls+1
        &&stats.up_observer_calls==observed+1,"different-device nested UP bypasses held lock without observer");observed++;
    Check(!foreign.stream&&!foreign.index,"foreign nested original retains its own UP reset side effects");Preserved(fake);
    fake.reenter=true;const unsigned before_foreign_primitive=foreign.calls;
    Check(HookDrawIndexedUP(device,D3DPT_TRIANGLELIST,0,3,1,indices,D3DFMT_INDEX16,data,48)==S_OK
        &&fake.calls==++calls&&foreign.calls==before_foreign_primitive+1&&stats.up_observer_calls==observed+1,
        "different-device nested primitive UP also bypasses held lock without observer");observed++;Preserved(fake);
    fake.reenter_device=nullptr;
    stats.render_unsafe=1;SetLastError(0x1234);
    Check(HookDrawUP(device,D3DPT_TRIANGLELIST,1,data,48)==D3DERR_DEVICELOST&&fake.calls==calls,"unsafe host blocks UP draw");
    Check(HookDrawIndexedUP(device,D3DPT_TRIANGLELIST,0,3,1,indices,D3DFMT_INDEX16,data,48)==D3DERR_DEVICELOST
        &&fake.calls==calls&&stats.up_blocked_calls==2,"unsafe host blocks indexed UP draw");
    Check(GetLastError()==0x1234&&stats.up_observer_calls==observed,"blocked calls preserve entry error and do not observe");stats.render_unsafe=0;
    fake.throws=true;
    Check(HookDrawUP(device,D3DPT_TRIANGLELIST,1,data,48)==D3DERR_DRIVERINTERNALERROR&&fake.calls==++calls
        &&stats.up_original_exceptions==1&&stats.up_observer_calls==observed,"owned original exception converted without observer");fake.throws=false;
    state=nullptr;fake.reenter=true;
    Check(HookDrawUP(device,D3DPT_TRIANGLELIST,1,data,48)==S_OK&&fake.calls==calls+2&&stats.up_observer_calls==observed,
        "missing state forwards/reenters without held lock or duplicate observer");
    std::array<void*,119> patched=table;
    SupersamplingHook::State closing{};closing.device=device;closing.vtable=patched.data();
    for(size_t slot=0;slot<table.size();++slot)closing.table[slot]=table[slot];
    closing.original=closing.table.data();
    Check(Intercepts.size()==9,"exact nine documented COM slots installed");
    for(const auto& entry:Intercepts)patched[entry.first]=entry.second;
    Check(TerminalCleanup(closing)&&closing.terminal_cleanup,"read-only UP slots participate in ordinary terminal cleanup");
    for(const auto& entry:Intercepts)Check(patched[entry.first]==table[entry.first],"every intercepted slot restored exactly");
    Check(patched[83]==reinterpret_cast<void*>(OriginalPrimitive)&&patched[84]==reinterpret_cast<void*>(OriginalIndexed),
        "UP originals restored rather than replacing vptr/SDK truncation");
    MaterialShaderObserver::current.stats.enabled=0;MaterialShaderObserver::current.device=nullptr;
    _fxrstor(fp);SetLastError(entry_error);
    std::printf("PASS: %u authored CPU integrated UP-dispatch checks; no GPU/game\n",checks);return 0;
}
