// Authored-program GPU fixture. Compatibility overrides are compiled only into
// this executable, never the native graphics DLL or a game process.
#define wmain SupersamplingReferenceMain
#include "supersampling_probe.cpp"
#undef wmain
#include "supersampling_hook_config.h"
#include "supersampling_hook_capture.h"
#include "material_shader_effects.h"
#include <d3dcompiler.h>

namespace {
IDirect3DDevice9* live=nullptr;
WNDPROC previous=nullptr;
D3DPRESENT_PARAMETERS Params(HWND window) {
    D3DPRESENT_PARAMETERS p{};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;
    p.BackBufferWidth=1280;p.BackBufferHeight=720;p.BackBufferFormat=D3DFMT_A8R8G8B8;p.BackBufferCount=1;
    p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;return p;
}
LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_SIZE && live && wparam!=SIZE_MINIMIZED) {
        auto p=Params(window);p.BackBufferWidth=LOWORD(lparam);p.BackBufferHeight=HIWORD(lparam);
        live->Reset(&p);return 0;
    }
    return previous?CallWindowProcW(previous,window,message,wparam,lparam):DefWindowProcW(window,message,wparam,lparam);
}
void Pump() {
    MSG message{};while(PeekMessageW(&message,nullptr,0,0,PM_REMOVE)){TranslateMessage(&message);DispatchMessageW(&message);}
}
void Compile(const char* source,const char* profile,Com<ID3DBlob>& output) {
    Com<ID3DBlob> error;
    const HRESULT result=D3DCompile(source,std::strlen(source),"authored-effects-fixture",nullptr,nullptr,"main",profile,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,output.put(),error.put());
    if(FAILED(result) && error.get())std::fprintf(stderr,"%.*s\n",static_cast<int>(error->GetBufferSize()),static_cast<const char*>(error->GetBufferPointer()));
    Check(result,"authored_shader_compile_failed");
}
std::array<unsigned char,32> Digest(ID3DBlob* blob) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> digest{};
    Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"fixture_hash_provider_failed");
    const bool valid=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0
        && BCryptHashData(hash,static_cast<PUCHAR>(blob->GetBufferPointer()),static_cast<ULONG>(blob->GetBufferSize()),0)>=0
        && BCryptFinishHash(hash,digest.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);
    Require(valid,"fixture_hash_failed");return digest;
}
void Save(const std::filesystem::path& path,ID3DBlob* code) {
    Require(!std::filesystem::exists(path),"fixture_shader_file_exists");
    std::ofstream stream(path,std::ios::binary);stream.write(static_cast<const char*>(code->GetBufferPointer()),code->GetBufferSize());
    stream.close();Require(stream.good(),"fixture_shader_write_failed");
}
struct Geometry {
    Com<IDirect3DVertexBuffer9> buffer;
    Com<IDirect3DVertexDeclaration9> declaration;
    explicit Geometry(IDirect3DDevice9* device) {
        const float vertices[][4]={{-1,-1,0,1},{-1,1,0,1},{1,-1,0,1},{1,1,0,1}};
        Check(device->CreateVertexBuffer(sizeof(vertices),0,0,D3DPOOL_MANAGED,buffer.put(),nullptr),"effects_gpu_vb_failed");
        void* data=nullptr;Check(buffer->Lock(0,sizeof(vertices),&data,0),"effects_gpu_vb_lock_failed");
        std::memcpy(data,vertices,sizeof(vertices));Check(buffer->Unlock(),"effects_gpu_vb_unlock_failed");
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
        Check(device->CreateVertexDeclaration(elements,declaration.put()),"effects_gpu_declaration_failed");
    }
    void Bind(IDirect3DDevice9* device) {
        Check(device->SetVertexDeclaration(declaration.get()),"effects_gpu_bind_declaration_failed");
        Check(device->SetStreamSource(0,buffer.get(),0,16),"effects_gpu_bind_stream_failed");
    }
};
constexpr float Fog[4]={.1f,.2f,.3f,.4f},Reflection[4]={.2f,.3f,.4f,.5f};
HRESULT DrawScene(IDirect3DDevice9* device,Geometry& geometry,IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel) {
    geometry.Bind(device);
    Check(device->SetVertexShader(vertex),"effects_gpu_bind_vs_failed");Check(device->SetPixelShader(pixel),"effects_gpu_bind_ps_failed");
    Check(device->SetVertexShaderConstantF(68,Fog,1),"effects_gpu_fog_failed");
    Check(device->SetVertexShaderConstantF(69,Reflection,1),"effects_gpu_reflection_failed");
    Check(device->SetRenderState(D3DRS_ZENABLE,FALSE),"effects_gpu_depth_failed");
    Check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"effects_gpu_cull_failed");
    Check(device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE),"effects_gpu_alpha_failed");
    Check(device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE),"effects_gpu_srgb_failed");
    Check(device->BeginScene(),"effects_gpu_begin_failed");
    const HRESULT result=device->DrawPrimitive(D3DPT_TRIANGLESTRIP,0,2);
    Check(device->EndScene(),"effects_gpu_end_failed");return result;
}
void Restored(IDirect3DDevice9* device,IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel) {
    float fog[4]{},reflection[4]{};Com<IDirect3DVertexShader9> got_vs;Com<IDirect3DPixelShader9> got_ps;
    Check(device->GetVertexShaderConstantF(68,fog,1),"effects_gpu_read_fog_failed");
    Check(device->GetVertexShaderConstantF(69,reflection,1),"effects_gpu_read_reflection_failed");
    Check(device->GetVertexShader(got_vs.put()),"effects_gpu_read_vs_failed");Check(device->GetPixelShader(got_ps.put()),"effects_gpu_read_ps_failed");
    Require(!std::memcmp(fog,Fog,sizeof(fog)) && !std::memcmp(reflection,Reflection,sizeof(reflection))
        && got_vs.get()==vertex && got_ps.get()==pixel,"effects_gpu_state_not_bitwise_restored");
}
std::array<unsigned,3> Pixel(IDirect3DDevice9* device) {
    Com<IDirect3DSurface9> surface;Check(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,surface.put()),"effects_gpu_read_surface_failed");
    const auto pixels=Readback(device,surface.get(),1280,720);const auto color=pixels[360u*1280u+640u];
    return {{(color>>16)&255,(color>>8)&255,color&255}};
}
void Near(const std::array<unsigned,3>& pixel,float red,float green,float blue) {
    const float values[]{red,green,blue};
    for(size_t i=0;i<3;++i)Require(std::abs(static_cast<float>(pixel[i])-values[i]*255)<=2,"effects_gpu_pixel_not_expected");
}
}

int wmain(int argc,wchar_t** argv) {
    try {
        Require(argc==3,"usage_effects_gpu_absolute_dxvk_path_new_output_directory");
        const auto output=std::filesystem::absolute(argv[2]);
        Require(!std::filesystem::exists(output) && std::filesystem::create_directory(output),"effects_gpu_use_new_output_directory");
        Com<ID3DBlob> river_code,leaf_code,replacement_code,pixel_code;
        Compile("float4 fog:register(c68);float4 reflection:register(c69);struct O{float4 p:POSITION;float4 c:COLOR0;};O main(float4 p:POSITION){O o;o.p=p;o.c=float4(fog.x,reflection.y,fog.z,1);return o;}","vs_3_0",river_code);
        Compile("struct O{float4 p:POSITION;float4 c:COLOR0;};O main(float4 p:POSITION){O o;o.p=p;o.c=float4(.1,.2,.3,1);return o;}","vs_3_0",leaf_code);
        Compile("struct O{float4 p:POSITION;float4 c:COLOR0;};O main(float4 p:POSITION){O o;o.p=p;o.c=float4(.6,.5,.7,1);return o;}","vs_3_0",replacement_code);
        Compile("float4 main(float4 c:COLOR0):COLOR{return c;}","ps_3_0",pixel_code);
        const auto river_hash=Digest(river_code.get()),leaf_hash=Digest(leaf_code.get()),replacement_hash=Digest(replacement_code.get()),pixel_hash=Digest(pixel_code.get());
        Require(MaterialShaderEffects::FixtureCompatibility(0,river_hash.data(),pixel_hash.data(),static_cast<DWORD>(river_code->GetBufferSize()),static_cast<DWORD>(pixel_code->GetBufferSize()),nullptr),"effects_gpu_river_identity_failed");
        Require(MaterialShaderEffects::FixtureCompatibility(3,leaf_hash.data(),pixel_hash.data(),static_cast<DWORD>(leaf_code->GetBufferSize()),static_cast<DWORD>(pixel_code->GetBufferSize()),replacement_hash.data()),"effects_gpu_leaf_identity_failed");
        const auto replacement=output/L"authored-leaf.vs.bin";Save(replacement,replacement_code.get());
        SetProcessDPIAware();Runtime runtime(std::filesystem::absolute(argv[1]));
        Com<IDirect3D9> api;*api.put()=runtime.create();Require(api.get()!=nullptr,"effects_gpu_factory_failed");
        Window window(1280,720);window.show();auto parameters=Params(window.get());Com<IDirect3DDevice9> device;
        Check(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window.get(),D3DCREATE_HARDWARE_VERTEXPROCESSING,&parameters,device.put()),"effects_gpu_device_failed");
        Com<IDirect3DVertexShader9> river,leaf;Com<IDirect3DPixelShader9> pixel;
        Check(device->CreateVertexShader(static_cast<const DWORD*>(river_code->GetBufferPointer()),river.put()),"effects_gpu_create_river_vs_failed");
        Check(device->CreateVertexShader(static_cast<const DWORD*>(leaf_code->GetBufferPointer()),leaf.put()),"effects_gpu_create_leaf_vs_failed");
        Check(device->CreatePixelShader(static_cast<const DWORD*>(pixel_code->GetBufferPointer()),pixel.put()),"effects_gpu_create_ps_failed");
        Geometry geometry(device.get());NativeGraphicsConfig::Version2 native{};
        native.base={sizeof(native),2,1280,720,640,360,{}};native.flags=NativeGraphicsConfig::Water|NativeGraphicsConfig::Vegetation;
        const auto report=(output/L"hook.private.json").wstring();std::copy(report.begin(),report.end(),native.base.report_path);
        Require(WePlaySupersamplingInitialize(&native)==1,"effects_gpu_native_init_failed");
        MaterialShaderEffects::Config effects{};effects.flags=MaterialShaderEffects::River|MaterialShaderEffects::Leaf;effects.render_width=1280;effects.render_height=720;
        const auto effect_report=(output/L"effects.private.json").wstring(),replacement_path=replacement.wstring();
        std::copy(effect_report.begin(),effect_report.end(),effects.report_path);std::copy(replacement_path.begin(),replacement_path.end(),effects.leaf_vertex_path);
        Require(WePlayNativeGraphicsConfigureEffects(&effects)==1,"effects_gpu_config_queue_failed");
        previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window.get(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Procedure)));live=device.get();
        Require(WePlaySupersamplingAttach(device.get())==1,"effects_gpu_attach_failed");
        Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"effects_gpu_prime_draw_failed");Pump();
        Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"effects_gpu_prime_after_resize_failed");
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_prime_present_failed");
        NativeGraphicsCapture::Request epoch_capture{sizeof(epoch_capture),1,1};
        Require(WePlayNativeGraphicsRequestCapture(&epoch_capture)==1,"effects_gpu_epoch_capture_queue_failed");
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_no_draw_present_failed");
        SupersamplingStats no_draw{};
        Require(WePlaySupersamplingReadStats(&no_draw)==1 && no_draw.capture_completed==0
            && !std::filesystem::exists(output/L"capture-1.json"),"effects_gpu_stale_bb_captured_as_new_epoch");
        Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"effects_gpu_active_river_failed");Restored(device.get(),river.get(),pixel.get());
        const auto river_pixel=Pixel(device.get());Near(river_pixel,.094f,.345f,.282f);
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_river_present_failed");
        SupersamplingStats drawn{};Require(WePlaySupersamplingReadStats(&drawn)==1 && drawn.capture_completed==1,
            "effects_gpu_fresh_epoch_not_captured");
        Check(DrawScene(device.get(),geometry,leaf.get(),pixel.get()),"effects_gpu_active_leaf_failed");Restored(device.get(),leaf.get(),pixel.get());
        const auto leaf_pixel=Pixel(device.get());Near(leaf_pixel,.6f,.5f,.7f);
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_leaf_present_failed");
        Check(DrawScene(device.get(),geometry,leaf.get(),pixel.get()),"effects_gpu_cache_warm_draw_failed");
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_cache_warm_present_failed");
        MaterialShaderEffects::Stats warm_before{},warm_after{};
        Require(MaterialShaderEffects::ReadStats(&warm_before) && warm_before.retained_programs>=1,"effects_gpu_program_cache_not_retained");
        for(unsigned frame=0;frame<8;++frame) {
            Check(DrawScene(device.get(),geometry,leaf.get(),pixel.get()),"effects_gpu_warm_frame_draw_failed");
            Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_warm_frame_present_failed");
        }
        Require(MaterialShaderEffects::ReadStats(&warm_after) && warm_after.shader_creations==warm_before.shader_creations
            && warm_after.retained_programs==warm_before.retained_programs,"effects_gpu_warm_frames_recreated_programs");
        Restored(device.get(),leaf.get(),pixel.get());Near(Pixel(device.get()),.6f,.5f,.7f);
        auto disabled=effects;disabled.flags=0;Require(WePlayNativeGraphicsConfigureEffects(&disabled)==1,"effects_gpu_disable_queue_failed");
        Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"effects_gpu_boundary_draw_failed");
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_disable_boundary_failed");
        Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"effects_gpu_disabled_draw_failed");Restored(device.get(),river.get(),pixel.get());
        Near(Pixel(device.get()),.1f,.3f,.3f);Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_disabled_present_failed");
        parameters=Params(window.get());Check(device->Reset(&parameters),"effects_gpu_disabled_reset_failed");
        Require(WePlayNativeGraphicsConfigureEffects(&effects)==1,"effects_gpu_reenable_queue_failed");
        Check(DrawScene(device.get(),geometry,leaf.get(),pixel.get()),"effects_gpu_enable_boundary_draw_failed");
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_enable_boundary_present_failed");
        Check(DrawScene(device.get(),geometry,leaf.get(),pixel.get()),"effects_gpu_after_reset_leaf_failed");Restored(device.get(),leaf.get(),pixel.get());Near(Pixel(device.get()),.6f,.5f,.7f);
        Require(WePlaySupersamplingFixtureFailDraw()==1,"effects_gpu_failure_setup_failed");
        const HRESULT failed=DrawScene(device.get(),geometry,river.get(),pixel.get());
        Require(failed==D3DERR_INVALIDCALL,"effects_gpu_original_failure_not_retained");Restored(device.get(),river.get(),pixel.get());
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_after_failure_present_failed");
        NativeGraphicsCapture::Request fault_capture{sizeof(fault_capture),1,2};
        Require(WePlayNativeGraphicsRequestCapture(&fault_capture)==1 && WePlaySupersamplingFixtureModuleFault()==1,
            "effects_gpu_fault_visibility_setup_failed");
        Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"effects_gpu_pre_fault_draw_failed");
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_safe_fault_present_failed");
        SupersamplingStats faulted{};Require(WePlaySupersamplingReadStats(&faulted)==1 && faulted.effects_faulted
            && !faulted.effects_active && faulted.capture_failures>=1 && !std::filesystem::exists(output/L"capture-2.json"),
            "effects_gpu_module_fault_remained_optimistic");
        Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"effects_gpu_original_fallback_draw_failed");
        Restored(device.get(),river.get(),pixel.get());Near(Pixel(device.get()),.1f,.3f,.3f);
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_original_fallback_present_failed");
        MaterialShaderEffects::Stats measured{};Require(MaterialShaderEffects::ReadStats(&measured) && measured.applied[0]>=2 && measured.applied[3]>=2
            && measured.restored_draws>=4 && measured.failed_draws>=1 && !measured.restore_failures && !measured.faulted,"effects_gpu_activation_or_restore_counters_failed");
        Require(DestroyWindow(window.get())!=FALSE,"effects_gpu_window_close_failed");live=nullptr;
        Require(WePlaySupersamplingDetach(device.get())==1,"effects_gpu_final_detach_failed");
        device->SetStreamSource(0,nullptr,0,0);river.reset();leaf.reset();pixel.reset();device.reset();
        std::printf("{\"format\":\"s7-material-effects-gpu-v1\",\"game_tested\":false,\"authored_programs\":true,"
            "\"real_vertex_pixel_creation\":true,\"river_gpu_pixels\":true,\"leaf_gpu_replacement_pixels\":true,"
            "\"bitwise_state_restored\":true,\"failed_original_restored\":true,\"same_process_disable_enable\":true,"
            "\"reset_reenable\":true,\"fresh_draw_epoch_required\":true,\"module_fault_disables_to_original\":true,"
            "\"warm_generated_program_cache_reused\":true,"
            "\"river_applied\":%llu,\"leaf_applied\":%llu,\"restored_draws\":%llu}\n",measured.applied[0],measured.applied[3],measured.restored_draws);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"effects_gpu: %s\n",error.what());return 1;}
}
