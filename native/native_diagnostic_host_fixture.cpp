// Authored CPU-only host proof. Same-translation-unit access creates no public
// injection/bypass API and never creates a D3D factory/device or game process.
#define S7_SUPERSAMPLING_FIXTURE
#include "ambient_occlusion_effects.cpp"
#include "shadow_filter_effects.cpp"
#include "supersampling_hook.cpp"
#include <cstdio>
#include <float.h>
static_assert(sizeof(ShadowFilterEffects::Stats)==312,"Shadow observation ABI unchanged");
static_assert(sizeof(AmbientOcclusionEffects::Stats)==320,"AO profile Stats v2");
static_assert(offsetof(AmbientOcclusionEffects::Stats,profile_version)==312,"Appended explicit AO profile");

namespace {
unsigned checks=0,original_d3d_calls=0,original_screen_calls=0;
void* original_screen_argument=nullptr;
void Check(bool value,const char* label){++checks;if(!value){std::fprintf(stderr,"FAIL: %s\n",label);ExitProcess(1);}}
unsigned short Control(){unsigned short value=0;__asm {fnstcw value}return value;}
void SetState(unsigned round,DWORD error){unsigned ignored=0;_controlfp_s(&ignored,round,_MCW_RC);SetLastError(error);}
void __cdecl OriginalD3D(){++original_d3d_calls;SetState(_RC_UP,0x7357);}
unsigned release_calls=0;
ULONG STDMETHODCALLTYPE OriginalRelease(IDirect3DDevice9*){++release_calls;return 7;}
bool __cdecl OriginalScreen(void* argument){++original_screen_calls;original_screen_argument=argument;SetState(_RC_UP,0x7357);return true;}
AmbientOcclusionEffects::Stats ValidFrame(){
    AmbientOcclusionEffects::Stats value{};value.initialized=value.authorized=value.enabled=1;
    value.frames=value.completed_frame=value.last_applied_frame=value.last_restored_frame=9;
    value.completed_applications=value.completed_restorations=2;value.successful_draws=11;value.failed_draws=3;
    return value;
}
bool Eligible(const AmbientOcclusionEffects::Stats& value,bool healthy=true){
    std::uint64_t successful=0,failed=0;
    return SupersamplingHook::CompletedAmbientFrameGood(value,healthy,10,3,109,100,successful,failed);
}
void ResetHost(){
    using namespace SupersamplingHook;
    state=nullptr;configuration={};native_configuration={};stats={sizeof(stats),1};
    ambient_received=ambient_waiting=ambient_initialized=ambient_state_valid=ambient_frame_good=false;
    ambient_frame_counters_valid=false;
    ambient_state=AmbientOcclusionEffects::Stats{};ambient_frame_origin=0;
    ambient_successful_total=ambient_failed_total=ambient_frame_successful=ambient_frame_failed=0;
    shadow_received=shadow_waiting=shadow_initialized=shadow_state_valid=shadow_frame_good=false;
    shadow_frame_counters_valid=false;shadow_state=ShadowFilterEffects::Stats{};shadow_frame_origin=0;
    InterlockedExchange(&initialization_ready,1);
}
}
int main(){
    using namespace SupersamplingHook;
    setvbuf(stdout,nullptr,_IONBF,0);std::puts("CPU: frame policy");
    alignas(16) unsigned char saved_fp[512];_fxsave(saved_fp);const DWORD saved_error=GetLastError();
    for(DWORD flags=0;flags<64;++flags){
        Check(AmbientIntentAuthorized(flags)==((flags&33)==33 && !(flags&16)),"explicit Observe+AO-only intent");
        Check(ShadowIntentAuthorized(flags)==((flags&17)==17 && !(flags&32)),"explicit Observe+Shadow-only intent");
    }
    Check(CompiledCapabilities==15,"diagnostic mask cannot expand reported production capabilities");
    wchar_t directory[512]{};const DWORD directory_length=GetCurrentDirectoryW(512,directory);
    Check(directory_length&&directory_length<450&&std::wstring(directory).find(L"\\work\\")!=std::wstring::npos,
        "CPU diagnostics remain in owned work directory");
    const std::wstring prefix=std::wstring(directory)+L"\\cpu-never-";
    for(DWORD flags:{16u,32u,48u,49u,65u}) {
        ResetHost();InterlockedExchange(&initialized,0);InterlockedExchange(&initialization_ready,0);
        NativeGraphicsConfig::Version2 value{};value.base={sizeof(value),2,1280,720,640,360,{}};value.flags=flags;
        wcscpy_s(value.base.report_path,(prefix+L"written.private.json").c_str());
        if(flags&1){wcscpy_s(value.catalog_path,(prefix+L"read.bin").c_str());
            wcscpy_s(value.observer_report_path,(prefix+L"written-observer.json").c_str());value.catalog_sha256[0]=1;}
        Check(WePlaySupersamplingInitialize(&value)==0&&!initialization_ready&&!stats.last_failure&&!configuration.report_path[0],
            "actual initializer rejects missing Observe/conflicting/unknown intent before any renderer or report access");
    }
    auto frame=ValidFrame();Check(Eligible(frame),"positive exact-frame successful balanced ambient proof");
    Check(!Eligible(frame,false),"unhealthy module cannot qualify");
    for(unsigned field=0;field<15;++field){
        auto wrong=frame;
        switch(field){case 0:wrong.initialized=0;break;case 1:wrong.authorized=0;break;case 2:wrong.enabled=0;break;
        case 3:wrong.faulted=1;break;case 4:wrong.unsafe_state=1;break;case 5:wrong.successful_draws=10;break;
        case 6:wrong.successful_draws=9;break;case 7:wrong.failed_draws=4;break;case 8:wrong.failed_draws=2;break;
        case 9:wrong.completed_applications=0;break;case 10:wrong.completed_restorations=1;break;
        case 11:wrong.completed_frame=8;break;case 12:wrong.last_applied_frame=8;break;
        case 13:wrong.last_restored_frame=8;break;case 14:wrong.frames=8;break;}
        Check(!Eligible(wrong),"bad frame/counter/module state refuses capture");
    }
    frame=ValidFrame();frame.successful_draws=10+static_cast<std::uint64_t>(MAXDWORD)+1;
    Check(!Eligible(frame),"per-frame DWORD success overflow refuses capture");
    frame=ValidFrame();frame.frames=frame.completed_frame=frame.last_applied_frame=frame.last_restored_frame=0;
    std::uint64_t success=0,failed=0;
    Check(!CompletedAmbientFrameGood(frame,true,10,3,100,100,success,failed),"zero completed frame cannot qualify");

    std::puts("CPU: queue/config");ResetHost();AmbientOcclusionEffects::current={};
    native_configuration.flags=33;configuration.render_width=1280;configuration.render_height=720;
    AmbientOcclusionEffects::Config config{};
    for(DWORD flags:{0u,1u,16u,17u,32u,48u,49u}){native_configuration.flags=flags;
        Check(WePlayNativeGraphicsConfigureAmbient(&config)==0,"missing intent refuses fixed export");}
    native_configuration.flags=33;
    shadow_received=true;AmbientOcclusionEffects::Config opposite{};
    Check(WePlayNativeGraphicsConfigureAmbient(&opposite)==0,"opposite received trial cannot switch into AO");shadow_received=false;
    native_configuration.flags=49;
    Check(WePlayNativeGraphicsConfigureAmbient(&opposite)==0,"simultaneous AO/Shadow flags cannot configure AO");
    ShadowFilterEffects::Config shadow{};
    Check(WePlayNativeGraphicsConfigureShadow(&shadow)==0,"simultaneous AO/Shadow flags cannot configure Shadow");
    native_configuration.flags=33;
    for(unsigned bad=0;bad<4;++bad){auto wrong=config;
        if(bad==0)wrong.size=11;if(bad==1)wrong.version=3;if(bad==2)wrong.enabled=2;
        Check(WePlayNativeGraphicsConfigureAmbient(bad==3?reinterpret_cast<void*>(1):&wrong)==0,"malformed/unreadable Config12 refuses");}
    Check(!ambient_received&&!ambient_waiting,"invalid command never mutates queue");
    Check(WePlayNativeGraphicsConfigureAmbient(&config)==1,"fixed authorized Config12 queues");
    config.enabled=1;Check(ambient_pending_config.enabled==0,"queued config copied rather than borrowed");
    Check(WePlayNativeGraphicsConfigureAmbient(&config)==0,"pending command cannot be replaced");
    stats.present_calls=7;ApplyPendingAmbient();
    Check(ambient_initialized&&!ambient_waiting&&ambient_frame_origin==7&&stats.ambient_frame_origin==7
        &&ambient_state.initialized&&!ambient_state.enabled,"post-Present owner initializes fixed off profile");
    Check(WePlayNativeGraphicsConfigureAmbient(&config)==1,"explicit enable queues at safe boundary");ApplyPendingAmbient();
    Check(ambient_state.enabled&&stats.ambient_active==1&&!stats.ambient_frame_good,"enable does not qualify stale backbuffer");
    Check(ambient_state.profile_version==1,"original Config1 still selects exact profile A");
    AmbientOcclusionEffects::Config profile_b{};profile_b.version=2;profile_b.enabled=1;
    Check(WePlayNativeGraphicsConfigureAmbient(&profile_b)==1,"explicit Config2 selects B queue");
    profile_b.version=3;
    Check(ambient_pending_config.version==2&&ambient_state.profile_version==1,"queued B version is copied and not applied midframe");
    ApplyPendingAmbient();
    Check(ambient_state.profile_version==2&&ambient_state.version==2&&ambient_state.enabled,"post-Present B profile and Stats v2 are explicit");
    Check(AmbientJson().find("\"profile_version\":2")!=std::string::npos,"main report records selected B profile");
    Check(WePlayNativeGraphicsConfigureAmbient(&profile_b)==0&&!ambient_waiting&&ambient_state.profile_version==2,"unknown profile3 refuses without changing B");
    profile_b.version=2;profile_b.enabled=0;
    Check(WePlayNativeGraphicsConfigureAmbient(&profile_b)==1,"explicit B disable queues");ApplyPendingAmbient();
    Check(ambient_state.profile_version==2&&!ambient_state.enabled,"B disable retains explicit profile metadata");
    AmbientOcclusionEffects::Config profile_a{};
    Check(WePlayNativeGraphicsConfigureAmbient(&profile_a)==1,"explicit return to A remains supported");ApplyPendingAmbient();
    Check(ambient_state.profile_version==1&&!ambient_state.enabled,"A identity is not silently repinned to B");
    Check(AmbientOcclusionEffects::Shutdown(),"CPU module clean shutdown");

    std::puts("CPU: fourth-scope adapter");ResetHost();AmbientOcclusionEffects::current={};ShadowFilterEffects::current={};
    unsigned draw_calls=0,observer_calls=0;
    const auto draw=[&](){++draw_calls;return S_OK;};const auto observe=[&](HRESULT){++observer_calls;};
    const auto conflict=ExecuteConfiguredDraw(nullptr,false,false,false,true,true,draw,observe);
    Check(conflict.unsafe&&!conflict.original_called&&!draw_calls&&!observer_calls,"two fourth-scope profiles refuse before original draw");
    AmbientOcclusionEffects::current.stats.initialized=AmbientOcclusionEffects::current.stats.enabled=1;
    AmbientOcclusionEffects::current.backup_valid=true;AmbientOcclusionEffects::current.stats.unsafe_state=1;
    const auto ambient_fault=ExecuteConfiguredDraw(nullptr,false,false,false,false,true,draw,observe);
    Check(ambient_fault.unsafe&&ambient_fault.shadow_unsafe&&!draw_calls&&!observer_calls,"AO adapter reuses fourth unsafe/retry/observer contract");
    Check(!ShadowFilterEffects::current.stats.rejections,"AO route never calls the Shadow module");
    AmbientOcclusionEffects::current={};ShadowFilterEffects::current.stats.initialized=ShadowFilterEffects::current.stats.enabled=1;
    ShadowFilterEffects::current.backup_valid=true;ShadowFilterEffects::current.stats.unsafe_state=1;
    const auto shadow_fault=ExecuteConfiguredDraw(nullptr,false,false,false,true,false,draw,observe);
    Check(shadow_fault.unsafe&&shadow_fault.shadow_unsafe&&!draw_calls&&!observer_calls,"Shadow route remains independent fourth profile");
    Check(!AmbientOcclusionEffects::current.stats.rejections,"Shadow route never calls the AO module");ShadowFilterEffects::current={};
    for(unsigned indexed=0;indexed<2;++indexed) {
        ResetHost();AmbientOcclusionEffects::current={};ambient_initialized=true;ambient_state.enabled=1;
        AmbientOcclusionEffects::current.stats.initialized=AmbientOcclusionEffects::current.stats.enabled=1;
        AmbientOcclusionEffects::current.stats.unsafe_state=1;AmbientOcclusionEffects::current.backup_valid=true;
        SupersamplingHook::State render{};render.thread=GetCurrentThreadId();state=&render;
        const HRESULT result=indexed?HookDrawIndexed(nullptr,D3DPT_TRIANGLELIST,0,0,3,0,1):HookDraw(nullptr,D3DPT_TRIANGLELIST,0,1);
        Check(result==D3DERR_DEVICELOST&&stats.render_unsafe&&stats.ambient_unsafe&&!stats.shadow_unsafe,
            "actual regular/indexed hook maps fourth-scope unsafe to AO rather than Shadow");
        Check(!stats.ambient_frame_good&&!stats.effects_frame_has_geometry,"unsafe AO hook cannot qualify current frame");
    }
    AmbientOcclusionEffects::current={};ResetHost();

    std::puts("CPU: end-frame gate");ResetHost();AmbientOcclusionEffects::current={};
    ambient_received=ambient_initialized=true;ambient_frame_origin=100;stats.ambient_frame_origin=100;stats.present_calls=109;
    AmbientOcclusionEffects::current.stats=ValidFrame();AmbientOcclusionEffects::current.thread=GetCurrentThreadId();
    auto& module=AmbientOcclusionEffects::current.stats;
    module.frames=9;module.current_applications=module.current_restorations=2;
    module.last_applied_frame=module.last_restored_frame=10;
    ambient_successful_total=10;ambient_failed_total=3;
    EndAmbientFrame(nullptr);++stats.present_calls;
    Check(stats.ambient_frame_good==1&&stats.ambient_frame_successful_draws==1&&!stats.ambient_frame_failed_draws,
        "module EndFrame produces bounded successful-draw delta proof");
    Check(CaptureEpochReady(),"same-frame actual host capture gate passes positive proof");
    SupersamplingHook::State releasing{};releasing.device=nullptr;releasing.table[2]=reinterpret_cast<void*>(OriginalRelease);
    releasing.original=releasing.table.data();state=&releasing;
    Check(HookRelease(nullptr)==7&&release_calls==1&&CaptureEpochReady(),
        "ordinary external Release clears refs but preserves copied same-frame AO proof");state=nullptr;
    EndAmbientFrame(nullptr);++stats.present_calls;
    Check(!stats.ambient_frame_good&&!CaptureEpochReady(),"next Present without fresh ambient draw cannot reuse proof");
    module.enabled=0;module.successful_draws+=static_cast<std::uint64_t>(MAXDWORD)+1;
    EndAmbientFrame(nullptr);++stats.present_calls;
    Check(!ambient_frame_counters_valid&&!CaptureEpochReady(),"off control cannot bypass per-frame DWORD overflow refusal");
    module.successful_draws=0;EndAmbientFrame(nullptr);++stats.present_calls;
    Check(!ambient_frame_counters_valid&&!CaptureEpochReady(),"off control cannot bypass nonmonotonic counter refusal");
    Check(AmbientOcclusionEffects::Shutdown(),"end-frame CPU module releases cleanly");

    // No COM device is available here. An unrestored durable backup must stop
    // both original graphics teardown callbacks before any null-device call.
    std::puts("CPU: refused teardown");ResetHost();AmbientOcclusionEffects::current={};
    SupersamplingHook::State owned{};owned.thread=GetCurrentThreadId();state=&owned;
    game_modules[0]=GetModuleHandleW(nullptr);ambient_initialized=true;
    AmbientOcclusionEffects::current.stats.initialized=1;AmbientOcclusionEffects::current.stats.unsafe_state=1;
    AmbientOcclusionEffects::current.backup_valid=true;
    original_deinit_d3d=OriginalD3D;original_deinit_screen=OriginalScreen;
    SetState(_RC_DOWN,0x1234);const unsigned short entry_control=Control();const unsigned entry_mxcsr=_mm_getcsr();
    HookDeinitD3D();
    Check(!original_d3d_calls&&stats.render_unsafe&&stats.ambient_unsafe,"unsafe null-device cleanup blocks original D3D deinit");
    Check(Control()==entry_control&&_mm_getcsr()==entry_mxcsr&&GetLastError()==0x1234,"refused D3D teardown preserves caller FP/error");
    Check(!HookDeinitScreen(reinterpret_cast<void*>(0x7357))&&!original_screen_calls,"unsafe null-device cleanup blocks original Screen deinit");
    Check(Control()==entry_control&&_mm_getcsr()==entry_mxcsr&&GetLastError()==0x1234,"refused Screen teardown preserves caller FP/error");
    state=nullptr;
    HookDeinitD3D();
    Check(!original_d3d_calls,"unsafe module with missing hostState cannot reach original D3D deinit");
    Check(!HookDeinitScreen(reinterpret_cast<void*>(0x7357))&&!original_screen_calls,
        "unsafe module with missing hostState cannot reach original Screen deinit");
    Check(Control()==entry_control&&_mm_getcsr()==entry_mxcsr&&GetLastError()==0x1234,"missing-host rejection preserves caller FP/error");
    std::puts("CPU: normal teardown");AmbientOcclusionEffects::current={};ResetHost();
    HookDeinitD3D();Check(original_d3d_calls==1,"verified clean boundary calls exact original D3D callback once");
    Check(GetLastError()==0x7357,"normal D3D teardown retains original post-error");
    void* argument=reinterpret_cast<void*>(0x7357);
    Check(HookDeinitScreen(argument)&&original_screen_calls==1&&original_screen_argument==argument,
        "verified clean Screen callback preserves pointer/result and exact call count");
    Check(GetLastError()==0x7357,"normal Screen teardown retains original post-error");
    _fxrstor(saved_fp);SetLastError(saved_error);
    std::printf("PASS: %u authored CPU ambient host admission/frame/teardown checks; no GPU/game\n",checks);
    return 0;
}
