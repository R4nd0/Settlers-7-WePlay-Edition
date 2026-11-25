// Integrated public-protocol GPU proof. All rendered shaders/geometry are
// authored; source identities are overridden only in this fixture executable.
#define wmain WaterHookReferenceMain
#include "supersampling_probe.cpp"
#undef wmain
#include "supersampling_hook_config.h"
#include "supersampling_hook_capture.h"
#include "material_shader_effects.h"
#include "water_shader_effects.h"
#include <d3dcompiler.h>

namespace {
constexpr UINT WHWidth=800,WHHeight=600;
void WHCompile(const char* source,const char* target,size_t wanted,DWORD marker,Com<ID3DBlob>& output){
    Com<ID3DBlob> code,error;
    const HRESULT result=D3DCompile(source,std::strlen(source),"authored-integrated-water",nullptr,nullptr,"main",target,
        D3DCOMPILE_OPTIMIZATION_LEVEL3,0,code.put(),error.put());
    if(FAILED(result)&&error.get())std::fprintf(stderr,"%s\n",static_cast<const char*>(error->GetBufferPointer()));
    Check(result,"water_hook_authored_compile_failed");
    Require(code->GetBufferSize()+8<=wanted&&!(wanted%4),"water_hook_padding_bounds");
    Check(D3DCreateBlob(wanted,output.put()),"water_hook_padded_blob_failed");
    auto* out=static_cast<DWORD*>(output->GetBufferPointer());const size_t old=code->GetBufferSize()/4,count=wanted/4;
    const auto* original=static_cast<const DWORD*>(code->GetBufferPointer());Require(original[old-1]==0xffff,"water_hook_source_end");
    std::memcpy(out,original,(old-1)*4);out[old-1]=0xfffeu|static_cast<DWORD>((count-old-1)<<16);
    for(size_t i=old;i<count-1;++i)out[i]=marker;out[count-1]=0xffff;
}
std::array<unsigned char,32> WHDigest(ID3DBlob* data){
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> result{};
    Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"water_hook_hash_provider");
    const bool valid=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0
        &&BCryptHashData(hash,static_cast<PUCHAR>(data->GetBufferPointer()),static_cast<ULONG>(data->GetBufferSize()),0)>=0
        &&BCryptFinishHash(hash,result.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);Require(valid,"water_hook_digest");return result;
}
std::wstring WHHex(const std::array<unsigned char,32>& hash){std::wstring result;for(auto v:hash){constexpr wchar_t digits[]=L"0123456789abcdef";result+=digits[v>>4];result+=digits[v&15];}return result;}
struct WHGeometry {
    Com<IDirect3DVertexBuffer9> buffer;Com<IDirect3DVertexDeclaration9> declaration;
    explicit WHGeometry(IDirect3DDevice9* device){
        const float vertices[][4]={{-1,-1,0,1},{-1,1,0,1},{1,-1,0,1},{1,1,0,1}};
        Check(device->CreateVertexBuffer(sizeof(vertices),0,0,D3DPOOL_MANAGED,buffer.put(),nullptr),"water_hook_vertex_buffer");
        void* bytes=nullptr;Check(buffer->Lock(0,sizeof(vertices),&bytes,0),"water_hook_vb_lock");std::memcpy(bytes,vertices,sizeof(vertices));Check(buffer->Unlock(),"water_hook_vb_unlock");
        const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
        Check(device->CreateVertexDeclaration(elements,declaration.put()),"water_hook_vertex_declaration");
    }
    void Bind(IDirect3DDevice9* device){Check(device->SetVertexDeclaration(declaration.get()),"water_hook_decl_bind");Check(device->SetStreamSource(0,buffer.get(),0,16),"water_hook_vb_bind");}
};
constexpr float WHFog[4]={.1f,.2f,.3f,.4f},WHReflection[4]={.2f,.3f,.4f,.5f};
void WHDraw(IDirect3DDevice9* device,WHGeometry& geometry,IDirect3DVertexShader9* vertex,IDirect3DPixelShader9* pixel){
    geometry.Bind(device);Check(device->SetVertexShader(vertex),"water_hook_original_vs");Check(device->SetPixelShader(pixel),"water_hook_original_ps");
    Check(device->SetVertexShaderConstantF(68,WHFog,1),"water_hook_original_density");Check(device->SetVertexShaderConstantF(69,WHReflection,1),"water_hook_original_reflection");
    Check(device->SetRenderState(D3DRS_ZENABLE,FALSE),"water_hook_depth");Check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"water_hook_cull");
    Check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"water_hook_blend");Check(device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE),"water_hook_alpha");
    Check(device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE),"water_hook_srgb");D3DVIEWPORT9 viewport{0,0,WHWidth,WHHeight,0,1};Check(device->SetViewport(&viewport),"water_hook_viewport");
    Check(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff202020,1,0),"water_hook_clear");Check(device->BeginScene(),"water_hook_begin");
    Check(device->DrawPrimitive(D3DPT_TRIANGLESTRIP,0,2),"water_hook_real_draw");Check(device->EndScene(),"water_hook_end");
    Com<IDirect3DVertexShader9> vs;Com<IDirect3DPixelShader9> ps;float fog[4]{},reflection[4]{};
    Check(device->GetVertexShader(vs.put()),"water_hook_restored_vs");Check(device->GetPixelShader(ps.put()),"water_hook_restored_ps");
    Check(device->GetVertexShaderConstantF(68,fog,1),"water_hook_read_density");Check(device->GetVertexShaderConstantF(69,reflection,1),"water_hook_read_reflection");
    Require(vs.get()==vertex&&ps.get()==pixel&&!std::memcmp(fog,WHFog,16)&&!std::memcmp(reflection,WHReflection,16),"water_hook_original_program_and_classes_not_restored");
}
void WHPresent(IDirect3DDevice9* device){Check(device->Present(nullptr,nullptr,nullptr,nullptr),"water_hook_present");}
SupersamplingStats WHStats(){SupersamplingStats value{};Require(WePlaySupersamplingReadStats(&value)==1,"water_hook_read_core_stats");return value;}
WaterShaderEffects::Stats WHWater(){WaterShaderEffects::Stats value{};Require(WaterShaderEffects::ReadStats(&value),"water_hook_read_water_stats");return value;}
std::vector<unsigned char> WHRead(const std::filesystem::path& path){std::ifstream file(path,std::ios::binary);Require(file.good(),"water_hook_capture_file_missing");
    return {std::istreambuf_iterator<char>(file),std::istreambuf_iterator<char>()};}
std::uint64_t WHNumber(const std::string& text,const char* key){const auto at=text.find(std::string("\"")+key+"\":");Require(at!=std::string::npos,"water_hook_capture_scalar_missing");return std::stoull(text.substr(at+std::strlen(key)+3));}
void WHCapture(IDirect3DDevice9* device,const std::filesystem::path& output,DWORD sequence,bool enabled){
    NativeGraphicsCapture::Request request{sizeof(request),1,sequence};Require(WePlayNativeGraphicsRequestCapture(&request)==1,"water_hook_public_capture_queue");WHPresent(device);
    const auto bytes=WHRead(output/(L"capture-"+std::to_wstring(sequence)+L".json"));const std::string text(bytes.begin(),bytes.end());
    Require(text.find("\"complete\":true")!=std::string::npos&&text.find("\"fallback_used\":false")!=std::string::npos,"water_hook_capture_incomplete");
    const auto water_at=text.find("\"water\":{");Require(water_at!=std::string::npos,"water_hook_capture_water_missing");const auto water=text.substr(water_at+8);
    Require(WHNumber(water,"frame")==WHNumber(text,"frame")&&WHNumber(water,"enabled")==static_cast<unsigned>(enabled)
        &&!WHNumber(water,"faulted")&&!WHNumber(water,"unsafe"),"water_hook_capture_frame_origin_mismatch");
    if(enabled)Require(WHNumber(water,"completed_applications")==1&&WHNumber(water,"completed_restorations")==1
        &&WHNumber(water,"last_applied_frame")==WHNumber(text,"frame"),"water_hook_capture_fresh_application_missing");
}
}

int wmain(int argc,wchar_t** argv){try{
    Require(argc==3,"usage_water_hook_gpu_owned_dxvk_new_output");const auto output=std::filesystem::absolute(argv[2]);
    Require(!std::filesystem::exists(output)&&std::filesystem::create_directory(output),"water_hook_fresh_output");
    Com<ID3DBlob> vertex_code,pixel_code,candidate_code;
    WHCompile("struct O{float4 p:POSITION;float4 c:COLOR0;};O main(float4 p:POSITION){O o;o.p=p;o.c=float4(.1,.2,.3,1);return o;}","vs_3_0",2592,1,vertex_code);
    WHCompile("float4 main(float4 c:COLOR0):COLOR0{return c;}","ps_3_0",3740,2,pixel_code);
    WHCompile("float4 main(float4 c:COLOR0):COLOR0{return float4(.7,.4,.2,c.a);}","ps_3_0",3840,3,candidate_code);
    const auto vh=WHDigest(vertex_code.get()),ph=WHDigest(pixel_code.get()),ch=WHDigest(candidate_code.get());
    Require(WaterShaderEffects::FixtureIdentities(vh.data(),ph.data(),ch.data()),"water_hook_authored_identities");
    const auto recipe=(output/L"LocalData/MaterialShaders/river-depth-fresnel-v1-candidate").make_preferred();std::filesystem::create_directories(recipe);
    const auto candidate=recipe/(WHHex(ch)+L".ps.bin");{std::ofstream file(candidate,std::ios::binary);file.write(static_cast<const char*>(candidate_code->GetBufferPointer()),candidate_code->GetBufferSize());file.close();Require(file.good(),"water_hook_save_authored_candidate");}
    SetProcessDPIAware();Runtime runtime(std::filesystem::absolute(argv[1]));Com<IDirect3D9> api;*api.put()=runtime.create();Require(api.get()!=nullptr,"water_hook_factory");
    Window window(WHWidth,WHHeight);window.show();D3DPRESENT_PARAMETERS parameters{};parameters.Windowed=TRUE;parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;
    parameters.hDeviceWindow=window.get();parameters.BackBufferWidth=WHWidth;parameters.BackBufferHeight=WHHeight;parameters.BackBufferFormat=D3DFMT_A8R8G8B8;
    parameters.BackBufferCount=1;parameters.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    Com<IDirect3DDevice9> device;Check(api->CreateDevice(0,D3DDEVTYPE_HAL,window.get(),D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&parameters,device.put()),"water_hook_device");
    Com<IDirect3DVertexShader9> vertex;Com<IDirect3DPixelShader9> pixel;
    Check(device->CreateVertexShader(static_cast<const DWORD*>(vertex_code->GetBufferPointer()),vertex.put()),"water_hook_create_authored_vs");
    Check(device->CreatePixelShader(static_cast<const DWORD*>(pixel_code->GetBufferPointer()),pixel.put()),"water_hook_create_authored_ps");WHGeometry geometry(device.get());
    NativeGraphicsConfig::Version2 native{};native.base={sizeof(native),2,WHWidth,WHHeight,WHWidth,WHHeight,{}};native.flags=NativeGraphicsConfig::Water;
    const auto report=(output/L"hook.private.json").wstring();std::copy(report.begin(),report.end(),native.base.report_path);
    Require(WePlaySupersamplingInitialize(&native)==1,"water_hook_PUBLIC_initialize");
    MaterialShaderEffects::Config bootstrap{};bootstrap.render_width=WHWidth;bootstrap.render_height=WHHeight;
    const auto effects_report=(output/L"effects.private.json").wstring();std::copy(effects_report.begin(),effects_report.end(),bootstrap.report_path);
    Require(WePlayNativeGraphicsConfigureEffects(&bootstrap)==1,"water_hook_PUBLIC_zero_effects_bootstrap");
    Require(WePlaySupersamplingAttach(device.get())==1,"water_hook_owned_attach");
    for(unsigned i=0;i<3;++i){WHDraw(device.get(),geometry,vertex.get(),pixel.get());WHPresent(device.get());}
    WaterShaderEffects::Config water{};water.render_width=WHWidth;water.render_height=WHHeight;
    Require(WePlayNativeGraphicsConfigureWater(&water)==1,"water_hook_PUBLIC_initial_off_queue");
    WHDraw(device.get(),geometry,vertex.get(),pixel.get());WHPresent(device.get());const DWORD origin=WHStats().water_frame_origin;Require(origin>=4,"water_hook_nonzero_late_module_origin");
    WHDraw(device.get(),geometry,vertex.get(),pixel.get());WHCapture(device.get(),output,1,false);
    water.enabled=1;const auto path=candidate.wstring();Require(path.size()<512,"water_hook_path_bound");std::copy(path.begin(),path.end(),water.pixel_path);std::memcpy(water.pixel_sha256,ch.data(),32);
    Require(WePlayNativeGraphicsConfigureWater(&water)==1,"water_hook_PUBLIC_enable_queue");WHDraw(device.get(),geometry,vertex.get(),pixel.get());WHPresent(device.get());
    Require(WHWater().configured&&WHWater().enabled&&!WHStats().capture_failures,"water_hook_queued_enable_applied");
    NativeGraphicsCapture::Request stale{sizeof(stale),1,2};Require(WePlayNativeGraphicsRequestCapture(&stale)==1,"water_hook_capture_pending");WHPresent(device.get());
    Require(WHStats().capture_completed==1&&!std::filesystem::exists(output/L"capture-2.json"),"water_hook_no_draw_epoch_must_defer");
    WHDraw(device.get(),geometry,vertex.get(),pixel.get());WHPresent(device.get());Require(WHStats().capture_completed==2,"water_hook_fresh_candidate_captured");
    // Inspect the existing public capture without issuing a second request.
    {const auto bytes=WHRead(output/L"capture-2.json");const std::string text(bytes.begin(),bytes.end());const auto at=text.find("\"water\":{");Require(at!=std::string::npos,"water_hook_on_water_metadata");const auto state=text.substr(at+8);
        Require(WHNumber(state,"frame")==WHNumber(text,"frame")&&WHNumber(state,"completed_applications")==1&&WHNumber(state,"completed_restorations")==1&&WHNumber(state,"enabled")==1,"water_hook_on_capture_origin_and_application");}
    const auto warm=WHWater();for(unsigned i=0;i<4;++i){WHDraw(device.get(),geometry,vertex.get(),pixel.get());WHPresent(device.get());}
    Require(WHWater().shader_creations==warm.shader_creations&&WHWater().retained_programs==1,"water_hook_integrated_warm_cache");
    water.enabled=0;Require(WePlayNativeGraphicsConfigureWater(&water)==1,"water_hook_PUBLIC_off_queue");WHDraw(device.get(),geometry,vertex.get(),pixel.get());WHPresent(device.get());
    WHDraw(device.get(),geometry,vertex.get(),pixel.get());WHCapture(device.get(),output,3,false);
    Require(WHRead(output/L"capture-1-source.bmp")==WHRead(output/L"capture-3-source.bmp"),"water_hook_off_on_off_bitwise_rollback");
    Require(WHRead(output/L"capture-1-source.bmp")!=WHRead(output/L"capture-2-source.bmp"),"water_hook_integrated_real_GPU_pixel_response");
    Check(device->Reset(&parameters),"water_hook_actual_core_Reset");water.enabled=1;Require(WePlayNativeGraphicsConfigureWater(&water)==1,"water_hook_PUBLIC_reenable_after_Reset");
    WHDraw(device.get(),geometry,vertex.get(),pixel.get());WHPresent(device.get());WHDraw(device.get(),geometry,vertex.get(),pixel.get());WHCapture(device.get(),output,4,true);
    Require(WHRead(output/L"capture-2-source.bmp")==WHRead(output/L"capture-4-source.bmp"),"water_hook_Reset_reenable_identical_pixels");
    water.enabled=0;Require(WePlayNativeGraphicsConfigureWater(&water)==1,"water_hook_final_disable");WHDraw(device.get(),geometry,vertex.get(),pixel.get());WHPresent(device.get());
    const auto final=WHStats();const auto module=WHWater();Require(!final.render_unsafe&&!final.water_unsafe&&!final.last_failure&&!module.faulted&&!module.pending_original_refs,"water_hook_clean_final_state");
    Require(final.effects_config_version==1&&final.effects_active==0&&final.water_protocol_received==1,"water_hook_protocol_no_legacy_River");
    Require(WePlaySupersamplingDetach(device.get())==1,"water_hook_detach");
    std::printf("{\"format\":\"s7-integrated-water-hook-gpu-v1\",\"passed\":true,\"authored_shaders_only\":true,\"public_initialize\":true,\"zero_effects_bootstrap\":true,\"queued_water1076\":true,\"nonzero_water_frame_origin\":%lu,\"off_on_off_exact\":true,\"fresh_frame_required\":true,\"warm_cache_reused\":true,\"original_PS_and_VS68_69_restored\":true,\"actual_Reset_reenable\":true,\"captures\":%lu,\"presents\":%lu,\"game_tested\":false,\"visible_game_quality_qualified\":false}\n",origin,final.capture_completed,final.present_calls);
    return 0;
}catch(const std::exception& error){std::fprintf(stderr,"Integrated water GPU fixture failed: %s\n",error.what());return 1;}}
