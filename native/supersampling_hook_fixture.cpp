// Reuse the independent reference pattern, runtime hash lock, capture and BMP
// helpers; this executable tests the actual COM hook instead of calling blits.
#define wmain SupersamplingReferenceMain
#include "supersampling_probe.cpp"
#undef wmain
#include "supersampling_hook.h"
#include "supersampling_hook_capture.h"
#include "supersampling_hook_config.h"
#include "material_shader_observer.h"
#include <memory>

namespace {
D3DPRESENT_PARAMETERS Parameters(HWND window,UINT width,UINT height) {
    D3DPRESENT_PARAMETERS value{};
    value.Windowed=TRUE;value.SwapEffect=D3DSWAPEFFECT_DISCARD;
    value.hDeviceWindow=window;value.BackBufferCount=1;
    value.BackBufferWidth=width;value.BackBufferHeight=height;
    value.BackBufferFormat=D3DFMT_A8R8G8B8;
    value.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    return value;
}
IDirect3DDevice9* fixture_device=nullptr;
WNDPROC fixture_previous=nullptr;
HRESULT resize_reset_result=E_FAIL;
POINT observed_mouse{};
POINT observed_size{};
LPARAM observed_minimize=-1;
bool inspect_destroy_device=false,original_destroy_device_usable=false;
constexpr UINT FixtureMessage=WM_APP+73;

LRESULT CALLBACK FixtureProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_DESTROY && inspect_destroy_device && fixture_device) {
        D3DCAPS9 caps{};original_destroy_device_usable=SUCCEEDED(fixture_device->GetDeviceCaps(&caps));
    }
    if(message==WM_SIZE && fixture_device) {
        if(wparam==SIZE_MINIMIZED){observed_minimize=lparam;return 0;}
        observed_size={LOWORD(lparam),HIWORD(lparam)};
        RECT logical{};
        if(!WePlaySupersamplingFixtureRect(window,&logical,reinterpret_cast<const void*>(FixtureProcedure)))return 0;
        // This models S7's measured resize behavior: the original handler asks
        // for its client rectangle, then resets its actual scene/viewport.
        auto parameters=Parameters(window,static_cast<UINT>(logical.right),static_cast<UINT>(logical.bottom));
        resize_reset_result=fixture_device->Reset(&parameters);
        return 0;
    }
    if(message==WM_MOUSEMOVE) {
        observed_mouse={static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};return 0;
    }
    if(message==FixtureMessage)return lparam;
    return fixture_previous?CallWindowProcW(fixture_previous,window,message,wparam,lparam)
        :DefWindowProcW(window,message,wparam,lparam);
}

void VerifyLogicalContext(HWND window) {
    const void* known=reinterpret_cast<const void*>(FixtureProcedure);
    const void* foreign=reinterpret_cast<const void*>(GetProcAddress(GetModuleHandleW(L"d3d9.dll"),"Direct3DCreate9"));
    RECT physical{},logical{},forwarded{};
    Require(GetClientRect(window,&physical) && physical.right==640 && physical.bottom==360,"fixture_physical_size_changed");
    Require(WePlaySupersamplingFixtureRect(window,&logical,known) && logical.right==1280 && logical.bottom==720,
        "fixture_logical_rect_failed");
    SetLastError(0x7357);Require(GetClientRect(window,&physical)!=FALSE,"fixture_real_rect_error_baseline_failed");
    const DWORD real_rect_error=GetLastError();SetLastError(0x7357);
    Require(WePlaySupersamplingFixtureRect(window,&logical,known) && GetLastError()==real_rect_error,"fixture_logical_rect_last_error_changed");
    RECT invalid{};SetLastError(0x7357);const BOOL actual_failed=GetClientRect(nullptr,&invalid);
    const DWORD failure_error=GetLastError();SetLastError(0x7357);
    Require(!actual_failed && !WePlaySupersamplingFixtureRect(nullptr,&invalid,known) && GetLastError()==failure_error,
        "fixture_real_rect_failure_error_masked");
    Require(WePlaySupersamplingFixtureRect(window,&forwarded,foreign) && forwarded.right==640 && forwarded.bottom==360,
        "fixture_foreign_caller_was_virtualized");
    HWND other=CreateWindowExW(0,WindowClass,L"Unmodified fixture window",WS_POPUP,0,0,200,100,
        nullptr,nullptr,GetModuleHandleW(nullptr),nullptr);
    Require(other!=nullptr,"fixture_other_window_failed");
    RECT other_rect{};
    const bool untouched=WePlaySupersamplingFixtureRect(other,&other_rect,known) && other_rect.right==200 && other_rect.bottom==100;
    DestroyWindow(other);Require(untouched,"fixture_other_window_was_virtualized");
    POINT origin{};Require(ClientToScreen(window,&origin)!=FALSE,"fixture_origin_failed");
    POINT to_client{origin.x+80,origin.y+40};
    Require(WePlaySupersamplingFixturePoint(window,&to_client,known,FALSE) && to_client.x==160 && to_client.y==80,
        "fixture_screen_to_logical_failed");
    Require(WePlaySupersamplingFixturePoint(window,&to_client,known,TRUE)
        && to_client.x==origin.x+80 && to_client.y==origin.y+40,"fixture_logical_to_screen_failed");
    POINT foreign_point{origin.x+80,origin.y+40};
    Require(WePlaySupersamplingFixturePoint(window,&foreign_point,foreign,FALSE) && foreign_point.x==80 && foreign_point.y==40,
        "fixture_foreign_point_was_virtualized");
    SendMessageW(window,WM_MOUSEMOVE,0,MAKELPARAM(80,40));
    Require(observed_mouse.x==160 && observed_mouse.y==80,"fixture_mouse_message_not_mapped");
    SendMessageW(window,WM_SIZE,SIZE_MINIMIZED,0);
    Require(observed_minimize==0,"fixture_minimize_message_changed");
    Require(SendMessageW(window,FixtureMessage,0,0x7357)==0x7357,"fixture_unknown_message_changed");
}

void Upload(IDirect3DDevice9* device,UINT width,UINT height) {
    Com<IDirect3DSurface9> source,backbuffer;
    Check(device->CreateOffscreenPlainSurface(width,height,D3DFMT_A8R8G8B8,
        D3DPOOL_SYSTEMMEM,source.put(),nullptr),"fixture_upload_allocate_failed");
    D3DLOCKED_RECT locked{};
    Check(source->LockRect(&locked,nullptr,0),"fixture_upload_lock_failed");
    for(UINT y=0;y<height;++y) {
        auto* row=reinterpret_cast<std::uint32_t*>(static_cast<unsigned char*>(locked.pBits)
            +static_cast<size_t>(y)*locked.Pitch);
        for(UINT x=0;x<width;++x)row[x]=Pattern(x,y,width,height);
    }
    Check(source->UnlockRect(),"fixture_upload_unlock_failed");
    Check(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,backbuffer.put()),"fixture_backbuffer_failed");
    Check(device->UpdateSurface(source.get(),nullptr,backbuffer.get(),nullptr),"fixture_pattern_upload_failed");
}

void ExerciseDraw(IDirect3DDevice9* device,UINT width,UINT height) {
    struct Vertex {float x,y,z,w;DWORD color;};
    const Vertex vertices[]={{0,0,0,1,0xffffffff},{0,static_cast<float>(height),0,1,0xffffffff},
        {static_cast<float>(width),static_cast<float>(height),0,1,0xffffffff}};
    Com<IDirect3DVertexBuffer9> buffer;
    Check(device->CreateVertexBuffer(sizeof(vertices),0,D3DFVF_XYZRHW|D3DFVF_DIFFUSE,
        D3DPOOL_MANAGED,buffer.put(),nullptr),"fixture_vertex_buffer_failed");
    void* data=nullptr;
    Check(buffer->Lock(0,sizeof(vertices),&data,0),"fixture_vertex_lock_failed");
    std::memcpy(data,vertices,sizeof(vertices));
    Check(buffer->Unlock(),"fixture_vertex_unlock_failed");
    Check(device->SetFVF(D3DFVF_XYZRHW|D3DFVF_DIFFUSE),"fixture_fvf_failed");
    Check(device->SetStreamSource(0,buffer.get(),0,sizeof(Vertex)),"fixture_stream_failed");
    Check(device->BeginScene(),"fixture_scene_begin_failed");
    Check(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1),"fixture_draw_failed");
    Check(device->EndScene(),"fixture_scene_end_failed");
    Check(device->SetStreamSource(0,nullptr,0,0),"fixture_stream_release_failed");
}

bool MarkerShader(IDirect3DDevice9* device) {
    // Authored by this project: exact 44-byte ps_3_0 stream generated by
    // tools/material_shaders/shaderlib.py::marker_pixel_shader(). No game code.
    const DWORD marker[]={0xffff0300,0x05000051,0xa00f0000,0x3f800000,0,
        0x3f800000,0x3f800000,0x02000001,0x800f0800,0xa0e40000,0xffff};
    Com<IDirect3DPixelShader9> shader,previous_shader;
    Com<IDirect3DSurface9> target,previous_target;
    Check(device->CreatePixelShader(marker,shader.put()),"marker_shader_create_failed");
    Check(device->GetPixelShader(previous_shader.put()),"marker_previous_shader_failed");
    Check(device->GetRenderTarget(0,previous_target.put()),"marker_previous_target_failed");
    D3DVIEWPORT9 previous_viewport{};
    Check(device->GetViewport(&previous_viewport),"marker_previous_viewport_failed");
    Check(device->CreateRenderTarget(1,1,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,
        target.put(),nullptr),"marker_target_failed");
    Check(device->SetRenderTarget(0,target.get()),"marker_bind_target_failed");
    const D3DVIEWPORT9 viewport{0,0,1,1,0,1};
    Check(device->SetViewport(&viewport),"marker_viewport_failed");
    Check(device->SetPixelShader(shader.get()),"marker_bind_shader_failed");
    Check(device->SetRenderState(D3DRS_ZENABLE,FALSE),"marker_depth_state_failed");
    Check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"marker_cull_state_failed");
    struct Vertex {float x,y,z,w;DWORD color;};
    const Vertex vertices[]={{-.5f,-.5f,0,1,0xffffffff},{1.5f,-.5f,0,1,0xffffffff},{-.5f,1.5f,0,1,0xffffffff}};
    Check(device->BeginScene(),"marker_begin_scene_failed");
    Com<IDirect3DVertexBuffer9> buffer;
    Check(device->CreateVertexBuffer(sizeof(vertices),0,D3DFVF_XYZRHW|D3DFVF_DIFFUSE,D3DPOOL_MANAGED,buffer.put(),nullptr),"marker_vertex_buffer_failed");
    void* data=nullptr;Check(buffer->Lock(0,sizeof(vertices),&data,0),"marker_vertex_lock_failed");
    std::memcpy(data,vertices,sizeof(vertices));Check(buffer->Unlock(),"marker_vertex_unlock_failed");
    Check(device->SetStreamSource(0,buffer.get(),0,sizeof(Vertex)),"marker_stream_failed");
    Check(device->DrawPrimitive(D3DPT_TRIANGLELIST,0,1),"marker_draw_failed");
    Check(device->SetStreamSource(0,nullptr,0,0),"marker_stream_clear_failed");
    Check(device->EndScene(),"marker_end_scene_failed");
    const auto pixels=Readback(device,target.get(),1,1);
    Check(device->SetPixelShader(previous_shader.get()),"marker_restore_shader_failed");
    Check(device->SetRenderTarget(0,previous_target.get()),"marker_restore_target_failed");
    Check(device->SetViewport(&previous_viewport),"marker_restore_viewport_failed");
    Require((pixels[0]&0xffffff)==0xff00ff,"marker_gpu_pixel_not_magenta");
    return true;
}

double Verify(IDirect3DDevice9* device,HWND window,UINT width,UINT height,UINT high_width,UINT high_height,
              const std::filesystem::path& output,const char* label) {
    Upload(device,high_width,high_height);
    Check(device->Present(nullptr,nullptr,nullptr,nullptr),"hook_fixture_present_failed");
    MSG message{};
    while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
    Check(DwmFlush(),"hook_fixture_compositor_failed");Sleep(100);
    const auto pixels=Capture(window,width,height);
    double error=0;
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x) {
        const double sx=(static_cast<double>(x)+.5)*high_width/width-.5;
        const double sy=(static_cast<double>(y)+.5)*high_height/height-.5;
        const UINT left=static_cast<UINT>(std::floor(sx)),top=static_cast<UINT>(std::floor(sy));
        const double fx=sx-left,fy=sy-top;
        for(unsigned shift:{0u,8u,16u}) {
            const auto channel=[&](UINT px,UINT py){return static_cast<double>((Pattern(px,py,high_width,high_height)>>shift)&255);};
            const double expected=channel(left,top)*(1-fx)*(1-fy)+channel(left+1,top)*fx*(1-fy)
                +channel(left,top+1)*(1-fx)*fy+channel(left+1,top+1)*fx*fy;
            error=std::max(error,std::abs(static_cast<double>((pixels[static_cast<size_t>(y)*width+x]>>shift)&255)-expected));
        }
    }
    if(error>3)std::fprintf(stderr,"fixture=%s max_error=%.3f\n",label,error);
    Require(error<=3,"hook_output_does_not_match_complete_reference");
    Bitmap(output/(std::string(label)+"-output.bmp"),pixels,width,height);
    return error;
}
}

int wmain(int argc,wchar_t** argv) {
    try {
        Require(argc>=3 && argc<=6,"usage_hook_fixture_dxvk_output_optional_catalog_sha_and_shutdown_mode");
        const bool observe=argc==5 || argc==6;
        const wchar_t* shutdown=(argc==4?argv[3]:(argc==6?argv[5]:L"--held-parent"));
        const bool last_parent=!std::wcscmp(shutdown,L"--last-parent"),engine_boundary=!std::wcscmp(shutdown,L"--engine-deinit");
        Require(last_parent || engine_boundary || !std::wcscmp(shutdown,L"--held-parent"),"fixture_unknown_shutdown_mode");
        const auto output=std::filesystem::absolute(argv[2]);
        Require(!std::filesystem::exists(output) && std::filesystem::create_directory(output),"use_new_fixture_output_directory");
        SetProcessDPIAware();Runtime runtime(std::filesystem::absolute(argv[1]));
        Com<IDirect3D9> api;*api.put()=runtime.create();Require(api.get()!=nullptr,"fixture_factory_failed");
        // Model the actual Ubi startup: Windows clips the physical client
        // before the engine handles its first WM_SIZE or geometry draw.
        Window window(1280,600);window.show();
        auto parameters=Parameters(window.get(),1280,720);
        Com<IDirect3DDevice9> device;
        Check(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window.get(),D3DCREATE_HARDWARE_VERTEXPROCESSING,
            &parameters,device.put()),"fixture_device_failed");
        SupersamplingConfig config{sizeof(SupersamplingConfig),1,1280,720,640,360,{}};
        const auto report=(output/L"hook-state.json").wstring();
        Require(report.size()<512,"fixture_report_path_too_long");
        std::copy(report.begin(),report.end(),config.report_path);
        NativeGraphicsConfig::Version2 version2{};
        if(observe) {
            version2.base=config;version2.base.size=sizeof(version2);version2.base.version=2;
            version2.flags=NativeGraphicsConfig::Observe;
            const std::wstring catalog=std::filesystem::absolute(argv[3]).wstring(),observer_report=(output/L"observer.json").wstring();
            Require(catalog.size()<512 && observer_report.size()<512 && std::wcslen(argv[4])==64,"fixture_observer_paths_or_sha_invalid");
            std::copy(catalog.begin(),catalog.end(),version2.catalog_path);
            std::copy(observer_report.begin(),observer_report.end(),version2.observer_report_path);
            const auto nibble=[](wchar_t c)->unsigned {
                if(c>=L'0' && c<=L'9')return static_cast<unsigned>(c-L'0');
                if(c>=L'a' && c<=L'f')return static_cast<unsigned>(c-L'a'+10);
                if(c>=L'A' && c<=L'F')return static_cast<unsigned>(c-L'A'+10);
                throw std::runtime_error("fixture_observer_sha_invalid");
            };
            for(size_t i=0;i<32;++i)version2.catalog_sha256[i]=static_cast<unsigned char>(nibble(argv[4][i*2])*16+nibble(argv[4][i*2+1]));
        }
        Require(WePlaySupersamplingInitialize(observe?static_cast<void*>(&version2):static_cast<void*>(&config))==1,"fixture_initialize_failed");
        fixture_previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window.get(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(FixtureProcedure)));
        Require(fixture_previous!=nullptr,"fixture_original_window_procedure_failed");
        fixture_device=device.get();
        Require(WePlaySupersamplingAttach(device.get())==1,"fixture_attach_failed");
        SupersamplingStats startup{};
        Require(WePlaySupersamplingReadStats(&startup)==1 && startup.resize_queued==1
            && startup.high_resolution_draws==0,"fixture_startup_transition_waited_for_geometry");
        const void* startup_known=reinterpret_cast<const void*>(FixtureProcedure);
        const void* startup_foreign=reinterpret_cast<const void*>(GetProcAddress(GetModuleHandleW(L"d3d9.dll"),"Direct3DCreate9"));
        RECT startup_rect{};
        Require(WePlaySupersamplingFixtureRect(window.get(),&startup_rect,startup_known)
            && startup_rect.right==1280 && startup_rect.bottom==720,"fixture_startup_logical_rect_failed");
        Require(WePlaySupersamplingFixtureRect(window.get(),&startup_rect,startup_foreign)
            && startup_rect.right==1280 && startup_rect.bottom==600,"fixture_startup_foreign_caller_changed");
        POINT startup_origin{};Check(ClientToScreen(window.get(),&startup_origin)?S_OK:E_FAIL,"fixture_startup_origin_failed");
        POINT startup_point{startup_origin.x+80,startup_origin.y+40};
        Require(WePlaySupersamplingFixturePoint(window.get(),&startup_point,startup_known,FALSE)
            && startup_point.x==80 && startup_point.y==40,"fixture_startup_input_was_scaled_before_output");
        SendMessageW(window.get(),WM_SIZE,SIZE_MINIMIZED,0);
        Require(observed_minimize==0,"fixture_startup_minimize_changed");
        SendMessageW(window.get(),WM_SIZE,SIZE_RESTORED,MAKELPARAM(123,456));
        Require(observed_size.x==123 && observed_size.y==456,"fixture_startup_unknown_size_was_mapped");
        SendMessageW(window.get(),WM_SIZE,SIZE_RESTORED,MAKELPARAM(1280,600));
        Require(observed_size.x==1280 && observed_size.y==720,"fixture_startup_clipped_size_not_logical");
        Check(resize_reset_result,"fixture_startup_original_reset_failed");
        Require(WePlaySupersamplingReadStats(&startup)==1 && startup.high_resolution_draws==0
            && startup.last_reset_requested_width==1280 && startup.last_reset_requested_height==720
            && startup.target_width==1280 && startup.target_height==720,
            "fixture_startup_engine_scene_shrank_before_first_draw");
        MSG transition{};
        while(PeekMessageW(&transition,nullptr,0,0,PM_REMOVE)){TranslateMessage(&transition);DispatchMessageW(&transition);}
        RECT transitioned{};
        Require(GetClientRect(window.get(),&transitioned) && transitioned.right==640 && transitioned.bottom==360,
            "fixture_owned_window_transition_failed");
        Check(resize_reset_result,"fixture_logical_wm_size_reset_failed");
        Require(observed_size.x==1280 && observed_size.y==720,"fixture_wm_size_not_logical_render_size");
        VerifyLogicalContext(window.get());
        ExerciseDraw(device.get(),1280,720);
        const bool marker=MarkerShader(device.get());
        const double first=Verify(device.get(),window.get(),640,360,1280,720,output,"hook-2x");
        SupersamplingStats cache_before{},cache_after{};
        Require(WePlaySupersamplingReadStats(&cache_before)==1,"fixture_cache_before_failed");
        for(unsigned i=0;i<1000;++i){device->AddRef();device->Release();}
        Require(WePlaySupersamplingReadStats(&cache_after)==1
            && cache_after.swapchain_creations==cache_before.swapchain_creations
            && cache_after.cached_release_calls>=1000,"fixture_temporary_releases_dropped_cache");
        NativeGraphicsCapture::Request capture{sizeof(capture),1,1};
        Require(WePlayNativeGraphicsRequestCapture(&capture)==1,"fixture_capture_request_failed");
        Require(WePlayNativeGraphicsRequestCapture(&capture)==0,"fixture_duplicate_capture_request_accepted");
        const double retained=Verify(device.get(),window.get(),640,360,1280,720,output,"hook-retained-cache");
        SupersamplingStats cache_present{};
        Require(WePlaySupersamplingReadStats(&cache_present)==1
            && cache_present.swapchain_creations==cache_before.swapchain_creations
            && cache_present.capture_completed==1 && cache_present.capture_failures==0,
            "fixture_retained_present_recreated_swapchain");
        if(observe) {
            auto observations=std::make_unique<MaterialShaderObserver::Stats>();
            Require(MaterialShaderObserver::ReadStats(observations.get()) && observations->enabled
                && observations->frames>=2 && observations->shader_reads>=1 && observations->cached_refs==0,
                "fixture_real_gpu_observer_callbacks_failed");
        }
        NativeGraphicsCapture::Request rejected_capture{sizeof(rejected_capture),1,2};
        Require(WePlayNativeGraphicsRequestCapture(&rejected_capture)==1 && WePlaySupersamplingFixtureFailPresent()==1,
            "fixture_failed_present_capture_setup_failed");
        Upload(device.get(),1280,720);
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"fixture_fallback_original_present_failed");
        // Verify the extended interface did not fall off a truncated clone.
        Com<IDirect3DDevice9Ex> extended;
        const HRESULT extended_result=device->QueryInterface(__uuidof(IDirect3DDevice9Ex),reinterpret_cast<void**>(extended.put()));
        Require(SUCCEEDED(extended_result) || extended_result==E_NOINTERFACE,"fixture_extended_query_failed");
        const bool extended_tested=SUCCEEDED(extended_result);
        if(extended_tested) {
            D3DDISPLAYMODEEX display{};display.Size=sizeof(display);D3DDISPLAYROTATION rotation{};
            Check(extended->GetDisplayModeEx(0,&display,&rotation),"fixture_extended_method_failed");
        }
        extended.reset();
        parameters=Parameters(window.get(),1280,720);
        Check(device->Reset(&parameters),"fixture_reset_failed");
        const double after_reset=Verify(device.get(),window.get(),640,360,1280,720,output,"hook-after-reset");
        // A real low-res Reset remains low-res and is explicitly rejected by
        // the hook; no fabricated high-resolution scene qualification.
        parameters=Parameters(window.get(),640,360);
        Check(device->Reset(&parameters),"fixture_low_reset_failed");
        Upload(device.get(),640,360);
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"fixture_low_fallback_failed");
        SupersamplingStats low{};
        Require(WePlaySupersamplingReadStats(&low)==1 && low.last_failure==7,"fixture_low_source_not_rejected");
        parameters=Parameters(window.get(),1280,720);
        Check(device->Reset(&parameters),"fixture_recovery_reset_failed");
        const double recovered=Verify(device.get(),window.get(),640,360,1280,720,output,"hook-recovered");
        const void* valid_caller=reinterpret_cast<const void*>(FixtureProcedure);
        const void* foreign_caller=reinterpret_cast<const void*>(GetProcAddress(GetModuleHandleW(L"d3d9.dll"),"Direct3DCreate9"));
        Require(WePlaySupersamplingFixturePrepareDeinit(valid_caller,GetCurrentThreadId()+1)==0
            && WePlaySupersamplingFixturePrepareDeinit(foreign_caller,GetCurrentThreadId())==0,
            "fixture_invalid_engine_deinit_context_accepted");
        if(engine_boundary)Require(WePlaySupersamplingFixturePrepareDeinit(valid_caller,GetCurrentThreadId())==1,
            "fixture_guarded_engine_deinit_cleanup_failed");
        inspect_destroy_device=true;
        if(last_parent)device.reset(); // The cached swapchain is now the last public device owner.
        Require(DestroyWindow(window.get())!=FALSE,"fixture_owned_window_destroy_failed");
        SupersamplingStats destroyed{};
        Require(WePlaySupersamplingReadStats(&destroyed)==1 && destroyed.window_shutdowns==(engine_boundary?0u:1u)
            && destroyed.terminal_cleanup==1 && destroyed.detached==1 && original_destroy_device_usable,
            "fixture_original_window_handler_lost_live_device");
        if(!last_parent) {
            D3DCAPS9 remaining_caps{};
            Check(device->GetDeviceCaps(&remaining_caps),"fixture_held_device_invalid_after_cache_shutdown");
            Require(WePlaySupersamplingDetach(device.get())==1,"fixture_idempotent_detach_failed");
        }
        fixture_device=nullptr;
        device.reset();
        SupersamplingStats stats{};
        Require(WePlaySupersamplingReadStats(&stats)==1,"fixture_stats_failed");
        Require(stats.output_present_calls==4 && stats.reset_successes==6 && stats.fallback_calls==2
            && stats.high_resolution_draws>=1 && stats.detached==1 && stats.resize_applied==1,
            "fixture_lifecycle_or_counters_failed");
        std::printf("{\"format\":\"s7-supersampling-hook-fixture-v1\",\"game_tested\":false,"
            "\"hud_input_tested\":false,\"first_error\":%.4f,\"reset_error\":%.4f,\"recovered_error\":%.4f,\"retained_error\":%.4f,"
            "\"source\":[%lu,%lu],\"output\":[%lu,%lu],\"output_present_calls\":%lu,"
            "\"reset_successes\":%lu,\"fallback_calls\":%lu,\"high_resolution_draws\":%lu,"
            "\"hook_detached\":%lu,\"owned_window_resize_applied\":%lu,\"logical_resize_preserved_high_scene\":true,"
            "\"startup_clipped_size_preserved_before_geometry\":true,\"startup_input_unscaled\":true,"
            "\"logical_context_guards_passed\":true,\"logical_point_and_mouse_roundtrip_passed\":true,"
            "\"cache_reuse_passed\":true,\"window_shutdown_cleanup_passed\":true,\"swapchain_creations\":%lu,\"cached_release_calls\":%lu,"
            "\"same_frame_surface_capture_passed\":true,"
            "\"failed_filtered_capture_recorded\":true,\"observer_v2_enabled\":%s,"
            "\"original_destroy_device_usable\":true,\"shutdown_mode\":\"%s\",\"engine_deinit_rejections\":%lu,"
            "\"extended_interface_tested\":%s,\"authored_marker_gpu_magenta\":%s,\"device_loss_tested\":false}\n",first,after_reset,recovered,retained,
            stats.source_width,stats.source_height,stats.output_width,stats.output_height,stats.output_present_calls,
            stats.reset_successes,stats.fallback_calls,stats.high_resolution_draws,stats.detached,stats.resize_applied,
            stats.swapchain_creations,stats.cached_release_calls,
            observe?"true":"false",
            last_parent?"last_parent":(engine_boundary?"engine_deinit":"held_parent"),destroyed.engine_deinit_rejected,
            extended_tested?"true":"false",marker?"true":"false");
        return 0;
    } catch(const std::exception& error){std::fprintf(stderr,"hook_fixture: %s\n",error.what());return 1;}
}
