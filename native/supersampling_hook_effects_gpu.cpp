// Authored-program GPU fixture. Compatibility overrides are compiled only into
// this executable, never the native graphics DLL or a game process.
#define wmain SupersamplingReferenceMain
#include "supersampling_probe.cpp"
#undef wmain
#include "supersampling_hook_config.h"
#include "supersampling_hook_capture.h"
#include "material_shader_effects.h"
#include "material_shader_effects_contract_v3.h"
#include "material_shader_effects_contract_v4.h"
#include "material_shader_effects_contract_v5.h"
#include "water_shader_config_palette.h"
#include "water_shader_palette_contract.h"
#include "shader_debug_overlay.h"
#include "material_comparison.h"
#include <d3dcompiler.h>

namespace {
IDirect3DDevice9* live=nullptr;
WNDPROC previous=nullptr;
unsigned forwarded_buttons=0;
D3DPRESENT_PARAMETERS Params(HWND window) {
    D3DPRESENT_PARAMETERS p{};p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window;
    p.BackBufferWidth=1280;p.BackBufferHeight=720;p.BackBufferFormat=D3DFMT_A8R8G8B8;p.BackBufferCount=1;
    p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;return p;
}
LRESULT CALLBACK Procedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    if(message==WM_LBUTTONDOWN||message==WM_LBUTTONUP)++forwarded_buttons;
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
std::array<std::uint64_t,2> OverlaySurfaceProof(IDirect3DDevice9* device,Geometry& geometry,IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel,ShaderDebugOverlay::Profile profile=ShaderDebugOverlay::Profile::Water) {
    Check(DrawScene(device,geometry,vertex,pixel),"overlay_gpu_original_scene_failed");
    Com<IDirect3DSurface9> output,target;Check(device->GetRenderTarget(0,target.put()),"overlay_gpu_original_target_failed");
    D3DVIEWPORT9 viewport{};Check(device->GetViewport(&viewport),"overlay_gpu_original_viewport_failed");
    Check(device->CreateRenderTarget(640,360,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,output.put(),nullptr),"overlay_gpu_physical_target_failed");
    Check(device->ColorFill(output.get(),nullptr,0xff204060),"overlay_gpu_physical_clear_failed");
    const DWORD frequency=D3DSTREAMSOURCE_INDEXEDDATA|2u;
    Check(device->SetStreamSourceFreq(0,frequency),"overlay_gpu_instanced_baseline_failed");
    Check(device->SetRenderState(D3DRS_FILLMODE,D3DFILL_WIREFRAME),"overlay_gpu_wireframe_baseline_failed");
    ShaderDebugOverlay::Status status{};status.applied_valid=1;status.profile=profile;
    ShaderDebugOverlay::MultiStatus multi{};multi.families[0].applied_valid=1;
    Require(profile==ShaderDebugOverlay::Profile::All?ShaderDebugOverlay::DrawAll(device,output.get(),640,360,multi)
        :ShaderDebugOverlay::Draw(device,output.get(),640,360,status),"overlay_gpu_actual_font_panel_draw_failed");
    Restored(device,vertex,pixel);UINT restored_frequency=0;DWORD fill=0;D3DVIEWPORT9 after{};Com<IDirect3DSurface9> restored_target;
    Check(device->GetStreamSourceFreq(0,&restored_frequency),"overlay_gpu_frequency_readback_failed");
    Check(device->GetRenderState(D3DRS_FILLMODE,&fill),"overlay_gpu_fill_readback_failed");
    Check(device->GetViewport(&after),"overlay_gpu_viewport_readback_failed");Check(device->GetRenderTarget(0,restored_target.put()),"overlay_gpu_target_readback_failed");
    Require(restored_frequency==frequency&&fill==D3DFILL_WIREFRAME&&restored_target.get()==target.get()
        &&!std::memcmp(&viewport,&after,sizeof(viewport)),"overlay_gpu_original_state_not_restored");
    const auto pixels=Readback(device,output.get(),640,360);std::uint64_t changed=0,text=0;
    for(unsigned y=0;y<360;++y)for(unsigned x=0;x<640;++x){const auto rgb=pixels[static_cast<size_t>(y)*640+x]&0xffffffu;
        const bool panel=x>=16&&x<(profile==ShaderDebugOverlay::Profile::All?436u:376u)&&y>=16&&y<(profile==ShaderDebugOverlay::Profile::All?308u:228u);
        if(!panel)Require(rgb==0x204060u,"overlay_gpu_changed_outside_panel");else if(rgb!=0x204060u)++changed;
        if(x>=28&&x<200&&y>=28&&y<46&&((rgb>>16)&255)>150&&((rgb>>8)&255)>150&&(rgb&255)>150)++text;
    }
    Require(changed>20000&&text>10,"overlay_gpu_panel_or_font_pixels_missing");
    Require(ShaderDebugOverlay::Reset(device),"overlay_gpu_standalone_font_cleanup_failed");
    ShaderDebugOverlay::Stats stats{};Require(ShaderDebugOverlay::ReadStats(&stats)&&!stats.unsafe_state&&!stats.pending_restore
        &&!stats.retained_original_refs&&!stats.font_ready,"overlay_gpu_standalone_refs_retained");
    Check(device->SetStreamSourceFreq(0,1),"overlay_gpu_restore_fixture_frequency_failed");
    Check(device->SetRenderState(D3DRS_FILLMODE,D3DFILL_SOLID),"overlay_gpu_restore_fixture_fill_failed");
    return {{changed,text}};
}
void V3Trial(const std::filesystem::path& renderer,const std::filesystem::path& output) {
    Com<ID3DBlob> vertex_code,pixel_code,replacement_code;
    Compile("float4 main(float4 p:POSITION):POSITION{return p;}","vs_3_0",vertex_code);
    Compile("float4 main():COLOR{return float4(.2,.3,.4,1);}","ps_3_0",pixel_code);
    Compile("float4 main():COLOR{return float4(.65,.55,.25,1);}","ps_3_0",replacement_code);
    const auto vertex_hash=Digest(vertex_code.get()),pixel_hash=Digest(pixel_code.get()),replacement_hash=Digest(replacement_code.get());
    Require(MaterialShaderEffects::FixtureCompatibility(3,vertex_hash.data(),pixel_hash.data(),static_cast<DWORD>(vertex_code->GetBufferSize()),
        static_cast<DWORD>(pixel_code->GetBufferSize()),nullptr),"v3_gpu_authored_original_identity_failed");
    Require(MaterialShaderEffects::FixtureProgramCompatibility(5,0,replacement_hash.data(),static_cast<DWORD>(replacement_code->GetBufferSize())),
        "v3_gpu_authored_program_pin_failed");
    const auto program=output/L"LocalData"/L"MaterialShaders"/NativeEffectsContractV3::Recipe/(std::wstring(NativeEffectsContractV3::Digest)+L".ps.bin");
    std::filesystem::create_directories(program.parent_path());Save(program,replacement_code.get());
    SetProcessDPIAware();Runtime runtime(renderer);Com<IDirect3D9> api;*api.put()=runtime.create();Require(api.get()!=nullptr,"v3_gpu_factory_failed");
    Window window(1280,720);window.show();auto parameters=Params(window.get());Com<IDirect3DDevice9> device;
    Check(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window.get(),D3DCREATE_HARDWARE_VERTEXPROCESSING,&parameters,device.put()),"v3_gpu_device_failed");
    Com<IDirect3DVertexShader9> vertex;Com<IDirect3DPixelShader9> pixel;
    Check(device->CreateVertexShader(static_cast<const DWORD*>(vertex_code->GetBufferPointer()),vertex.put()),"v3_gpu_original_vs_failed");
    Check(device->CreatePixelShader(static_cast<const DWORD*>(pixel_code->GetBufferPointer()),pixel.put()),"v3_gpu_original_ps_failed");
    Geometry geometry(device.get());NativeGraphicsConfig::Version2 native{};
    native.base={sizeof(native),2,1280,720,640,360,{}};native.flags=NativeGraphicsConfig::Vegetation;
    wcscpy_s(native.base.report_path,(output/L"hook.private.json").c_str());
    Require(WePlaySupersamplingInitialize(&native)==1,"v3_gpu_native_init_failed");
    MaterialShaderConfigV3::Config effects{};effects.render_width=1280;effects.render_height=720;
    wcscpy_s(effects.report_path,(output/L"material-effects.private.json").c_str());wcscpy_s(effects.leaf_pixel_path,program.c_str());
    auto malformed=effects;malformed.size=6180;
    Require(!WePlayNativeGraphicsConfigureEffects(&malformed),"v3_gpu_old_size_new_version_accepted");
    Require(WePlayNativeGraphicsConfigureEffects(&effects)==1,"v3_gpu_initial_off_queue_failed");
    previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window.get(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Procedure)));live=device.get();
    Require(WePlaySupersamplingAttach(device.get())==1,"v3_gpu_attach_failed");
    Check(DrawScene(device.get(),geometry,vertex.get(),pixel.get()),"v3_gpu_prime_failed");Pump();
    Check(DrawScene(device.get(),geometry,vertex.get(),pixel.get()),"v3_gpu_resized_prime_failed");
    Check(device->Present(nullptr,nullptr,nullptr,nullptr),"v3_gpu_initial_off_boundary_failed");
    Check(DrawScene(device.get(),geometry,vertex.get(),pixel.get()),"v3_gpu_off_draw_failed");Restored(device.get(),vertex.get(),pixel.get());Near(Pixel(device.get()),.2f,.3f,.4f);
    effects.flags=MaterialShaderEffects::Leaf;Require(WePlayNativeGraphicsConfigureEffects(&effects)==1,"v3_gpu_enable_queue_failed");
    Check(device->Present(nullptr,nullptr,nullptr,nullptr),"v3_gpu_enable_boundary_failed");
    Check(DrawScene(device.get(),geometry,vertex.get(),pixel.get()),"v3_gpu_candidate_draw_failed");Restored(device.get(),vertex.get(),pixel.get());Near(Pixel(device.get()),.65f,.55f,.25f);
    NativeGraphicsCapture::Request capture{sizeof(capture),1,31};Require(WePlayNativeGraphicsRequestCapture(&capture)==1,"v3_gpu_capture_queue_failed");
    Check(device->Present(nullptr,nullptr,nullptr,nullptr),"v3_gpu_candidate_present_failed");
    SupersamplingStats captured{};Require(WePlaySupersamplingReadStats(&captured)==1&&captured.effects_config_version==3&&captured.capture_completed==1
        &&std::filesystem::exists(output/L"capture-31.json"),"v3_gpu_explicit_epoch_capture_failed");
    // Capture readback drops temporary public surface refs. The conservative
    // external-Release hook may clear generated programs at that boundary;
    // prime exactly once before measuring uncaptured steady frames.
    MaterialShaderEffects::Stats after_readback{};Require(MaterialShaderEffects::ReadStats(&after_readback),"v3_gpu_post_readback_stats_failed");
    Check(DrawScene(device.get(),geometry,vertex.get(),pixel.get()),"v3_gpu_uncaptured_prime_draw_failed");
    Restored(device.get(),vertex.get(),pixel.get());Check(device->Present(nullptr,nullptr,nullptr,nullptr),"v3_gpu_uncaptured_prime_present_failed");
    MaterialShaderEffects::Stats warm_before{};Require(MaterialShaderEffects::ReadStats(&warm_before)&&warm_before.retained_programs==1
        &&warm_before.shader_creations==after_readback.shader_creations+(after_readback.retained_programs?0u:1u),
        "v3_gpu_uncaptured_prime_not_one_bounded_lazy_creation");
    for(unsigned frame=0;frame<8;++frame){Check(DrawScene(device.get(),geometry,vertex.get(),pixel.get()),"v3_gpu_warm_draw_failed");
        Restored(device.get(),vertex.get(),pixel.get());Check(device->Present(nullptr,nullptr,nullptr,nullptr),"v3_gpu_warm_present_failed");}
    MaterialShaderEffects::Stats warm_after{};Require(MaterialShaderEffects::ReadStats(&warm_after)&&warm_after.shader_creations==warm_before.shader_creations
        &&warm_after.retained_programs==1,"v3_gpu_program_cache_churn");
    Require(WePlaySupersamplingFixtureFailDraw()==1,"v3_gpu_original_failure_setup_failed");
    Require(DrawScene(device.get(),geometry,vertex.get(),pixel.get())==D3DERR_INVALIDCALL,"v3_gpu_failed_original_hresult_changed");Restored(device.get(),vertex.get(),pixel.get());
    parameters=Params(window.get());Check(device->Reset(&parameters),"v3_gpu_reset_failed");
    Check(DrawScene(device.get(),geometry,vertex.get(),pixel.get()),"v3_gpu_after_reset_draw_failed");Restored(device.get(),vertex.get(),pixel.get());Near(Pixel(device.get()),.65f,.55f,.25f);
    effects.flags=0;Require(WePlayNativeGraphicsConfigureEffects(&effects)==1,"v3_gpu_disable_queue_failed");
    Check(device->Present(nullptr,nullptr,nullptr,nullptr),"v3_gpu_disable_boundary_failed");
    Check(DrawScene(device.get(),geometry,vertex.get(),pixel.get()),"v3_gpu_final_off_draw_failed");Restored(device.get(),vertex.get(),pixel.get());Near(Pixel(device.get()),.2f,.3f,.4f);
    Check(device->Present(nullptr,nullptr,nullptr,nullptr),"v3_gpu_final_off_present_failed");
    MaterialShaderEffects::Stats measured{};Require(MaterialShaderEffects::ReadStats(&measured)&&measured.config_version==3&&measured.applied[3]>=10
        &&measured.program_creations[0]>=2&&measured.restored_draws==measured.applied[3]
        &&measured.failed_draws==1&&!measured.restore_failures&&!measured.shader_failures&&!measured.set_failures&&!measured.faulted,
        "v3_gpu_activation_restoration_proof_failed");
    Require(DestroyWindow(window.get())!=FALSE,"v3_gpu_window_close_failed");live=nullptr;
    Require(WePlaySupersamplingDetach(device.get())==1,"v3_gpu_detach_failed");
    device->SetStreamSource(0,nullptr,0,0);vertex.reset();pixel.reset();device.reset();
    std::printf("{\"format\":\"s7-material-effects-v3-gpu-v1\",\"game_tested\":false,\"authored_programs\":true,\"wire_bytes\":7204,"
        "\"public_off_on_off\":true,\"pixel_replacement_verified\":true,\"same_frame_capture\":true,\"failed_draw_restored\":true,"
        "\"reset_recreated_program\":true,\"warm_program_reused\":true,\"leaf_applied\":%llu,\"restored_draws\":%llu}\n",measured.applied[3],measured.restored_draws);
}
void V4Trial(const std::filesystem::path& renderer,const std::filesystem::path& output,bool shader_overlay=false) {
    Com<ID3DBlob> vertex_code,pixel_code;
    Compile("float4 main(float4 p:POSITION):POSITION{return p;}","vs_3_0",vertex_code);
    Compile("float4 main():COLOR{return float4(.2,.3,.4,1);}","ps_3_0",pixel_code);
    std::array<Com<ID3DBlob>,3> replacements;
    const char* authored[]={"float4 main():COLOR{return float4(.25,.2,.15,1);}",
        "float4 main():COLOR{return float4(.5,.3,.25,1);}","float4 main():COLOR{return float4(.75,.4,.35,1);}"};
    std::array<std::array<unsigned char,32>,3> hashes{};
    const auto vertex_hash=Digest(vertex_code.get()),pixel_hash=Digest(pixel_code.get());
    Require(MaterialShaderEffects::FixtureCompatibility(3,vertex_hash.data(),pixel_hash.data(),static_cast<DWORD>(vertex_code->GetBufferSize()),
        static_cast<DWORD>(pixel_code->GetBufferSize()),nullptr),"v4_gpu_original_identity_failed");
    MaterialShaderConfigV4::Config config{};config.render_width=1280;config.render_height=720;
    wcscpy_s(config.report_path,(output/L"material-effects.private.json").c_str());
    for(unsigned index=0;index<3;++index){
        Compile(authored[index],"ps_3_0",replacements[index]);hashes[index]=Digest(replacements[index].get());
        Require(MaterialShaderEffects::FixtureProgramCompatibility(6,index,hashes[index].data(),static_cast<DWORD>(replacements[index]->GetBufferSize())),"v4_gpu_program_pin_failed");
        const auto program=output/L"LocalData"/L"MaterialShaders"/NativeEffectsContractV4::Recipes[index]/(std::wstring(NativeEffectsContractV4::Digests[index])+L".ps.bin");
        std::filesystem::create_directories(program.parent_path());Save(program,replacements[index].get());wcscpy_s(config.program_paths[index],program.c_str());
    }
    SetProcessDPIAware();Runtime runtime(renderer);Com<IDirect3D9> api;*api.put()=runtime.create();Require(api.get()!=nullptr,"v4_gpu_factory_failed");
    Window window(1280,720);window.show();auto parameters=Params(window.get());Com<IDirect3DDevice9> device;
    Check(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window.get(),D3DCREATE_HARDWARE_VERTEXPROCESSING,&parameters,device.put()),"v4_gpu_device_failed");
    Com<IDirect3DVertexShader9> vertex;Com<IDirect3DPixelShader9> pixel;
    Check(device->CreateVertexShader(static_cast<const DWORD*>(vertex_code->GetBufferPointer()),vertex.put()),"v4_gpu_original_vs_failed");
    Check(device->CreatePixelShader(static_cast<const DWORD*>(pixel_code->GetBufferPointer()),pixel.put()),"v4_gpu_original_ps_failed");
    Geometry geometry(device.get());std::array<std::uint64_t,2> overlay_pixels{};
    if(shader_overlay)overlay_pixels=OverlaySurfaceProof(device.get(),geometry,vertex.get(),pixel.get(),ShaderDebugOverlay::Profile::Leaf);
    NativeGraphicsConfig::Version2 native{};
    native.base={sizeof(native),2,1280,720,640,360,{}};native.flags=NativeGraphicsConfig::Vegetation;
    wcscpy_s(native.base.report_path,(output/L"hook.private.json").c_str());
    Require(WePlaySupersamplingInitialize(&native)==1,"v4_gpu_native_init_failed");
    Require(WePlayNativeGraphicsConfigureEffects(&config)==1,"v4_gpu_initial_off_queue_failed");
    previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window.get(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Procedure)));live=device.get();
    Require(WePlaySupersamplingAttach(device.get())==1,"v4_gpu_attach_failed");
    const auto draw=[&](){Check(DrawScene(device.get(),geometry,vertex.get(),pixel.get()),"v4_gpu_draw_failed");Restored(device.get(),vertex.get(),pixel.get());};
    const auto present=[&](){Check(device->Present(nullptr,nullptr,nullptr,nullptr),"v4_gpu_present_failed");if(shader_overlay)Restored(device.get(),vertex.get(),pixel.get());};
    const auto select=[&](DWORD variant,DWORD flags){config.selected_variant=variant;config.flags=flags;
        Require(WePlayNativeGraphicsConfigureEffects(&config)==1,"v4_gpu_selection_queue_failed");draw();present();};
    draw();Pump();draw();present();draw();Near(Pixel(device.get()),.2f,.3f,.4f);present();
    if(shader_overlay){DWORD overlay[4]={16,2,1,2};Require(WePlayNativeGraphicsConfigureOverlay(overlay)==1,"leaf_overlay_gpu_config16_failed");
        draw();present();SupersamplingStats shown{};Require(WePlaySupersamplingReadStats(&shown)==1&&shown.overlay_visible&&shown.overlay_draws&&!shown.overlay_unsafe,"leaf_overlay_gpu_not_visible");}
    select(0,4);draw();present();MaterialShaderEffects::Stats warm{};
    Require(MaterialShaderEffects::ReadStats(&warm)&&warm.config_version==4&&warm.retained_programs==3
        &&warm.variant_creations==std::array<std::uint64_t,3>{{1,1,1}},"v4_gpu_first_match_did_not_create_all_three_once");
    for(unsigned index:{1u,2u,0u}){select(index,4);draw();present();}
    select(0,0);MaterialShaderEffects::Stats off_warm{};
    Require(MaterialShaderEffects::ReadStats(&off_warm)&&off_warm.retained_programs==3&&off_warm.shader_creations==warm.shader_creations,
        "v4_gpu_off_or_selection_destroyed_warm_palette");
    select(0,4);draw();present();MaterialShaderEffects::Stats reused{};
    Require(MaterialShaderEffects::ReadStats(&reused)&&reused.variant_creations==warm.variant_creations,"v4_gpu_warm_selection_recreated_programs");
    const float colors[3][3]={{.25f,.2f,.15f},{.5f,.3f,.25f},{.75f,.4f,.35f}};
    for(unsigned index=0;index<3;++index){
        select(index,4);
        NativeGraphicsCapture::Request capture{sizeof(capture),1,50+index};Require(WePlayNativeGraphicsRequestCapture(&capture)==1,"v4_gpu_capture_queue_failed");
        present();SupersamplingStats stale{};Require(WePlaySupersamplingReadStats(&stale)==1&&stale.capture_completed==index,
            "v4_gpu_selected_epoch_captured_old_backbuffer");
        draw();Near(Pixel(device.get()),colors[index][0],colors[index][1],colors[index][2]);present();
        SupersamplingStats proof{};Require(WePlaySupersamplingReadStats(&proof)==1&&proof.capture_completed==index+1
            &&proof.effects_selected_variant==index&&proof.effects_drawn_variant==index&&proof.effects_drawn_frame==proof.present_calls,
            "v4_gpu_actual_selected_draw_frame_not_captured");
    }
    parameters=Params(window.get());Check(device->Reset(&parameters),"v4_gpu_reset_failed");
    draw();Near(Pixel(device.get()),.75f,.4f,.35f);present();
    Require(WePlaySupersamplingFixtureFailDraw()==1,"v4_gpu_original_failure_setup_failed");
    Require(DrawScene(device.get(),geometry,vertex.get(),pixel.get())==D3DERR_INVALIDCALL,"v4_gpu_failed_original_hresult_changed");
    Restored(device.get(),vertex.get(),pixel.get());present();
    select(2,0);draw();Near(Pixel(device.get()),.2f,.3f,.4f);
    NativeGraphicsCapture::Request off_capture{sizeof(off_capture),1,60};Require(WePlayNativeGraphicsRequestCapture(&off_capture)==1,"v4_gpu_off_capture_queue_failed");present();
    MaterialShaderEffects::Stats measured{};Require(MaterialShaderEffects::ReadStats(&measured)&&measured.config_version==4&&measured.selected_variant==2
        &&measured.failed_draws==1&&!measured.restore_failures&&!measured.shader_failures&&!measured.set_failures&&!measured.faulted,
        "v4_gpu_module_activation_or_rollback_failed");
    for(unsigned index=0;index<3;++index)Require(measured.variant_applications[index]>0
        &&measured.variant_applications[index]==measured.variant_restorations[index]
        &&measured.variant_successful_draws[index]>0&&measured.variant_sha256[index]==hashes[index],"v4_gpu_actual_variant_evidence_missing");
    std::string overlay_receipt;
    if(shader_overlay){std::array<std::uint64_t,4> epochs{};SupersamplingStats start{};Require(WePlaySupersamplingReadStats(&start)==1,"leaf_overlay_gpu_start_stats_failed");
        for(unsigned row=0;row<4;++row){MaterialShaderEffects::Stats before{};Require(MaterialShaderEffects::ReadStats(&before),"leaf_overlay_gpu_before_input_failed");
            const auto forwarded=forwarded_buttons;const LPARAM point=MAKELPARAM(40,60+row*32+16);
            SendMessageW(window.get(),WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(window.get(),WM_LBUTTONUP,0,point);
            MaterialShaderEffects::Stats queued{};Require(MaterialShaderEffects::ReadStats(&queued)&&queued.enable_changes==before.enable_changes&&forwarded_buttons==forwarded,"leaf_overlay_gpu_midframe_mutation_or_game_input");
            present();MaterialShaderEffects::Stats configured{};Require(MaterialShaderEffects::ReadStats(&configured)&&configured.enable_changes>before.enable_changes
                &&configured.config_version==4&&configured.enabled_flags==(row?4u:0u)&&configured.selected_variant==(row?row-1:0u),"leaf_overlay_gpu_fixed_choice_not_configured");
            epochs[row]=configured.enable_changes;draw();if(row)Near(Pixel(device.get()),colors[row-1][0],colors[row-1][1],colors[row-1][2]);else Near(Pixel(device.get()),.2f,.3f,.4f);present();
            SupersamplingStats applied{};Require(WePlaySupersamplingReadStats(&applied)==1&&applied.effects_active==(row?4u:0u)
                &&applied.effects_selected_variant==(row?row-1:0u)&&applied.effects_drawn_variant==(row?row-1:MAXDWORD)
                &&applied.effects_drawn_frame==applied.present_calls&&!applied.effects_variant_frame_failed_draws
                &&(row?(applied.effects_variant_frame_applications>0&&applied.effects_variant_frame_applications==applied.effects_variant_frame_restorations):!applied.effects_variant_frame_applications),"leaf_overlay_gpu_missing_actual_draw_proof");
        }
        SupersamplingStats done{};Require(WePlaySupersamplingReadStats(&done)==1&&done.overlay_choices-start.overlay_choices==4&&done.overlay_input_events-start.overlay_input_events>=8
            &&done.overlay_draws-start.overlay_draws>=8&&!done.overlay_unsafe&&!done.render_unsafe,"leaf_overlay_gpu_core_choices_or_unsafe");
        DWORD disabled[4]={16,2,0,2};Require(WePlayNativeGraphicsConfigureOverlay(disabled)==1,"leaf_overlay_gpu_disable_failed");draw();present();
        Check(device->Reset(&parameters),"leaf_overlay_gpu_reset_failed");ShaderDebugOverlay::Stats renderer_stats{};
        Require(ShaderDebugOverlay::ReadStats(&renderer_stats)&&!renderer_stats.unsafe_state&&!renderer_stats.pending_restore&&!renderer_stats.retained_original_refs&&!renderer_stats.font_ready,"leaf_overlay_gpu_reset_retained_refs");
        overlay_receipt=",\"shader_overlay\":{\"profile_id\":2,\"wire_bytes\":16,\"physical_panel_pixels\":"+std::to_string(overlay_pixels[0])
            +",\"font_pixels\":"+std::to_string(overlay_pixels[1])+",\"outside_control_unchanged\":true,\"original_state_restored\":true,\"actual_wndproc_choices\":4,\"safe_boundary_only\":true"
            +",\"physical_input_before_scaling\":true,\"actual_selected_program_pixels\":true,\"reset_closed_cleanly\":true,\"unsafe\":0,\"epochs\":["
            +std::to_string(epochs[0])+","+std::to_string(epochs[1])+","+std::to_string(epochs[2])+","+std::to_string(epochs[3])+"],\"overlay_draws\":"
            +std::to_string(done.overlay_draws-start.overlay_draws)+",\"input_events\":"+std::to_string(done.overlay_input_events-start.overlay_input_events)+"}";
    }
    Require(DestroyWindow(window.get())!=FALSE,"v4_gpu_window_close_failed");live=nullptr;
    Require(WePlaySupersamplingDetach(device.get())==1,"v4_gpu_detach_failed");device->SetStreamSource(0,nullptr,0,0);vertex.reset();pixel.reset();device.reset();
    std::array<std::string,3> hash_text{};constexpr char digits[]="0123456789abcdef";
    for(unsigned index=0;index<3;++index)for(auto value:hashes[index]){hash_text[index]+=digits[value>>4];hash_text[index]+=digits[value&15];}
    std::printf("{\"format\":\"s7-material-effects-v4-gpu-v1\",\"game_tested\":false,\"authored_programs\":true,\"wire_bytes\":4120,"
        "\"three_program_colors\":true,\"first_match_creates_three\":true,\"safe_live_selection\":true,\"off_retains_warm_palette\":true,"
        "\"same_frame_captures\":true,\"stale_epoch_deferred\":true,\"failed_original_restored\":true,\"reset_recreated_palette\":true,"
        "\"variant_applications\":[%llu,%llu,%llu],\"variant_restorations\":[%llu,%llu,%llu],\"program_sha256\":[\"%s\",\"%s\",\"%s\"]%s}\n",
        measured.variant_applications[0],measured.variant_applications[1],measured.variant_applications[2],
        measured.variant_restorations[0],measured.variant_restorations[1],measured.variant_restorations[2],
        hash_text[0].c_str(),hash_text[1].c_str(),hash_text[2].c_str(),overlay_receipt.c_str());
}
void WaterPaletteTrial(const std::filesystem::path& renderer,const std::filesystem::path& output,bool shader_overlay=false) {
    Com<ID3DBlob> base_vertex,base_pixel,vertex_code,pixel_code;
    Compile("float4 main(float4 p:POSITION):POSITION{return p;}","vs_3_0",base_vertex);
    Compile("float4 main():COLOR{return float4(.2,.3,.4,1);}","ps_3_0",base_pixel);
    PadProgram(base_vertex.get(),2592,0x771100,vertex_code);PadProgram(base_pixel.get(),3740,0x771100,pixel_code);
    const auto vertex_hash=Digest(vertex_code.get()),pixel_hash=Digest(pixel_code.get());
    const char* authored[]={"float4 main():COLOR{return float4(.1,.4,.7,1);}",
        "float4 main():COLOR{return float4(.15,.5,.8,1);}","float4 main():COLOR{return float4(.2,.6,.9,1);}"};
    std::array<Com<ID3DBlob>,3> replacements;std::array<std::array<unsigned char,32>,3> hashes{};
    WaterShaderEffects::ConfigPalette water{};water.render_width=1280;water.render_height=720;
    for(unsigned index=0;index<3;++index){Com<ID3DBlob> compiled;Compile(authored[index],"ps_3_0",compiled);
        PadProgram(compiled.get(),3840,0x771101+index,replacements[index]);hashes[index]=Digest(replacements[index].get());
        Require(WaterShaderEffects::FixturePaletteIdentity(index,hashes[index].data()),"water_palette_gpu_program_pin_failed");
        std::wstring digest;for(const char* at=WaterShaderPaletteContract::Digests[index];*at;++at)digest+=static_cast<wchar_t>(*at);
        const auto path=output/L"LocalData"/L"MaterialShaders"/WaterShaderPaletteContract::Recipes[index]/(digest+L".ps.bin");
        std::filesystem::create_directories(path.parent_path());Save(path,replacements[index].get());wcscpy_s(water.program_paths[index],path.c_str());
    }
    Require(WaterShaderEffects::FixtureIdentities(vertex_hash.data(),pixel_hash.data(),hashes[0].data()),"water_palette_gpu_original_identities_failed");
    SetProcessDPIAware();Runtime runtime(renderer);Com<IDirect3D9> api;*api.put()=runtime.create();Require(api.get()!=nullptr,"water_palette_gpu_factory_failed");
    Window window(1280,720);window.show();auto parameters=Params(window.get());Com<IDirect3DDevice9> device;
    Check(api->CreateDevice(0,D3DDEVTYPE_HAL,window.get(),D3DCREATE_HARDWARE_VERTEXPROCESSING,&parameters,device.put()),"water_palette_gpu_device_failed");
    Com<IDirect3DVertexShader9> vertex;Com<IDirect3DPixelShader9> pixel;
    Check(device->CreateVertexShader(static_cast<const DWORD*>(vertex_code->GetBufferPointer()),vertex.put()),"water_palette_gpu_original_vs_failed");
    Check(device->CreatePixelShader(static_cast<const DWORD*>(pixel_code->GetBufferPointer()),pixel.put()),"water_palette_gpu_original_ps_failed");
    Geometry geometry(device.get());std::array<std::uint64_t,2> overlay_pixels{};
    if(shader_overlay)overlay_pixels=OverlaySurfaceProof(device.get(),geometry,vertex.get(),pixel.get());
    NativeGraphicsConfig::Version2 native{};native.base={sizeof(native),2,1280,720,640,360,{}};native.flags=NativeGraphicsConfig::Water;
    wcscpy_s(native.base.report_path,(output/L"hook.private.json").c_str());Require(WePlaySupersamplingInitialize(&native)==1,"water_palette_gpu_public_init_failed");
    MaterialShaderEffects::Config bootstrap{};bootstrap.render_width=1280;bootstrap.render_height=720;
    wcscpy_s(bootstrap.report_path,(output/L"effects.private.json").c_str());Require(WePlayNativeGraphicsConfigureEffects(&bootstrap)==1,"water_palette_gpu_legacy_vs_bootstrap_failed");
    previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window.get(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Procedure)));live=device.get();
    Require(WePlaySupersamplingAttach(device.get())==1,"water_palette_gpu_attach_failed");
    const auto draw=[&](){Check(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff202020,1,0),"water_palette_gpu_clear_failed");
        Check(DrawScene(device.get(),geometry,vertex.get(),pixel.get()),"water_palette_gpu_draw_failed");Restored(device.get(),vertex.get(),pixel.get());};
    const auto present=[&](){Check(device->Present(nullptr,nullptr,nullptr,nullptr),"water_palette_gpu_present_failed");
        if(shader_overlay)Restored(device.get(),vertex.get(),pixel.get());};
    const auto select=[&](DWORD index,DWORD enabled){water.selected_variant=index;water.enabled=enabled;
        Require(WePlayNativeGraphicsConfigureWater(&water)==1,"water_palette_gpu_selection_queue_failed");draw();present();};
    for(unsigned frame=0;frame<4;++frame){draw();if(!frame)Pump();present();}
    Require(WePlayNativeGraphicsConfigureWater(&water)==1,"water_palette_gpu_initial_off_queue_failed");
    NativeGraphicsCapture::Request off_first{sizeof(off_first),1,70};Require(WePlayNativeGraphicsRequestCapture(&off_first)==1,"water_palette_gpu_immediate_capture_after_ack_failed");
    present();SupersamplingStats deferred{};Require(WePlaySupersamplingReadStats(&deferred)==1&&!deferred.capture_completed,
        "water_palette_gpu_pending_config_captured_old_frame");
    draw();Near(Pixel(device.get()),.2f,.3f,.4f);present();SupersamplingStats initial{};Require(WePlaySupersamplingReadStats(&initial)==1
        &&initial.capture_completed==1&&initial.water_frame_origin>=4,"water_palette_gpu_initial_original_or_late_origin_failed");
    if(shader_overlay){DWORD overlay[3]={12,1,1};Require(WePlayNativeGraphicsConfigureOverlay(overlay)==1,"overlay_gpu_public_configure_failed");
        draw();present();SupersamplingStats shown{};Require(WePlaySupersamplingReadStats(&shown)==1&&shown.overlay_visible
            &&shown.overlay_draws&&!shown.overlay_unsafe,"overlay_gpu_post_downsample_not_visible");}
    select(0,1);draw();present();WaterShaderEffects::Stats warm{};Require(WaterShaderEffects::ReadStats(&warm)&&warm.config_version==2
        &&warm.retained_programs==3&&warm.variant_creations==std::array<std::uint64_t,3>{{1,1,1}},"water_palette_gpu_first_draw_must_create_three");
    for(unsigned index:{1u,2u,0u}){select(index,1);draw();present();}
    select(0,0);WaterShaderEffects::Stats warm_off{};Require(WaterShaderEffects::ReadStats(&warm_off)&&warm_off.retained_programs==3
        &&warm_off.variant_creations==warm.variant_creations,"water_palette_gpu_off_selection_reloaded_cache");
    select(0,1);draw();present();
    const float colors[3][3]={{.1f,.4f,.7f},{.15f,.5f,.8f},{.2f,.6f,.9f}};
    for(unsigned index=0;index<3;++index){water.selected_variant=index;water.enabled=1;
        Require(WePlayNativeGraphicsConfigureWater(&water)==1,"water_palette_gpu_immediate_variant_queue_failed");
        NativeGraphicsCapture::Request request{sizeof(request),1,80+index};Require(WePlayNativeGraphicsRequestCapture(&request)==1,"water_palette_gpu_immediate_request_failed");
        present();present();SupersamplingStats stale{};Require(WePlaySupersamplingReadStats(&stale)==1&&stale.capture_completed==index+1,
            "water_palette_gpu_config_or_drawless_present_claimed_stale_body");
        draw();Near(Pixel(device.get()),colors[index][0],colors[index][1],colors[index][2]);present();
        SupersamplingStats captured{};Require(WePlaySupersamplingReadStats(&captured)==1&&captured.capture_completed==index+2,
            "water_palette_gpu_selected_real_frame_not_captured");
    }
    Check(device->Reset(&parameters),"water_palette_gpu_actual_reset_failed");draw();Near(Pixel(device.get()),.2f,.6f,.9f);present();
    Require(WePlaySupersamplingFixtureFailDraw()==1,"water_palette_gpu_original_failure_setup_failed");
    Require(DrawScene(device.get(),geometry,vertex.get(),pixel.get())==D3DERR_INVALIDCALL,"water_palette_gpu_original_failure_changed");
    Restored(device.get(),vertex.get(),pixel.get());NativeGraphicsCapture::Request failed{sizeof(failed),1,90};
    Require(WePlayNativeGraphicsRequestCapture(&failed)==1,"water_palette_gpu_failed_frame_request_failed");present();
    SupersamplingStats refused{};Require(WePlaySupersamplingReadStats(&refused)==1&&refused.capture_completed==4,
        "water_palette_gpu_failed_original_frame_was_captured");draw();present();
    select(2,0);draw();Near(Pixel(device.get()),.2f,.3f,.4f);NativeGraphicsCapture::Request last{sizeof(last),1,100};
    Require(WePlayNativeGraphicsRequestCapture(&last)==1,"water_palette_gpu_final_original_capture_failed");present();
    WaterShaderEffects::Stats measured{};Require(WaterShaderEffects::ReadStats(&measured)&&!measured.faulted&&!measured.unsafe_state
        &&!measured.pending_original_refs&&!measured.shader_failures&&!measured.set_failures&&!measured.restore_failures&&measured.failed_draws==1,
        "water_palette_gpu_restore_or_safety_state_failed");
    std::array<std::string,3> hash_text{};constexpr char digits[]="0123456789abcdef";
    for(unsigned index=0;index<3;++index){Require(measured.variant_applications[index]>0&&measured.variant_applications[index]==measured.variant_restorations[index]
        &&measured.variant_successful_draws[index]>0&&measured.variant_sha256[index]==hashes[index],"water_palette_gpu_actual_program_proof_failed");
        for(auto value:hashes[index]){hash_text[index]+=digits[value>>4];hash_text[index]+=digits[value&15];}}
    std::string overlay_receipt;
    if(shader_overlay){
        std::array<std::uint64_t,4> epochs{};SupersamplingStats start{};Require(WePlaySupersamplingReadStats(&start)==1,"overlay_gpu_start_stats_failed");
        for(unsigned row=0;row<4;++row){WaterShaderEffects::Stats before{};Require(WaterShaderEffects::ReadStats(&before),"overlay_gpu_before_input_failed");
            const unsigned forwarded=forwarded_buttons;const LPARAM position=MAKELPARAM(40,60+row*32+16);
            SendMessageW(window.get(),WM_LBUTTONDOWN,MK_LBUTTON,position);SendMessageW(window.get(),WM_LBUTTONUP,0,position);
            WaterShaderEffects::Stats queued{};Require(WaterShaderEffects::ReadStats(&queued)&&queued.configuration_epoch==before.configuration_epoch
                &&forwarded_buttons==forwarded,"overlay_gpu_click_mutated_midframe_or_reached_game");
            present();WaterShaderEffects::Stats configured{};Require(WaterShaderEffects::ReadStats(&configured)&&configured.configuration_epoch>before.configuration_epoch
                &&configured.enabled==(row?1u:0u)&&configured.selected_variant==(row?row-1:0u),"overlay_gpu_actual_physical_row_selection_failed");
            epochs[row]=configured.configuration_epoch;draw();if(row)Near(Pixel(device.get()),colors[row-1][0],colors[row-1][1],colors[row-1][2]);else Near(Pixel(device.get()),.2f,.3f,.4f);
            present();WaterShaderEffects::Stats applied{};Require(WaterShaderEffects::ReadStats(&applied)&&applied.configuration_epoch==epochs[row]
                &&!applied.unsafe_state&&!applied.pending_original_refs&&applied.completed_drawn_variant==(row?row-1:MAXDWORD)
                &&(row?(applied.completed_applications>0&&applied.completed_applications==applied.completed_restorations):!applied.completed_applications),"overlay_gpu_choice_missing_actual_restored_draw");
        }
        SupersamplingStats done{};Require(WePlaySupersamplingReadStats(&done)==1&&done.overlay_choices-start.overlay_choices==4
            &&done.overlay_input_events-start.overlay_input_events>=8&&done.overlay_draws>start.overlay_draws&&!done.overlay_unsafe&&!done.render_unsafe,"overlay_gpu_core_input_or_draw_proof_failed");
        DWORD disabled[3]={12,1,0};Require(WePlayNativeGraphicsConfigureOverlay(disabled)==1,"overlay_gpu_disable_failed");
        draw();present();Check(device->Reset(&parameters),"overlay_gpu_actual_reset_failed");ShaderDebugOverlay::Stats renderer_stats{};
        Require(ShaderDebugOverlay::ReadStats(&renderer_stats)&&!renderer_stats.unsafe_state&&!renderer_stats.pending_restore
            &&!renderer_stats.retained_original_refs&&!renderer_stats.font_ready,"overlay_gpu_actual_reset_retained_refs");
        overlay_receipt=",\"shader_overlay\":{\"wire_bytes\":12,\"physical_panel_pixels\":"+std::to_string(overlay_pixels[0])
            +",\"font_pixels\":"+std::to_string(overlay_pixels[1])+",\"outside_control_unchanged\":true,\"original_state_restored\":true,\"actual_wndproc_choices\":4"
            +",\"safe_boundary_only\":true,\"physical_input_before_scaling\":true,\"actual_selected_program_pixels\":true,\"reset_closed_cleanly\":true,\"unsafe\":0,\"epochs\":["
            +std::to_string(epochs[0])+","+std::to_string(epochs[1])+","+std::to_string(epochs[2])+","+std::to_string(epochs[3])+"],\"overlay_draws\":"
            +std::to_string(done.overlay_draws-start.overlay_draws)+",\"input_events\":"+std::to_string(done.overlay_input_events-start.overlay_input_events)+"}";
    }
    Require(DestroyWindow(window.get())!=FALSE,"water_palette_gpu_window_close_failed");live=nullptr;Require(WePlaySupersamplingDetach(device.get())==1,"water_palette_gpu_detach_failed");
    device->SetStreamSource(0,nullptr,0,0);vertex.reset();pixel.reset();device.reset();
    std::printf("{\"format\":\"s7-water-palette-gpu-v1\",\"game_tested\":false,\"authored_programs\":true,\"wire_bytes\":3096,"
        "\"public_typed_queue\":true,\"immediate_capture_deferred_safely\":true,\"three_program_colors\":true,\"first_draw_creates_three\":true,"
        "\"off_selection_cache_reused\":true,\"failed_original_restored\":true,\"actual_reset\":true,\"legacy_VS68_69_unchanged\":true,"
        "\"frame_origin\":%lu,\"program_sha256\":[\"%s\",\"%s\",\"%s\"]%s}\n",initial.water_frame_origin,
        hash_text[0].c_str(),hash_text[1].c_str(),hash_text[2].c_str(),overlay_receipt.c_str());
}
void CombinedTrial(const std::filesystem::path& renderer,const std::filesystem::path& output,const wchar_t* catalog_path,const wchar_t* catalog_sha) {
    const char* original_colors[]={"float4 main():COLOR{return float4(.3,.4,.2,1);}","float4 main():COLOR{return float4(.5,.2,.1,1);}","float4 main():COLOR{return float4(.2,.3,.4,1);}"};
    const char* replacements[]={"float4 main():COLOR{return float4(.25,.2,.15,1);}","float4 main():COLOR{return float4(.7,.2,.1,1);}",
        "float4 main():COLOR{return float4(.6,.1,.3,1);}","float4 main(float4 p:POSITION):POSITION{return p;}","float4 main(float4 p:POSITION):POSITION{return p;}",
        "float4 main():COLOR{return float4(.5,.3,.25,1);}","float4 main():COLOR{return float4(.75,.4,.35,1);}"};
    std::array<Com<ID3DBlob>,3> original_vs,original_ps;std::array<Com<ID3DBlob>,7> programs;std::array<std::array<unsigned char,32>,7> hashes{};
    Com<ID3DBlob> position;Compile("float4 main(float4 p:POSITION):POSITION{return p;}","vs_3_0",position);
    for(unsigned i=0;i<3;++i){Com<ID3DBlob> ps;Compile(original_colors[i],"ps_3_0",ps);PadProgram(position.get(),i==2?3832:2004,0x551100+i,original_vs[i]);
        PadProgram(ps.get(),i==0?1636:i==1?3396:1328,0x551200+i,original_ps[i]);const auto v=Digest(original_vs[i].get()),p=Digest(original_ps[i].get());
        Require(MaterialShaderEffects::FixtureCompatibility(i+1,v.data(),p.data(),static_cast<DWORD>(original_vs[i]->GetBufferSize()),static_cast<DWORD>(original_ps[i]->GetBufferSize()),nullptr),"combined_gpu_original_pins");}
    const auto report=(output/L"hook.private.json").wstring();NativeEffectsContractV5::Resolved paths{};
    Require(NativeEffectsContractV5::Resolve(report.c_str(),MaterialShaderConfigV5::Layout::PrivateCase,paths,false)==NativeEffectsContractV5::Error::None,"combined_gpu_fixed_paths");
    for(unsigned i=0;i<7;++i){Com<ID3DBlob> compiled;Compile(replacements[i],i==3||i==4?"vs_3_0":"ps_3_0",compiled);PadProgram(compiled.get(),NativeEffectsContractV5::ProgramBytes[i],0x551300+i,programs[i]);
        hashes[i]=Digest(programs[i].get());Require(MaterialShaderEffects::FixtureProgramCompatibility(7,i,hashes[i].data(),static_cast<DWORD>(programs[i]->GetBufferSize())),"combined_gpu_program_pins");
        std::filesystem::create_directories(std::filesystem::path(paths.program_paths[i]).parent_path());Save(paths.program_paths[i],programs[i].get());}
    Com<ID3DBlob> water_vs,water_ps,water_plain;PadProgram(position.get(),2592,0x661100,water_vs);Compile("float4 main():COLOR{return float4(.2,.25,.3,1);}","ps_3_0",water_plain);PadProgram(water_plain.get(),3740,0x661200,water_ps);
    const auto wv=Digest(water_vs.get()),wp=Digest(water_ps.get());WaterShaderEffects::ConfigPalette water{};water.render_width=1280;water.render_height=720;water.enabled=1;
    std::array<Com<ID3DBlob>,3> water_programs;std::array<std::array<unsigned char,32>,3> water_hashes{};
    const char* water_colors[]={"float4 main():COLOR{return float4(.1,.4,.7,1);}","float4 main():COLOR{return float4(.15,.5,.8,1);}","float4 main():COLOR{return float4(.2,.6,.9,1);}"};
    for(unsigned i=0;i<3;++i){Com<ID3DBlob> compiled;Compile(water_colors[i],"ps_3_0",compiled);PadProgram(compiled.get(),3840,0x661300+i,water_programs[i]);water_hashes[i]=Digest(water_programs[i].get());
        Require(WaterShaderEffects::FixturePaletteIdentity(i,water_hashes[i].data()),"combined_gpu_water_pins");std::wstring digest;for(const char* p=WaterShaderPaletteContract::Digests[i];*p;++p)digest+=static_cast<wchar_t>(*p);
        const auto path=output/L"LocalData"/L"MaterialShaders"/WaterShaderPaletteContract::Recipes[i]/(digest+L".ps.bin");std::filesystem::create_directories(path.parent_path());Save(path,water_programs[i].get());wcscpy_s(water.program_paths[i],path.c_str());}
    Require(WaterShaderEffects::FixtureIdentities(wv.data(),wp.data(),water_hashes[0].data()),"combined_gpu_water_original_pins");
    SetProcessDPIAware();Runtime runtime(renderer);Com<IDirect3D9> api;*api.put()=runtime.create();Require(api.get()!=nullptr,"combined_gpu_factory");Window window(1280,720);window.show();auto parameters=Params(window.get());Com<IDirect3DDevice9> device;
    Check(api->CreateDevice(0,D3DDEVTYPE_HAL,window.get(),D3DCREATE_HARDWARE_VERTEXPROCESSING,&parameters,device.put()),"combined_gpu_device");
    std::array<Com<IDirect3DVertexShader9>,3> vs;std::array<Com<IDirect3DPixelShader9>,3> ps;
    for(unsigned i=0;i<3;++i){Check(device->CreateVertexShader(static_cast<const DWORD*>(original_vs[i]->GetBufferPointer()),vs[i].put()),"combined_gpu_original_vs");Check(device->CreatePixelShader(static_cast<const DWORD*>(original_ps[i]->GetBufferPointer()),ps[i].put()),"combined_gpu_original_ps");}
    Com<IDirect3DVertexShader9> river_vs;Com<IDirect3DPixelShader9> river_ps;
    Check(device->CreateVertexShader(static_cast<const DWORD*>(water_vs->GetBufferPointer()),river_vs.put()),"combined_gpu_river_vs");Check(device->CreatePixelShader(static_cast<const DWORD*>(water_ps->GetBufferPointer()),river_ps.put()),"combined_gpu_river_ps");
    std::array<Com<IDirect3DTexture9>,3> textures;for(unsigned i=0;i<3;++i)Check(device->CreateTexture(i==2?256:512,i==2?256:512,i==2?9:10,0,i==2?D3DFMT_L8:D3DFMT_DXT5,D3DPOOL_MANAGED,textures[i].put(),nullptr),"combined_gpu_authored_macro_texture");
    Geometry geometry(device.get());const auto physical=OverlaySurfaceProof(device.get(),geometry,vs[2].get(),ps[2].get(),ShaderDebugOverlay::Profile::All);
    NativeGraphicsConfig::Version3 init{};init.base.base={sizeof(init),3,1280,720,640,360,{}};init.base.flags=NativeGraphicsConfig::KnownFlags;wcscpy_s(init.base.base.report_path,report.c_str());
    wcscpy_s(init.base.catalog_path,std::filesystem::absolute(catalog_path).c_str());wcscpy_s(init.base.observer_report_path,(output/L"observer.private.json").c_str());Require(std::wcslen(catalog_sha)==64,"combined_gpu_catalog_sha");
    const auto digit=[](wchar_t c)->unsigned{if(c>=L'0'&&c<=L'9')return static_cast<unsigned>(c-L'0');if(c>=L'a'&&c<=L'f')return static_cast<unsigned>(c-L'a'+10);throw std::runtime_error("combined_gpu_catalog_hex");};
    for(unsigned i=0;i<32;++i)init.base.catalog_sha256[i]=static_cast<unsigned char>(digit(catalog_sha[i*2])*16+digit(catalog_sha[i*2+1]));
    Require(WePlaySupersamplingInitialize(&init)==1,"combined_gpu_init3");MaterialShaderConfigV5::Config config{};config.render_width=1280;config.render_height=720;
    Require(WePlayNativeGraphicsConfigureEffects(&config)==1&&WePlayNativeGraphicsConfigureWater(&water)==1,"combined_gpu_typed_initial_queues");DWORD shadow[3]={12,1,0},ambient[3]={12,2,0};
    Require(WePlayNativeGraphicsConfigureShadow(shadow)==1&&WePlayNativeGraphicsConfigureAmbient(ambient)==1,"combined_gpu_shadow_ambient_off");
    previous=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(window.get(),GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(Procedure)));live=device.get();Require(WePlaySupersamplingAttach(device.get())==1,"combined_gpu_attach");
    const auto draw=[&](unsigned role){for(unsigned i=0;i<3;++i)Check(device->SetTexture(4+i,textures[i].get()),"combined_gpu_bind_macro_texture");
        const float region[4]={56,64,3200,3200},detail[4]={512,9,8,0},terrain[4]={15,.5f,.02f,.2f};Check(device->SetVertexShaderConstantF(66,region,1),"combined_gpu_region");Check(device->SetVertexShaderConstantF(67,detail,1),"combined_gpu_detail");Check(device->SetPixelShaderConstantF(33,terrain,1),"combined_gpu_material");
        Check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,role<2),"combined_gpu_blend");Check(device->SetRenderState(D3DRS_SRCBLEND,D3DBLEND_ONE),"combined_gpu_srcblend");Check(device->SetRenderState(D3DRS_DESTBLEND,D3DBLEND_ZERO),"combined_gpu_dstblend");
        auto*v=role==3?river_vs.get():vs[role].get();auto*p=role==3?river_ps.get():ps[role].get();Check(DrawScene(device.get(),geometry,v,p),"combined_gpu_draw");Restored(device.get(),v,p);float actual[4]{};Check(device->GetPixelShaderConstantF(33,actual,1),"combined_gpu_material_readback");Require(!std::memcmp(actual,terrain,sizeof(actual)),"combined_gpu_material_not_restored");};
    const auto present=[&](){Check(device->Present(nullptr,nullptr,nullptr,nullptr),"combined_gpu_present");};
    const auto frame=[&](){for(unsigned r:{3u,2u,0u,1u})draw(r);present();};
    frame();Pump();frame();DWORD overlay[4]={16,3,1,6};Require(WePlayNativeGraphicsConfigureOverlay(overlay)==1,"combined_gpu_overlay3");frame();
    const auto click=[&](unsigned x,unsigned y){const auto before=forwarded_buttons;const LPARAM point=MAKELPARAM(x,y);SendMessageW(window.get(),WM_LBUTTONDOWN,MK_LBUTTON,point);SendMessageW(window.get(),WM_LBUTTONUP,0,point);Require(forwarded_buttons==before,"combined_gpu_click_reached_game");};
    const auto choose=[&](unsigned family,unsigned row){click(20+(family-1)*80+40,64);click(40,92+row*32+16);};
    MaterialShaderEffects::Stats off{};Require(MaterialShaderEffects::ReadStats(&off)&&off.config_version==5&&!off.enabled_flags,"combined_gpu_initial_original_materials");WaterShaderEffects::Stats water_before{};Require(WaterShaderEffects::ReadStats(&water_before)&&water_before.enabled&&water_before.selected_variant==0,"combined_gpu_initial_waterB");
    choose(2,2);choose(3,1);MaterialShaderEffects::Stats queued{};Require(MaterialShaderEffects::ReadStats(&queued)&&queued.enable_changes==off.enable_changes,"combined_gpu_mutated_during_input");present();frame();
    MaterialShaderEffects::Stats both{};Require(MaterialShaderEffects::ReadStats(&both)&&both.enabled_flags==6&&both.selected_variant==1&&both.retained_programs==7&&both.shader_creations==7,"combined_gpu_dual_family_not_warm_seven");
    choose(2,0);present();frame();MaterialShaderEffects::Stats terrain_only{};Require(MaterialShaderEffects::ReadStats(&terrain_only)&&terrain_only.enabled_flags==2&&terrain_only.retained_programs==7&&terrain_only.shader_creations==7,"combined_gpu_leaf_off_dropped_terrain_cache");
    choose(2,3);choose(3,0);present();frame();MaterialShaderEffects::Stats leaf_only{};Require(MaterialShaderEffects::ReadStats(&leaf_only)&&leaf_only.enabled_flags==4&&leaf_only.selected_variant==2&&leaf_only.retained_programs==7&&leaf_only.shader_creations==7,"combined_gpu_terrain_off_dropped_leaf_cache");
    click(60,64);present();MaterialShaderEffects::Stats tabbed{};Require(MaterialShaderEffects::ReadStats(&tabbed)&&tabbed.enable_changes==leaf_only.enable_changes,"combined_gpu_tab_changed_shader");
    WaterShaderEffects::Stats preserved{};Require(WaterShaderEffects::ReadStats(&preserved)&&preserved.enabled&&preserved.selected_variant==0&&preserved.configuration_epoch==water_before.configuration_epoch,"combined_gpu_other_groups_changed_water");
    draw(2);Near(Pixel(device.get()),.75f,.4f,.35f);choose(3,1);present();draw(0);Near(Pixel(device.get()),.7f,.2f,.1f);draw(3);Near(Pixel(device.get()),.1f,.4f,.7f);frame();
    choose(2,0);choose(3,0);present();draw(2);Near(Pixel(device.get()),.2f,.3f,.4f);draw(0);Near(Pixel(device.get()),.3f,.4f,.2f);frame();
    MaterialShaderEffects::Stats measured{};Require(MaterialShaderEffects::ReadStats(&measured)&&!measured.enabled_flags&&!measured.faulted&&!measured.shader_failures&&!measured.set_failures&&!measured.restore_failures&&!measured.exceptions&&measured.combined_program_sha256==hashes,"combined_gpu_program_or_safety_proof");
    for(auto n:measured.combined_program_creations)Require(n>0,"combined_gpu_program_not_created");Require(measured.applied[1]>0&&measured.applied[2]>0&&measured.variant_applications[2]>0&&measured.variant_applications==measured.variant_restorations,"combined_gpu_missing_actual_roles");
    ShaderDebugOverlay::Preset selected{};Require(ShaderDebugOverlay::ReadPreset(&selected)&&selected.values[0].enabled==1&&selected.values[0].variant==0&&!selected.values[1].enabled&&!selected.values[2].enabled&&!selected.values[3].enabled&&!selected.values[4].enabled,"combined_gpu_preset_groups_not_preserved");
    MaterialShaderObserver::DrawTraceConfig trace{};trace.sequence=1;trace.first_draw=1;trace.draw_count=2;
    trace.target_width=1280;trace.target_height=720;trace.x=640;trace.y=360;trace.width=trace.height=4;
    Require(WePlayNativeGraphicsConfigureObservation(&trace)==1,"combined_gpu_trace_queue");present();
    draw(2);draw(0);present();
    for(unsigned i=1;i<=2;++i) {
        std::ifstream image(output/(L"observer.private.json.trace-1-draw-"+std::to_wstring(i)+L".raw"),std::ios::binary);
        std::array<DWORD,16> pixels{};image.read(reinterpret_cast<char*>(pixels.data()),sizeof(pixels));
        Require(image.gcount()==sizeof(pixels),"combined_gpu_trace_readback_in_scene_failed");
        for(auto color:pixels)Near({{(color>>16)&255,(color>>8)&255,color&255}},i==1?.2f:.3f,i==1?.3f:.4f,i==1?.4f:.2f);
    }
    std::ifstream trace_file(output/L"observer.private.json.trace-1.json",std::ios::binary);
    const std::string traced((std::istreambuf_iterator<char>(trace_file)),std::istreambuf_iterator<char>());
    Require(traced.find("\"complete\":true")!=std::string::npos&&traced.find("\"failures\":0,")!=std::string::npos
        &&traced.find("\"successful_draws\":2,")!=std::string::npos,"combined_gpu_trace_frame_receipt");
    Check(device->Reset(&parameters),"combined_gpu_actual_reset");ShaderDebugOverlay::Stats renderer_stats{};Require(ShaderDebugOverlay::ReadStats(&renderer_stats)&&!renderer_stats.unsafe_state&&!renderer_stats.font_ready&&!renderer_stats.retained_original_refs,"combined_gpu_overlay_reset_refs");
    SupersamplingStats core{};Require(WePlaySupersamplingReadStats(&core)==1&&!core.render_unsafe&&!core.overlay_unsafe&&core.overlay_draws>0,"combined_gpu_core_unsafe");
    Require(DestroyWindow(window.get())!=FALSE,"combined_gpu_close");live=nullptr;Require(WePlaySupersamplingDetach(device.get())==1,"combined_gpu_detach");device->SetStreamSource(0,nullptr,0,0);for(unsigned i=0;i<3;++i)device->SetTexture(4+i,nullptr);
    std::printf("{\"format\":\"s7-combined-shader-gpu-v1\",\"game_tested\":false,\"authored_programs\":true,\"initialize_bytes\":3136,\"scope\":1,\"material_bytes\":24,\"overlay_bytes\":16,\"profile\":6,\"seven_programs\":true,\"rapid_groups_atomic\":true,\"other_groups_preserved\":true,\"tab_only_ui\":true,\"actual_leaf_terrain_water_pixels\":true,\"original_restored\":true,\"actual_reset\":true,\"faulted\":0,\"unsafe\":0,\"warm_material_creations\":%llu,\"peak_programs\":%lu,\"physical_panel_pixels\":%llu,\"font_pixels\":%llu,\"overlay_draws\":%lu}\n",both.shader_creations,measured.peak_retained_programs,physical[0],physical[1],core.overlay_draws);
}
}

int wmain(int argc,wchar_t** argv) {
    try {
        const bool combined=argc==6&&!std::wcscmp(argv[3],L"--combined");
        const bool overlay=argc==5&&!std::wcscmp(argv[3],L"--water-palette")&&!std::wcscmp(argv[4],L"--shader-overlay");
        const bool leaf_overlay=argc==5&&!std::wcscmp(argv[3],L"--v4")&&!std::wcscmp(argv[4],L"--shader-overlay");
        Require(combined||argc==3 || argc==5 || (argc==4&&(!std::wcscmp(argv[3],L"--v3")||!std::wcscmp(argv[3],L"--v4")||!std::wcscmp(argv[3],L"--water-palette"))),"usage_effects_gpu_dxvk_output_optional_catalog_sha256_or_v3_v4_water_palette");
        const bool compare=argc==5;
        const auto output=std::filesystem::absolute(argv[2]);
        Require(!std::filesystem::exists(output) && std::filesystem::create_directory(output),"effects_gpu_use_new_output_directory");
        if(combined){CombinedTrial(std::filesystem::absolute(argv[1]),output,argv[4],argv[5]);return 0;}
        if(overlay){WaterPaletteTrial(std::filesystem::absolute(argv[1]),output,true);return 0;}
        if(leaf_overlay){V4Trial(std::filesystem::absolute(argv[1]),output,true);return 0;}
        if(argc==4){if(!std::wcscmp(argv[3],L"--water-palette"))WaterPaletteTrial(std::filesystem::absolute(argv[1]),output);
            else if(!std::wcscmp(argv[3],L"--v4"))V4Trial(std::filesystem::absolute(argv[1]),output);
            else V3Trial(std::filesystem::absolute(argv[1]),output);return 0;}
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
