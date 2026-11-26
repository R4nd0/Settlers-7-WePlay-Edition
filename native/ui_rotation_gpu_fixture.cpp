// Recipient-original UI programs with authored packed-vertex test inputs.
// Never loads a game; no original programs are embedded or redistributed.
#define wmain SupersamplingReferenceMain
#include "supersampling_probe.cpp"
#undef wmain
#include "ui_rotation_fix.h"
#include <intrin.h>

namespace {
std::vector<DWORD> UiRead(const std::filesystem::path& path,size_t bytes) {
    Require(std::filesystem::file_size(path)==bytes,"ui_fixture_input_size");
    std::ifstream file(path,std::ios::binary);std::vector<DWORD> out(bytes/4);
    file.read(reinterpret_cast<char*>(out.data()),bytes);Require(file.gcount()==static_cast<std::streamsize>(bytes),"ui_fixture_input_read");return out;
}
std::string UiHash(const std::vector<DWORD>& words) {
    BCRYPT_ALG_HANDLE algorithm=nullptr;BCRYPT_HASH_HANDLE hash=nullptr;std::array<unsigned char,32> bytes{};
    Require(BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0,"ui_fixture_hash_provider");
    bool ok=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    if(ok)ok=BCryptHashData(hash,reinterpret_cast<PUCHAR>(const_cast<DWORD*>(words.data())),static_cast<ULONG>(words.size()*4),0)>=0
        &&BCryptFinishHash(hash,bytes.data(),32,0)>=0;
    if(hash)BCryptDestroyHash(hash);BCryptCloseAlgorithmProvider(algorithm,0);Require(ok,"ui_fixture_hash");
    const char hex[]="0123456789abcdef";std::string out;for(auto b:bytes){out+=hex[b>>4];out+=hex[b&15];}return out;
}
struct UiVertex {float x,y;short tex[4],transform[4];DWORD color;};
static_assert(sizeof(UiVertex)==28,"authored original UI vertex layout");
void UiRestored(IDirect3DDevice9* device,IDirect3DVertexShader9* expected) {
    Com<IDirect3DVertexShader9> got;Check(device->GetVertexShader(got.put()),"ui_fixture_read_shader");Require(got.get()==expected,"ui_fixture_original_vs_not_restored");
}
}
int wmain(int argc,wchar_t** argv) {
    try {
        Require(argc==4,"ui_fixture_renderer_recipient_programs_output");
        const auto output=std::filesystem::absolute(argv[3]);Require(!std::filesystem::exists(output)&&std::filesystem::create_directory(output),"ui_fixture_new_output");
        const auto programs=std::filesystem::absolute(argv[2]);
        const auto vs_code=UiRead(programs/L"original-ui.vs.bin",484),ps_code=UiRead(programs/L"original-ui.ps.bin",1108);
        Require(UiHash(vs_code)=="0fafd8564b812ef5d8609329052df4be741f015c3c3b51b0946420e0fe6880f3"
            &&UiHash(ps_code)=="69b5a31786c16faf4c06b1592c6925f745230ac27d7729db78d330fdf084aae6","ui_fixture_recipient_program_pins");
        SetProcessDPIAware();Runtime runtime(std::filesystem::absolute(argv[1]));Com<IDirect3D9> api;*api.put()=runtime.create();
        Require(api.get()!=nullptr,"ui_fixture_factory");Window window(640,360);D3DPRESENT_PARAMETERS p{};
        p.Windowed=TRUE;p.SwapEffect=D3DSWAPEFFECT_DISCARD;p.hDeviceWindow=window.get();p.BackBufferWidth=3840;p.BackBufferHeight=2160;
        p.BackBufferFormat=D3DFMT_A8R8G8B8;p.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;Check(api->CreateDevice(0,D3DDEVTYPE_HAL,window.get(),D3DCREATE_HARDWARE_VERTEXPROCESSING,&p,device.put()),"ui_fixture_device");
        Com<IDirect3DVertexShader9> vs;Com<IDirect3DPixelShader9> ps;
        Check(device->CreateVertexShader(vs_code.data(),vs.put()),"ui_fixture_vs");Check(device->CreatePixelShader(ps_code.data(),ps.put()),"ui_fixture_ps");
        Com<IDirect3DVertexDeclaration9> decl;const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT2,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},
            {0,24,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},
            {0,8,D3DDECLTYPE_SHORT4N,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},{0,16,D3DDECLTYPE_SHORT4N,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,1},D3DDECL_END()};
        Check(device->CreateVertexDeclaration(elements,decl.put()),"ui_fixture_declaration");
        Com<IDirect3DVertexBuffer9> vb;Check(device->CreateVertexBuffer(4*sizeof(UiVertex),0,0,D3DPOOL_MANAGED,vb.put(),nullptr),"ui_fixture_vb");
        UiRotationFix::Initialize(3840,2160);UiRotationFix::Config mode{};
        auto bind=[&](float cx,float cy,float angle) {
            const short px=static_cast<short>(static_cast<int>(std::round(cx*10))),py=static_cast<short>(static_cast<int>(std::round(cy*10)));
            const short sine=static_cast<short>(std::round(std::sin(angle)*32767)),cosine=static_cast<short>(std::round(std::cos(angle)*32767));
            const UiVertex vertices[]={{cx-40,cy-20,{0,0,4,2},{px,py,sine,cosine},0xffff2040},
                {cx-40,cy+20,{0,0,4,2},{px,py,sine,cosine},0xffff2040},{cx+40,cy-20,{0,0,4,2},{px,py,sine,cosine},0xffff2040},
                {cx+40,cy+20,{0,0,4,2},{px,py,sine,cosine},0xffff2040}};
            void* data=nullptr;Check(vb->Lock(0,sizeof(vertices),&data,0),"ui_fixture_vb_lock");std::memcpy(data,vertices,sizeof(vertices));Check(vb->Unlock(),"ui_fixture_vb_unlock");
            Check(device->SetStreamSource(0,vb.get(),0,sizeof(UiVertex)),"ui_fixture_stream");Check(device->SetVertexDeclaration(decl.get()),"ui_fixture_decl_bind");
            Check(device->SetVertexShader(vs.get()),"ui_fixture_vs_bind");Check(device->SetPixelShader(ps.get()),"ui_fixture_ps_bind");
            float matrix[16]={2.f/3840,0,0,0,0,-2.f/2160,0,0,0,0,1,0,-1.f+1.f/3840,1.f-1.f/2160,0,1};
            Check(device->SetVertexShaderConstantF(64,matrix,4),"ui_fixture_projection");
            Check(device->SetRenderState(D3DRS_ZENABLE,FALSE),"ui_fixture_depth");Check(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE),"ui_fixture_cull");
            Check(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE),"ui_fixture_blend");Check(device->SetRenderState(D3DRS_ALPHATESTENABLE,FALSE),"ui_fixture_alpha");
            Check(device->SetRenderState(D3DRS_SRGBWRITEENABLE,FALSE),"ui_fixture_srgb");
        };
        auto frame=[&](bool enabled,float cx,float cy,float angle) {
            mode.enabled=enabled?1u:0u;Require(UiRotationFix::Configure(mode),"ui_fixture_configure");bind(cx,cy,angle);
            Check(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0),"ui_fixture_clear");Check(device->BeginScene(),"ui_fixture_begin");
            Check(UiRotationFix::Execute(device.get(),[&](){return device->DrawPrimitive(D3DPT_TRIANGLESTRIP,0,2);}),"ui_fixture_draw");
            Check(device->EndScene(),"ui_fixture_end");UiRestored(device.get(),vs.get());
            Com<IDirect3DSurface9> surface;Check(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,surface.put()),"ui_fixture_target");
            return Readback(device.get(),surface.get(),3840,2160);
        };
        auto colored=[](const std::vector<std::uint32_t>& pixels,unsigned x,unsigned y){return (pixels[y*3840+x]&0xffffffu)==0xff2040u;};
        const auto off=frame(false,3600,200,-.14f);const auto on=frame(true,3600,200,-.14f);
        Require(!colored(off,3600,200)&&colored(on,3600,200),"ui_fixture_wrapped_pivot_not_corrected");
        const float decoded=static_cast<float>(static_cast<short>(36000))/32767.f*3276.7f;
        const float sine=static_cast<float>(static_cast<short>(std::round(std::sin(-.14f)*32767)))/32767.f;
        const float cosine=static_cast<float>(static_cast<short>(std::round(std::cos(-.14f)*32767)))/32767.f;
        const auto wrong_x=static_cast<unsigned>(std::round(cosine*(3600-decoded)+decoded));
        const auto wrong_y=static_cast<unsigned>(std::round(-sine*(3600-decoded)+200));
        Require(colored(off,wrong_x,wrong_y)&&!colored(on,wrong_x,wrong_y),"ui_fixture_displaced_shape_remains");
        Require(frame(false,1800.3f,200.4f,-.14f)==frame(true,1800.3f,200.4f,-.14f),"ui_fixture_unwrapped_pixels_changed");
        Require(frame(false,3600,200,0)==frame(true,3600,200,0),"ui_fixture_identity_rotation_pixels_changed");
        Require(frame(false,-10,200,.14f)==frame(true,-10,200,.14f),"ui_fixture_negative_unwrapped_pixels_changed");
        mode.enabled=1;Require(UiRotationFix::Configure(mode),"ui_fixture_failure_enable");bind(3600,200,-.14f);
        SetLastError(0x12345678);const unsigned mxcsr=_mm_getcsr();
        const HRESULT failed=UiRotationFix::Execute(device.get(),[&](){Require(GetLastError()==0x12345678&&_mm_getcsr()==mxcsr,"ui_fixture_input_environment");SetLastError(0x34567890);return D3DERR_INVALIDCALL;});
        Require(failed==D3DERR_INVALIDCALL&&GetLastError()==0x34567890&&_mm_getcsr()==mxcsr,"ui_fixture_failed_draw_environment");UiRestored(device.get(),vs.get());
        bool caught=false;try{UiRotationFix::Execute(device.get(),[]()->HRESULT{throw std::runtime_error("authored-original-failure");});}
        catch(const std::runtime_error&){caught=true;}
        Require(caught&&UiRotationFix::Safe(),"ui_fixture_original_exception_restore");UiRestored(device.get(),vs.get());
        const auto before=UiRotationFix::Read();Require(before.creations==1&&before.applications==before.restorations
            &&before.failed_draws==2&&!before.set_failures&&!before.restore_failures&&!before.shader_failures,"ui_fixture_transaction_counts");
        Require(UiRotationFix::Reset(device.get()),"ui_fixture_cache_reset");Check(device->Reset(&p),"ui_fixture_actual_reset");
        Require(colored(frame(true,3600,200,-.14f),3600,200),"ui_fixture_after_reset");
        const auto final=UiRotationFix::Read();Require(final.creations==2&&final.applications==final.restorations,"ui_fixture_recreated_balanced");
        Require(UiRotationFix::Reset(device.get()),"ui_fixture_final_reset");
        device->SetStreamSource(0,nullptr,0,0);device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);
        std::printf("{\"format\":\"s7-ui-rotation-gpu-v1\",\"game_tested\":false,\"recipient_original_programs\":true,\"authored_vertices\":true,\"wrap_reproduced\":true,\"wrap_corrected\":true,\"unwrapped_pixels_identical\":true,\"identity_rotation_pixels_identical\":true,\"negative_pivot_pixels_identical\":true,\"failed_draw_and_exception_restored\":true,\"actual_reset\":true,\"wrong_center\":[%u,%u],\"correct_center\":[3600,200],\"applications\":%llu,\"restorations\":%llu,\"creations\":%llu}\n",wrong_x,wrong_y,final.applications,final.restorations,final.creations);
        return 0;
    }catch(const std::exception& e){std::fprintf(stderr,"ui rotation GPU fixture failed: %s\n",e.what());return 1;}
}
