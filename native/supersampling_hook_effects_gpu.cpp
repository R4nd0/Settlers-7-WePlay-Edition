// Authored-program GPU fixture. Compatibility overrides are compiled only into
// this executable, never the native graphics DLL or a game process.
#define wmain SupersamplingReferenceMain
#include "supersampling_probe.cpp"
#undef wmain
#include "supersampling_hook_config.h"
#include "supersampling_hook_capture.h"
#include "material_shader_effects.h"
#include "material_comparison.h"
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
void PadProgram(ID3DBlob* original,size_t wanted,DWORD comment,Com<ID3DBlob>& output) {
    Require(original && !(wanted%4) && !(original->GetBufferSize()%4)
        && wanted>=original->GetBufferSize()+8,"comparison_gpu_padding_invalid");
    const auto* source=static_cast<const DWORD*>(original->GetBufferPointer());
    const size_t old_words=original->GetBufferSize()/4,new_words=wanted/4,padding=new_words-old_words;
    Require(source[old_words-1]==0xffff && padding-1<=0x7fff,"comparison_gpu_padding_end_invalid");
    Check(D3DCreateBlob(wanted,output.put()),"comparison_gpu_padding_allocate_failed");
    auto* destination=static_cast<DWORD*>(output->GetBufferPointer());
    std::memcpy(destination,source,(old_words-1)*4);
    destination[old_words-1]=0xfffeu|static_cast<DWORD>((padding-1)<<16);
    for(size_t i=old_words;i<new_words-1;++i)destination[i]=comment;
    destination[new_words-1]=0xffff;
}
struct NegativePrograms {
    std::array<IDirect3DVertexShader9*,129> vertex{};
    std::array<IDirect3DPixelShader9*,129> pixel{};
    ~NegativePrograms(){for(auto* value:vertex)if(value)value->Release();for(auto* value:pixel)if(value)value->Release();}
};
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
float phase=0;
HRESULT DrawScene(IDirect3DDevice9* device,Geometry& geometry,IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel) {
    geometry.Bind(device);
    Check(device->SetVertexShader(vertex),"effects_gpu_bind_vs_failed");Check(device->SetPixelShader(pixel),"effects_gpu_bind_ps_failed");
    Check(device->SetVertexShaderConstantF(68,Fog,1),"effects_gpu_fog_failed");
    Check(device->SetVertexShaderConstantF(69,Reflection,1),"effects_gpu_reflection_failed");
    const float timer[4]={phase,.2f,.3f,.4f};
    Check(device->SetPixelShaderConstantF(80,timer,1),"effects_gpu_timer_failed");
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
    float timer[4]{};Check(device->GetPixelShaderConstantF(80,timer,1),"effects_gpu_read_timer_failed");
    const float expected[4]={phase,.2f,.3f,.4f};Require(!std::memcmp(timer,expected,sizeof(timer)),"comparison_gpu_ps80_not_restored");
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
        Require(argc==3 || argc==5,"usage_effects_gpu_dxvk_output_optional_catalog_sha256");
        const bool compare=argc==5;
        const auto output=std::filesystem::absolute(argv[2]);
        Require(!std::filesystem::exists(output) && std::filesystem::create_directory(output),"effects_gpu_use_new_output_directory");
        Com<ID3DBlob> river_code,leaf_code,replacement_code,pixel_code;
        Compile("float4 fog:register(c68);float4 reflection:register(c69);struct O{float4 p:POSITION;float4 c:COLOR0;};O main(float4 p:POSITION){O o;o.p=p;o.c=float4(fog.x,reflection.y,fog.z,1);return o;}","vs_3_0",river_code);
        Compile("struct O{float4 p:POSITION;float4 c:COLOR0;};O main(float4 p:POSITION){O o;o.p=p;o.c=float4(.1,.2,.3,1);return o;}","vs_3_0",leaf_code);
        Compile("struct O{float4 p:POSITION;float4 c:COLOR0;};O main(float4 p:POSITION){O o;o.p=p;o.c=float4(.6,.5,.7,1);return o;}","vs_3_0",replacement_code);
        Compile("float4 timer:register(c80);float4 main(float4 c:COLOR0):COLOR{return float4(c.r,c.g,c.b+timer.x*.1,1);}","ps_3_0",pixel_code);
        Com<ID3DBlob> river_base,pixel_base;
        Compile("float4 fog:register(c68);float4 reflection:register(c69);struct O{float4 p:POSITION;float4 c:COLOR0;};O main(float4 p:POSITION){O o;o.p=p;o.c=float4(fog.x,reflection.y,fog.z,1);return o;}","vs_3_0",river_base);
        Compile("float4 timer:register(c80);float4 main(float4 c:COLOR0):COLOR{return float4(c.r,c.g,c.b+timer.x*.1,1);}","ps_3_0",pixel_base);
        PadProgram(river_base.get(),2592,0x12345678,river_code);
        PadProgram(pixel_base.get(),3740,0x12345678,pixel_code);
        const auto river_hash=Digest(river_code.get()),leaf_hash=Digest(leaf_code.get()),replacement_hash=Digest(replacement_code.get()),pixel_hash=Digest(pixel_code.get());
        Require(MaterialShaderEffects::FixtureCompatibility(0,river_hash.data(),pixel_hash.data(),static_cast<DWORD>(river_code->GetBufferSize()),static_cast<DWORD>(pixel_code->GetBufferSize()),nullptr),"effects_gpu_river_identity_failed");
        Require(MaterialShaderEffects::FixtureCompatibility(3,leaf_hash.data(),pixel_hash.data(),static_cast<DWORD>(leaf_code->GetBufferSize()),static_cast<DWORD>(pixel_code->GetBufferSize()),replacement_hash.data()),"effects_gpu_leaf_identity_failed");
        if(compare)Require(MaterialComparison::FixtureIdentity(river_hash.data(),static_cast<DWORD>(river_code->GetBufferSize()),
            pixel_hash.data(),static_cast<DWORD>(pixel_code->GetBufferSize())),"comparison_gpu_identity_failed");
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
        NegativePrograms negatives;
        if(compare)for(size_t i=0;i<negatives.vertex.size();++i) {
            Com<ID3DBlob> wrong_vs,wrong_ps;PadProgram(river_base.get(),2592,static_cast<DWORD>(i+1),wrong_vs);
            PadProgram(pixel_base.get(),3740,static_cast<DWORD>(i+1),wrong_ps);
            Check(device->CreateVertexShader(static_cast<const DWORD*>(wrong_vs->GetBufferPointer()),&negatives.vertex[i]),"comparison_gpu_wrong_vs_create_failed");
            Check(device->CreatePixelShader(static_cast<const DWORD*>(wrong_ps->GetBufferPointer()),&negatives.pixel[i]),"comparison_gpu_wrong_ps_create_failed");
            UINT vs_bytes=0,ps_bytes=0;Check(negatives.vertex[i]->GetFunction(nullptr,&vs_bytes),"comparison_gpu_wrong_vs_size_failed");
            Check(negatives.pixel[i]->GetFunction(nullptr,&ps_bytes),"comparison_gpu_wrong_ps_size_failed");
            Require(vs_bytes==2592 && ps_bytes==3740,"comparison_gpu_wrong_programs_not_actual_sized");
        }
        native.base={sizeof(native),2,1280,720,640,360,{}};native.flags=NativeGraphicsConfig::Water|NativeGraphicsConfig::Vegetation;
        if(compare) {
            native.flags|=NativeGraphicsConfig::Observe;
            const std::wstring catalog=std::filesystem::absolute(argv[3]).wstring(),observer=(output/L"observer.private.json").wstring();
            Require(catalog.size()<512 && observer.size()<512 && std::wcslen(argv[4])==64,"comparison_gpu_catalog_invalid");
            std::copy(catalog.begin(),catalog.end(),native.catalog_path);std::copy(observer.begin(),observer.end(),native.observer_report_path);
            const auto digit=[](wchar_t c)->unsigned {if(c>=L'0'&&c<=L'9')return static_cast<unsigned>(c-L'0');
                if(c>=L'a'&&c<=L'f')return static_cast<unsigned>(c-L'a'+10);if(c>=L'A'&&c<=L'F')return static_cast<unsigned>(c-L'A'+10);
                throw std::runtime_error("comparison_gpu_catalog_sha_invalid");};
            for(size_t i=0;i<32;++i)native.catalog_sha256[i]=static_cast<unsigned char>(digit(argv[4][i*2])*16+digit(argv[4][i*2+1]));
        }
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
        if(compare) {
            MaterialComparison::Config enabled{};enabled.enabled=1;
            auto stress_control=effects;stress_control.flags=0;
            Require(WePlayNativeGraphicsConfigureEffects(&stress_control)==1,"comparison_gpu_stress_material_control_failed");
            Require(WePlayNativeGraphicsConfigureComparison(&enabled)==1,"comparison_gpu_enable_queue_failed");
            phase=.2f;Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"comparison_gpu_enable_boundary_failed");
            Check(device->Present(nullptr,nullptr,nullptr,nullptr),"comparison_gpu_enable_boundary_present_failed");
            for(size_t i=0;i<negatives.vertex.size();++i)
                Check(DrawScene(device.get(),geometry,negatives.vertex[i],negatives.pixel[i]),"comparison_gpu_wrong_sized_pair_draw_failed");
            MaterialComparison::State unmatched{};Require(MaterialComparison::ReadState(&unmatched) && !unmatched.pinned
                && !unmatched.matched,"comparison_gpu_wrong_hash_program_was_matched");
            Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"comparison_gpu_initial_pin_draw_failed");Restored(device.get(),river.get(),pixel.get());
            Near(Pixel(device.get()),.1f,.3f,.32f);Check(device->Present(nullptr,nullptr,nullptr,nullptr),"comparison_gpu_pin_present_failed");
            Require(WePlayNativeGraphicsConfigureEffects(&effects)==1,"comparison_gpu_enable_candidate_failed");
            phase=.7f;Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"comparison_gpu_candidate_boundary_failed");
            Check(device->Present(nullptr,nullptr,nullptr,nullptr),"comparison_gpu_candidate_boundary_present_failed");
            Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"comparison_gpu_later_draw_failed");Restored(device.get(),river.get(),pixel.get());
            Near(Pixel(device.get()),.094f,.345f,.302f);
            NativeGraphicsCapture::Request proof{sizeof(proof),1,10};Require(WePlayNativeGraphicsRequestCapture(&proof)==1,"comparison_gpu_capture_queue_failed");
            Check(device->Present(nullptr,nullptr,nullptr,nullptr),"comparison_gpu_proof_present_failed");
            auto off_material=effects;off_material.flags=0;Require(WePlayNativeGraphicsConfigureEffects(&off_material)==1,"comparison_gpu_material_off_failed");
            phase=.8f;Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"comparison_gpu_off_boundary_failed");Check(device->Present(nullptr,nullptr,nullptr,nullptr),"comparison_gpu_off_boundary_present_failed");
            phase=.9f;Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"comparison_gpu_original_control_failed");Restored(device.get(),river.get(),pixel.get());
            Near(Pixel(device.get()),.1f,.3f,.32f);Check(device->Present(nullptr,nullptr,nullptr,nullptr),"comparison_gpu_original_control_present_failed");
            MaterialComparison::Config comparison_disabled{};Require(WePlayNativeGraphicsConfigureComparison(&comparison_disabled)==1,"comparison_gpu_disable_failed");
            Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"comparison_gpu_disable_boundary_failed");Check(device->Present(nullptr,nullptr,nullptr,nullptr),"comparison_gpu_disable_boundary_present_failed");
            Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"comparison_gpu_disabled_draw_failed");Restored(device.get(),river.get(),pixel.get());Near(Pixel(device.get()),.1f,.3f,.39f);
            MaterialComparison::State comparison{};Require(MaterialComparison::ReadState(&comparison)&&!comparison.requested&&!comparison.pinned
                &&comparison.applications>=4&&comparison.applications==comparison.restorations&&!comparison.faulted&&!comparison.unsafe_state,
                "comparison_gpu_restore_or_disable_state_failed");
            Require(WePlayNativeGraphicsConfigureEffects(&effects)==1,"comparison_gpu_restore_material_failed");
            Check(device->Present(nullptr,nullptr,nullptr,nullptr),"comparison_gpu_restore_material_present_failed");phase=0;
        }
        Require(WePlaySupersamplingFixtureFailDraw()==1,"effects_gpu_failure_setup_failed");
        const HRESULT failed=DrawScene(device.get(),geometry,river.get(),pixel.get());
        Require(failed==D3DERR_INVALIDCALL,"effects_gpu_original_failure_not_retained");Restored(device.get(),river.get(),pixel.get());
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_after_failure_present_failed");
        NativeGraphicsCapture::Request fault_capture{sizeof(fault_capture),1,20};
        Require(WePlayNativeGraphicsRequestCapture(&fault_capture)==1 && WePlaySupersamplingFixtureModuleFault()==1,
            "effects_gpu_fault_visibility_setup_failed");
        Check(DrawScene(device.get(),geometry,river.get(),pixel.get()),"effects_gpu_pre_fault_draw_failed");
        Check(device->Present(nullptr,nullptr,nullptr,nullptr),"effects_gpu_safe_fault_present_failed");
        SupersamplingStats faulted{};Require(WePlaySupersamplingReadStats(&faulted)==1 && faulted.effects_faulted
            && !faulted.effects_active && faulted.capture_failures>=1 && !std::filesystem::exists(output/L"capture-20.json"),
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
            "\"comparison_gpu_enabled\":%s,"
            "\"comparison_saturated_wrong_hash_programs\":%u,"
            "\"river_applied\":%llu,\"leaf_applied\":%llu,\"restored_draws\":%llu}\n",compare?"true":"false",compare?129u:0u,measured.applied[0],measured.applied[3],measured.restored_draws);
        return 0;
    }catch(const std::exception& error){std::fprintf(stderr,"effects_gpu: %s\n",error.what());return 1;}
}
