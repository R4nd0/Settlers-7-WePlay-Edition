// Authored CPU-only host proof. Same-translation-unit access creates no public
// injection/bypass API and never creates a D3D factory/device or game process.
#define S7_SUPERSAMPLING_FIXTURE
#include "ambient_occlusion_effects.cpp"
#include "shadow_filter_effects.cpp"
#include "supersampling_hook.cpp"
#include <cstdio>
#include <float.h>
#include <filesystem>
#include <fstream>
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
    combined_scope_authorized=false;overlay_geometry_frame=overlay_geometry_revision=overlay_geometry_active=0;
    ambient_received=ambient_waiting=ambient_initialized=ambient_state_valid=ambient_frame_good=false;
    ambient_frame_counters_valid=false;
    ambient_state=AmbientOcclusionEffects::Stats{};ambient_frame_origin=0;
    ambient_successful_total=ambient_failed_total=ambient_frame_successful=ambient_frame_failed=0;
    shadow_received=shadow_waiting=shadow_initialized=shadow_state_valid=shadow_frame_good=false;
    shadow_frame_counters_valid=false;shadow_state=ShadowFilterEffects::Stats{};shadow_frame_origin=0;
    InterlockedExchange(&initialization_ready,1);
}
void EffectsV3Proof(const std::wstring& directory){
    using namespace SupersamplingHook;
    std::puts("CPU: explicit V3 transport/host dispatch");ResetHost();
    Check(sizeof(MaterialShaderEffects::Config)==4132&&sizeof(MaterialShaderConfigV2::Config)==6180
        &&sizeof(MaterialShaderConfigV3::Config)==7204,"old and new typed payload sizes remain exact");
    Check(MaterialShaderConfigV3::FitsWorker(392)&&!MaterialShaderConfigV3::FitsWorker(989),"bounded worker payload cannot overrun allocation");
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,12288,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(memory!=nullptr,"allocate bounded wire boundary fixture");DWORD protection=0;
    Check(VirtualProtect(memory+8192,4096,PAGE_NOACCESS,&protection)!=FALSE,"place inaccessible page after exact payload");
    MaterialShaderEffects::Config old{};MaterialShaderConfigV2::Config previous{};MaterialShaderConfigV3::Config next{};
    MaterialShaderConfigV4::Config palette{};MaterialShaderConfigV5::Config combined{};
    const void* values[]={&old,&previous,&next,&palette,&combined};const size_t lengths[]={sizeof(old),sizeof(previous),sizeof(next),sizeof(palette),sizeof(combined)};
    for(unsigned index=0;index<5;++index){
        auto* exact=memory+8192-lengths[index];std::memcpy(exact,values[index],lengths[index]);
        NativeEffectsWire::Pending copied{};
        Check(NativeEffectsWire::Decode(exact,&copied)&&copied.version==index+1,"each exact typed payload decodes at guard-page boundary without overread");
        const auto before=copied;
        reinterpret_cast<DWORD*>(exact)[1]=6;
        Check(!NativeEffectsWire::Decode(exact,&copied)&&!std::memcmp(&copied,&before,sizeof(copied)),"unknown version refuses without mutating output");
        reinterpret_cast<DWORD*>(exact)[1]=index+1;reinterpret_cast<DWORD*>(exact)[0]-=4;
        Check(!NativeEffectsWire::Decode(exact,&copied),"truncated declared typed payload refuses");
    }
    NativeEffectsWire::Pending copied{};
    const DWORD readable_header[]={7204,3};std::memcpy(memory+8192-8,readable_header,sizeof(readable_header));
    Check(!NativeEffectsWire::Decode(memory+8192-8,&copied),"readable header with inaccessible V3 body refuses");
    VirtualFree(memory,0,MEM_RELEASE);
    native_configuration.base={sizeof(native_configuration),2,1280,720,640,360,{}};
    native_configuration.flags=NativeGraphicsConfig::Observe|NativeGraphicsConfig::Vegetation;
    const auto root=std::filesystem::path(directory)/L"v3-dispatch";std::filesystem::create_directories(root);
    wcscpy_s(native_configuration.base.report_path,(root/L"hook.private.json").c_str());
    next.render_width=1280;next.render_height=720;
    wcscpy_s(next.report_path,(root/L"material-effects.private.json").c_str());
    const auto shader=root/L"LocalData"/L"MaterialShaders"/NativeEffectsContractV3::Recipe/(std::wstring(NativeEffectsContractV3::Digest)+L".ps.bin");
    std::filesystem::create_directories(shader.parent_path());wcscpy_s(next.leaf_pixel_path,shader.c_str());
    const DWORD authored_invalid[]={0xffff0300,0x0000ffff};
    {std::ofstream file(shader,std::ios::binary);file.write(reinterpret_cast<const char*>(authored_invalid),sizeof(authored_invalid));}
    Check(NativeEffectsContractV3::Validate(next,native_configuration,NativeEffectsContractV3::Layout::PrivateCase)==NativeEffectsContractV3::Error::None,
        "exact Leaf-only initial-off V3 host identity validates before pinned module content check");
    for(unsigned bad=0;bad<6;++bad){auto wrong=next;
        if(bad==0)wrong.version=2;if(bad==1)wrong.size=6180;if(bad==2)wrong.flags=MaterialShaderEffects::Terrain;
        if(bad==3)wrong.render_width=640;if(bad==4)wrong.leaf_vertex_path[0]=L'x';if(bad==5)wrong.river_density_scale=1;
        Check(!WePlayNativeGraphicsConfigureEffects(&wrong)&&!effects_waiting,"malformed V3 cannot mutate queue or masquerade as V2");
    }
    const DWORD entry=0x7253;SetLastError(entry);
    Check(WePlayNativeGraphicsConfigureEffects(&next)==1&&GetLastError()==entry,"valid V3 queues and preserves caller error");
    Check(effects_pending_version==3&&effects_pending_v3.version==3&&!stats.effects_active&&!effects_initialized,"typed V3 stays pending until safe frame boundary");
    next.leaf_pixel_path[0]=L'x';
    Check(effects_pending_v3.leaf_pixel_path[0]!=L'x',"queue owns payload independently of caller lifetime");
    Check(!WePlayNativeGraphicsConfigureEffects(&next),"pending command refuses replacement");
    ApplyPendingEffects();
    Check(!effects_waiting&&!effects_initialized&&effects_faulted&&stats.effects_config_errors==1&&!stats.effects_active,
        "unpinned content fails actual InitializeV3 safely with truthful failure rather than activating V1");
    effects_received=effects_waiting=effects_initialized=effects_faulted=false;effects_loaded_flags=0;effects_loaded_version=0;
    MaterialShaderEffects::Shutdown();ResetHost();
}
void EffectsV4Proof(const std::wstring& directory){
    using namespace SupersamplingHook;
    std::puts("CPU: explicit palette V4 dispatch and actual drawn frame");ResetHost();
    Check(sizeof(MaterialShaderConfigV4::Config)==4120&&MaterialShaderConfigV4::FitsWorker(440),"independent V4 fits actual worker overhead");
    MaterialShaderConfigV4::Config request{};request.render_width=1280;request.render_height=720;
    native_configuration.base={sizeof(native_configuration),2,1280,720,640,360,{}};
    native_configuration.flags=NativeGraphicsConfig::Observe|NativeGraphicsConfig::Vegetation;
    const auto root=std::filesystem::path(directory)/L"v4-dispatch";std::filesystem::create_directories(root);
    wcscpy_s(native_configuration.base.report_path,(root/L"hook.private.json").c_str());
    wcscpy_s(request.report_path,(root/L"material-effects.private.json").c_str());
    for(unsigned index=0;index<3;++index){
        const auto program=root/L"LocalData"/L"MaterialShaders"/NativeEffectsContractV4::Recipes[index]/(std::wstring(NativeEffectsContractV4::Digests[index])+L".ps.bin");
        std::filesystem::create_directories(program.parent_path());wcscpy_s(request.program_paths[index],program.c_str());
        const DWORD authored_invalid[]={0xffff0300,0xffff};std::ofstream file(program,std::ios::binary);
        file.write(reinterpret_cast<const char*>(authored_invalid),sizeof(authored_invalid));
    }
    for(unsigned bad=0;bad<6;++bad){auto wrong=request;
        if(bad==0)wrong.version=3;if(bad==1)wrong.size=4132;if(bad==2)wrong.flags=1;
        if(bad==3)wrong.selected_variant=3;if(bad==4)wrong.render_width=640;if(bad==5)wrong.program_paths[1][0]=L'x';
        Check(!WePlayNativeGraphicsConfigureEffects(&wrong)&&!effects_waiting,"invalid V4 cannot change queue");
    }
    Check(WePlayNativeGraphicsConfigureEffects(&request)==1&&effects_pending_version==4,"typed initialOFF palette queues");
    request.selected_variant=2;request.program_paths[0][0]=L'x';
    Check(effects_pending_v4.selected_variant==0&&effects_pending_v4.program_paths[0][0]!=L'x',"queued palette owns complete caller-independent paths/selection");
    ApplyPendingEffects();Check(effects_faulted&&!effects_initialized&&!stats.effects_active,"all three files must be hash-validated before palette activation");
    effects_received=effects_waiting=effects_initialized=effects_faulted=false;MaterialShaderEffects::Shutdown();ResetHost();
    stats.effects_config_version=4;stats.effects_selected_variant=1;stats.effects_active=4;stats.effects_revision=7;
    InvalidatePaletteFrame();LatchEffectsDraw(0);
    Check(!stats.effects_frame_has_geometry&&stats.effects_drawn_variant==MAXDWORD,"generic original/UI geometry cannot label chosen palette PS as drawn");
    RecordPaletteDraw(4,S_OK);LatchEffectsDraw(4);CompletePaletteFrame();++stats.present_calls;
    Check(stats.effects_drawn_variant==1&&stats.effects_drawn_frame==stats.present_calls&&stats.effects_variant_frame_applications==1
        &&stats.effects_variant_frame_restorations==1&&!stats.effects_variant_frame_failed_draws,"successful safe actual Leaf transaction latches exact variant/frame");
    effects_initialized=true;stats.effects_configured=1;stats.effects_requested=4;
    Check(CaptureEpochReady(),"fresh matched selected palette frame qualifies");
    ++stats.present_calls;Check(!CaptureEpochReady(),"second Present cannot relabel old palette backbuffer");
    InvalidatePaletteFrame();RecordPaletteDraw(4,D3DERR_INVALIDCALL);RecordPaletteDraw(4,S_OK);LatchEffectsDraw(4);
    CompletePaletteFrame();++stats.present_calls;
    Check(!CaptureEpochReady()&&stats.effects_variant_frame_failed_draws==1,"failed original Leaf draw disqualifies entire frame despite later success");
    InvalidatePaletteFrame();palette_current_applications=static_cast<std::uint64_t>(MAXDWORD)+1;palette_current_restorations=palette_current_applications;
    LatchEffectsDraw(4);CompletePaletteFrame();++stats.present_calls;
    Check(!CaptureEpochReady()&&!stats.effects_frame_has_geometry,"per-frame count overflow refuses capture");
    InvalidatePaletteFrame();stats.effects_active=0;LatchEffectsDraw(0);CompletePaletteFrame();++stats.present_calls;
    Check(CaptureEpochReady()&&stats.effects_drawn_variant==MAXDWORD&&stats.effects_drawn_frame==stats.present_calls,
        "OFF uses fresh original geometry with no drawn replacement variant claim");
    InvalidatePaletteFrame();Check(!CaptureEpochReady(),"reset/attach/selection invalidation cannot reuse prior frame proof");
    effects_initialized=false;ResetHost();
}
void WaterPaletteProof(const std::wstring& directory){
    using namespace SupersamplingHook;
    std::puts("CPU: water palette3096 independent queue and same-frame proof");ResetHost();
    water_received=water_waiting=water_initialized=false;water_pending_version=1;capture_pending=capture_last_sequence=0;
    effects_waiting=false;
    WaterShaderEffects::Config one{};WaterShaderEffects::ConfigPalette palette{};
    auto* memory=static_cast<unsigned char*>(VirtualAlloc(nullptr,8192,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    Check(memory!=nullptr,"water wire allocation");DWORD protection=0;
    Check(VirtualProtect(memory+4096,4096,PAGE_NOACCESS,&protection)!=FALSE,"water wire guard page");
    const void* values[]={&one,&palette};const size_t lengths[]={sizeof(one),sizeof(palette)};
    for(unsigned index=0;index<2;++index){
        auto* exact=memory+4096-lengths[index];std::memcpy(exact,values[index],lengths[index]);NativeWaterWire::Pending decoded{};
        Check(NativeWaterWire::Decode(exact,&decoded)&&decoded.version==index+1,"old1076/new3096 exact guard-page payload");
        const auto before=decoded;reinterpret_cast<DWORD*>(exact)[1]=3;
        Check(!NativeWaterWire::Decode(exact,&decoded)&&!std::memcmp(&decoded,&before,sizeof(decoded)),"unknown water version preserves output");
        std::memcpy(exact,values[index],lengths[index]);reinterpret_cast<DWORD*>(exact)[0]-=4;
        Check(!NativeWaterWire::Decode(exact,&decoded),"water shortened declared payload refused");
    }
    const DWORD header[]={3096,2};std::memcpy(memory+4096-8,header,sizeof(header));NativeWaterWire::Pending decoded{};
    Check(!NativeWaterWire::Decode(memory+4096-8,&decoded),"water readable header inaccessible body refused");VirtualFree(memory,0,MEM_RELEASE);
    palette.render_width=configuration.render_width=1280;palette.render_height=configuration.render_height=720;
    native_configuration.flags=NativeGraphicsConfig::Water;
    const auto root=std::filesystem::path(directory)/L"water-palette-dispatch";std::filesystem::create_directories(root);
    wcscpy_s(configuration.report_path,(root/L"hook.private.json").c_str());
    for(unsigned index=0;index<3;++index){std::wstring digest;for(const char* at=WaterShaderPaletteContract::Digests[index];*at;++at)digest+=static_cast<wchar_t>(*at);
        const auto program=root/L"LocalData"/L"MaterialShaders"/WaterShaderPaletteContract::Recipes[index]/(digest+L".ps.bin");
        std::filesystem::create_directories(program.parent_path());wcscpy_s(palette.program_paths[index],program.c_str());
        const DWORD invalid[]={0xffff0300,0xffff};std::ofstream file(program,std::ios::binary);file.write(reinterpret_cast<const char*>(invalid),sizeof(invalid));}
    auto bad=palette;bad.selected_variant=3;Check(!WePlayNativeGraphicsConfigureWater(&bad)&&!water_waiting,"water unknown selector cannot queue");
    Check(WePlayNativeGraphicsConfigureWater(&palette)==1&&water_pending_version==2&&water_waiting,"explicit palette initialOFF queued");
    DWORD overlay_config[]={12,1,1};
    Check(WePlayNativeGraphicsConfigureOverlay(overlay_config)==1&&overlay_received&&overlay_requested&&!state&&!overlay_palette_admitted,
        "overlay admits validated pending startup palette before any device or GPU resource");
    for(unsigned invalid=0;invalid<3;++invalid){DWORD wrong[]={12,1,1};wrong[invalid]=invalid==0?11:2;
        Check(!WePlayNativeGraphicsConfigureOverlay(wrong),"overlay Config12 header/version/bool exact");}
    water_received=false;Check(!WePlayNativeGraphicsConfigureOverlay(overlay_config),"overlay rejects absent admitted palette");water_received=true;
    overlay_config[2]=0;Check(WePlayNativeGraphicsConfigureOverlay(overlay_config)==1&&!overlay_requested,"overlay explicitdisable queues no renderer work");
    overlay_received=overlay_requested=overlay_input_safe=overlay_palette_admitted=false;
    palette.selected_variant=2;palette.program_paths[0][0]=L'x';
    Check(water_pending_palette.selected_variant==0&&water_pending_palette.program_paths[0][0]!=L'x',"water queue owns all path/selector bytes");
    State current{};state=&current;stats.attached=1;NativeGraphicsCapture::Request request{sizeof(request),1,901};
    Check(WePlayNativeGraphicsRequestCapture(&request)==1&&capture_pending==901,"capture can queue immediately after waterACK while pending");
    Check(!CaptureEpochReady(),"pendingwater cannot capture previous shader/frame");
    water_waiting=false;one.render_width=1280;one.render_height=720;
    Check(!WePlayNativeGraphicsConfigureWater(&one),"configured protocol cannot cross frompalette2 tolegacy1");water_waiting=true;
    ApplyPendingWater();Check(!water_waiting&&!capture_pending&&stats.capture_failures==1,"unpinned palette files fail atomically and cancel capture");
    Check(WaterShaderEffects::Shutdown(),"water failed-preload CPU cleanup");water_initialized=water_received=false;
    WaterShaderEffects::Stats frame{};frame.initialized=frame.configured=1;frame.config_version=2;frame.selected_variant=1;
    frame.configuration_epoch=2;frame.requested=frame.enabled=1;frame.completed_frame=frame.last_applied_frame=frame.last_restored_frame=9;
    frame.completed_drawn_variant=1;frame.completed_applications=frame.completed_restorations=frame.completed_successful_draws=2;
    Check(WaterPaletteFrameReady(frame,109,100,true),"actualwater selected/successful/restored frame maps nonzero origin");
    water_state=frame;water_frame_origin=100;stats.present_calls=109;stats.effects_frame_has_geometry=1;
    auto overlay_status=CurrentOverlayStatus();Check(overlay_status.applied_valid&&overlay_status.applied_enabled&&overlay_status.applied_variant==1,
        "overlay actual label requires successful current River proof");
    water_state.completed_drawn_variant=MAXDWORD;water_state.completed_frame=0;
    overlay_status=CurrentOverlayStatus();Check(!overlay_status.applied_valid&&!overlay_status.applied_enabled&&overlay_status.applied_variant==0&&overlay_status.pending,
        "unproved/menu overlay status never passes MAXDWORD variant to renderer");
    for(unsigned bad_field=0;bad_field<12;++bad_field){auto wrong=frame;
        switch(bad_field){case 0:wrong.completed_drawn_variant=0;break;case 1:wrong.completed_frame=8;break;
        case 2:wrong.configuration_epoch=0;break;case 3:wrong.completed_successful_draws=0;break;
        case 4:wrong.completed_failed_draws=1;break;case 5:wrong.completed_restorations=1;break;
        case 6:wrong.pending_original_refs=1;break;case 7:wrong.unsafe_state=1;break;case 8:wrong.shader_failures=1;break;
        case 9:wrong.cached_refs=1;break;case 10:wrong.last_restored_frame=8;break;case 11:wrong.selected_variant=3;break;}
        Check(!WaterPaletteFrameReady(wrong,109,100,true),"stale/mismatched/unsafe/failed water frame refuses");}
    frame.enabled=frame.requested=0;frame.completed_drawn_variant=MAXDWORD;frame.completed_applications=frame.completed_restorations=frame.completed_successful_draws=0;
    Check(WaterPaletteFrameReady(frame,109,100,true)&&!WaterPaletteFrameReady(frame,109,100,false),"OFF is fresh original geometry with no drawn program");
    Check(!WaterPaletteFrameReady(frame,MAXDWORD,MAXDWORD-2,true),"normalized water frame overflow refuses");
    state=nullptr;water_initialized=true;original_deinit_d3d=OriginalD3D;original_deinit_screen=OriginalScreen;
    const auto previous_d3d=original_d3d_calls,previous_screen=original_screen_calls;
    HookDeinitD3D();Check(!HookDeinitScreen(nullptr)&&original_d3d_calls==previous_d3d&&original_screen_calls==previous_screen,
        "missinghost epoch cannot forward original teardown with livewater module");
    water_initialized=water_received=water_waiting=false;water_pending_version=1;capture_pending=0;ResetHost();
}
void OverlayProfilesProof(){
    using namespace SupersamplingHook;using P=ShaderDebugOverlay::Profile;
    std::puts("CPU: immutable single-family overlay profiles");
    for(DWORD family:{2u,3u,4u,5u}){
        ResetHost();overlay_received=overlay_requested=overlay_extended=false;overlay_profile=P::Water;comparison_waiting=false;
        effects_received=shadow_received=ambient_received=water_received=false;
        if(family==2){native_configuration.flags=NativeGraphicsConfig::Observe|NativeGraphicsConfig::Vegetation;effects_received=true;effects_pending_version=4;}
        if(family==3){native_configuration.flags=NativeGraphicsConfig::Observe|NativeGraphicsConfig::Terrain;effects_received=true;effects_pending_version=2;
            effects_pending_v2.terrain_detail_scale=effects_pending_v2.terrain_relief_scale=1.0f;
            const wchar_t* unused=L"C:\\owned\\MaterialShaders\\";
            for(unsigned index=0;index<2;++index){const auto slot=3+index;const auto& spec=NativeEffectsContractV2::MacroPrograms[slot];
                const auto path=std::wstring(unused)+spec.directory+L"\\"+NativeEffectsContractV2::Digest(slot,true)+spec.suffix;
                if(index==0)wcscpy_s(effects_pending_v2.bump_vertex_path,path.c_str());else wcscpy_s(effects_pending_v2.parallax_vertex_path,path.c_str());}}
        if(family==4){native_configuration.flags=NativeGraphicsConfig::Observe|NativeGraphicsConfig::StableShadows;shadow_received=true;}
        if(family==5){native_configuration.flags=NativeGraphicsConfig::Observe|NativeGraphicsConfig::AmbientOcclusion;ambient_received=true;}
        DWORD request[]={16,2,1,family};Check(WePlayNativeGraphicsConfigureOverlay(request)==1&&static_cast<DWORD>(overlay_profile)==family,
            "pending matching admitted family accepts exact V2 before device creation");
        request[3]=family==2?3u:2u;Check(!WePlayNativeGraphicsConfigureOverlay(request),"family cannot change in a running profile");
        request[3]=family;request[2]=0;Check(WePlayNativeGraphicsConfigureOverlay(request)==1&&!overlay_requested,"matching V2 can hide safely");
        MaterialComparison::Config comparison{};comparison.enabled=1;
        Check(!WePlayNativeGraphicsConfigureComparison(&comparison),"strict single-family profile rejects later UV comparison enable");
        request[2]=2;Check(!WePlayNativeGraphicsConfigureOverlay(request),"V2 enabled remains normalized boolean");
        request[2]=1;request[3]=6;Check(!WePlayNativeGraphicsConfigureOverlay(request),"unknown family refused");
        overlay_received=false;stats.comparison_requested=1;request[3]=family;
        Check(!WePlayNativeGraphicsConfigureOverlay(request),"strict profile rejects existing comparison mutation");stats.comparison_requested=0;
        if(family==3){effects_pending_v2.terrain_detail_scale=1.2f;Check(!OverlayProfileAllowed(P::Terrain),"Macro label refuses nonunit parameter identity");effects_pending_v2.terrain_detail_scale=1.0f;
            effects_pending_v2.bump_vertex_path[0]=0;Check(!OverlayProfileAllowed(P::Terrain),"Macro label refuses historical or missing Macro VS identity");}
        native_configuration.flags|=NativeGraphicsConfig::Water;
        request[3]=family;Check(!WePlayNativeGraphicsConfigureOverlay(request),"combined family intent cannot claim isolated profile");
    }
    ResetHost();overlay_received=true;overlay_profile=P::Terrain;effects_faulted=effects_waiting=false;
    stats.effects_config_version=2;stats.effects_active=2;stats.effects_revision=3;stats.present_calls=4;
    RecordPaletteDraw(0,S_OK);LatchEffectsDraw();CompletePaletteFrame();++stats.present_calls;
    auto status=CurrentOverlayStatus();Check(!status.applied_valid&&status.pending,"generic geometry cannot claim actual Terrain application");
    RecordPaletteDraw(2,S_OK);LatchEffectsDraw(2);CompletePaletteFrame();++stats.present_calls;
    status=CurrentOverlayStatus();Check(status.applied_valid&&status.applied_enabled&&status.applied_variant==0,"safe successful actual Terrain role qualifies current frame");
    RecordPaletteDraw(2,S_OK);RecordPaletteDraw(3,D3DERR_INVALIDCALL);LatchEffectsDraw(2);CompletePaletteFrame();++stats.present_calls;
    Check(!CurrentOverlayStatus().applied_valid,"failed Terrain role disqualifies actual display despite other successful geometry");
    stats.effects_active=0;LatchEffectsDraw();CompletePaletteFrame();++stats.present_calls;
    Check(CurrentOverlayStatus().applied_valid&&!CurrentOverlayStatus().applied_enabled,"Terrain Original requires fresh geometry and no active replacement");
    overlay_received=overlay_requested=overlay_extended=false;overlay_profile=P::Water;effects_received=shadow_received=ambient_received=false;
    overlay_terrain_success=overlay_terrain_failed=0;overlay_terrain_revision=0;effects_pending_version=1;ResetHost();
}
void CombinedProof(const std::wstring& directory){
    using namespace SupersamplingHook;using P=ShaderDebugOverlay::Profile;
    std::puts("CPU: additive combined initialization, tuple and independent proof");ResetHost();
    NativeGraphicsConfig::Version3 init{};init.base.base={sizeof(init),3,1280,720,640,360,{}};
    init.base.flags=NativeGraphicsConfig::KnownFlags;
    const auto root=std::filesystem::path(directory)/L"v5-dispatch";std::filesystem::create_directories(root);
    wcscpy_s(init.base.base.report_path,(root/L"hook.private.json").c_str());
    wcscpy_s(init.base.catalog_path,(root/L"catalog.bin").c_str());init.base.catalog_sha256[0]=1;
    wcscpy_s(init.base.observer_report_path,(root/L"observer.private.json").c_str());
    NativeGraphicsConfig::Version2 normalized{};bool admitted=false;
    Check(NativeGraphicsConfig::DecodeCombined(&init,sizeof(init),63,normalized,admitted)==NativeGraphicsConfig::Error::None&&admitted
        &&normalized.base.size==3132&&normalized.base.version==2&&normalized.flags==63,"init3 scope1 normalizes only admitted complete3136 payload");
    for(unsigned bad=0;bad<6;++bad){auto wrong=init;
        if(bad==0)wrong.diagnostic_scope=0;if(bad==1)wrong.diagnostic_scope=2;if(bad==2)wrong.base.flags&=~1u;
        if(bad==3)wrong.base.base.size=3132;if(bad==4)wrong.base.flags|=64;if(bad==5)wrong.base.base.render_width=640;
        Check(NativeGraphicsConfig::DecodeCombined(&wrong,sizeof(wrong),63,normalized,admitted)!=NativeGraphicsConfig::Error::None&&!admitted,
            "malformed combined scope cannot grant combined authorization");}
    Check(NativeGraphicsConfig::Decode(&init,sizeof(init),63,normalized)!=NativeGraphicsConfig::Error::None,"old decoder never admits new scope implicitly");
    Check(NativeGraphicsConfig::DecodeCombined(&init,sizeof(init)-4,63,normalized,admitted)!=NativeGraphicsConfig::Error::None&&!admitted,"truncated scope refuses");
    native_configuration=init.base;native_configuration.base.size=3132;native_configuration.base.version=2;configuration=native_configuration.base;
    MaterialShaderConfigV5::Config request{};request.render_width=1280;request.render_height=720;
    NativeEffectsContractV5::Resolved resolved{};
    Check(NativeEffectsContractV5::Resolve(configuration.report_path,NativeEffectsContractV5::Layout::PrivateCase,resolved,false)==NativeEffectsContractV5::Error::None,
        "V5 derives only seven closed canonical resources from native admitted report root");
    for(const auto& path:resolved.program_paths){std::filesystem::create_directories(std::filesystem::path(path).parent_path());
        const DWORD invalid[]={0xffff0300,0xffff};std::ofstream file(path,std::ios::binary);file.write(reinterpret_cast<const char*>(invalid),sizeof(invalid));}
    effects_received=effects_waiting=effects_initialized=effects_faulted=false;overlay_received=false;
    Check(!WePlayNativeGraphicsConfigureEffects(&request)&&!effects_waiting,"V5 cannot queue without explicit combined initialization");
    combined_scope_authorized=true;
    Check(ShadowIntentAuthorized(63)&&AmbientIntentAuthorized(63),"explicit new scope permits both exact disjoint shader families");
    for(unsigned bad=0;bad<5;++bad){auto wrong=request;if(bad==0)wrong.flags=1;if(bad==1)wrong.selected_variant=3;
        if(bad==2)wrong.render_width=640;if(bad==3)wrong.size=4120;if(bad==4)wrong.version=6;
        Check(!WePlayNativeGraphicsConfigureEffects(&wrong)&&!effects_waiting,"V5 invalid compact tuple refuses before queue mutation");}
    Check(WePlayNativeGraphicsConfigureEffects(&request)==1&&effects_pending_version==5&&!effects_initialized,"24B initialOFF queues only at safe boundary");
    request.selected_variant=2;Check(effects_pending_v5.selected_variant==0,"queued tuple owns copied caller data");
    Check(!WePlayNativeGraphicsConfigureEffects(&request),"pending V5 command refuses replacement");effects_waiting=false;
    water_received=true;water_pending_version=2;water_pending_palette={};water_pending_palette.render_width=1280;water_pending_palette.render_height=720;
    for(unsigned i=0;i<3;++i){std::wstring hash;for(const char* at=WaterShaderPaletteContract::Digests[i];*at;++at)hash+=static_cast<wchar_t>(*at);
        const auto path=root/L"LocalData"/L"MaterialShaders"/WaterShaderPaletteContract::Recipes[i]/(hash+L".ps.bin");wcscpy_s(water_pending_palette.program_paths[i],path.c_str());}
    shadow_received=ambient_received=true;shadow_pending_config={};ambient_pending_config={};ambient_pending_config.version=2;
    overlay_received=overlay_extended=false;overlay_profile=P::Water;
    DWORD overlay[]={16,3,1,6};Check(WePlayNativeGraphicsConfigureOverlay(overlay)==1&&overlay_profile==P::All,"combined overlay accepts all pre-device admitted queues");
    Check(!WePlayNativeGraphicsConfigureEffects(&request)&&!WePlayNativeGraphicsConfigureWater(&water_pending_palette)
        &&!WePlayNativeGraphicsConfigureShadow(&shadow_pending_config)&&!WePlayNativeGraphicsConfigureAmbient(&ambient_pending_config),
        "armed combined UI is the sole mutation owner so persisted choices cannot drift via external selectors");
    Check(OverlayPresetJson().find("overlay_preset_revision")!=std::string::npos,"report carries atomic five-choice preset and monotonic revision");
    overlay[1]=2;Check(!WePlayNativeGraphicsConfigureOverlay(overlay),"old overlay2 cannot request combined family");overlay[1]=3;
    overlay[3]=2;Check(!WePlayNativeGraphicsConfigureOverlay(overlay),"overlay3 admits only combined family6");overlay[3]=6;
    ShaderDebugOverlay::Preset preset{};
    effects_loaded_v5=request;
    for(DWORD terrain=0;terrain<2;++terrain)for(DWORD leaf=0;leaf<2;++leaf)for(DWORD variant=0;variant<3;++variant){
        preset.values[1].enabled=leaf;preset.values[1].variant=variant;preset.values[2].enabled=terrain;
        const auto tuple=CombinedEffectsSelection(preset);
        Check(tuple.flags==(leaf*4+terrain*2)&&tuple.selected_variant==variant&&tuple.render_width==1280,
            "independent Leaf/Terrain tuple preserves other group for every bounded choice");}
    effects_waiting=effects_faulted=false;effects_initialized=true;stats.effects_configured=1;stats.effects_config_version=5;
    stats.effects_selected_variant=1;stats.effects_active=6;stats.effects_revision=9;stats.present_calls=10;InvalidatePaletteFrame();
    RecordPaletteDraw(2,S_OK);LatchEffectsDraw(2);CompletePaletteFrame();++stats.present_calls;
    Check(CurrentOverlayStatusFor(P::Terrain).applied_valid&&!CurrentOverlayStatusFor(P::Leaf).applied_valid,
        "actual Terrain proof independent of missing visible Leaf pass");
    RecordPaletteDraw(4,S_OK);LatchEffectsDraw(4);CompletePaletteFrame();++stats.present_calls;
    Check(CurrentOverlayStatusFor(P::Leaf).applied_valid&&!CurrentOverlayStatusFor(P::Terrain).applied_valid,
        "actual Leaf proof does not label missing Terrain pass applied");
    RecordPaletteDraw(2,S_OK);RecordPaletteDraw(4,S_OK);LatchEffectsDraw(4);CompletePaletteFrame();++stats.present_calls;
    Check(CurrentOverlayStatusFor(P::Leaf).applied_valid&&CurrentOverlayStatusFor(P::Terrain).applied_valid,"both actual restored families qualify same frame");
    InvalidatePaletteFrame();Check(!CurrentOverlayStatusFor(P::Leaf).applied_valid&&!CurrentOverlayStatusFor(P::Terrain).applied_valid,"config/reset invalidation clears both copied proofs");
    overlay_received=overlay_requested=overlay_extended=false;overlay_profile=P::Water;effects_received=effects_waiting=effects_initialized=effects_faulted=false;
    water_received=water_waiting=water_initialized=false;effects_pending_version=1;shadow_received=ambient_received=false;ResetHost();
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
    EffectsV3Proof(directory);
    EffectsV4Proof(directory);
    WaterPaletteProof(directory);
    OverlayProfilesProof();
    CombinedProof(directory);
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
