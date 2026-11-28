// Authored native-driver jitter conformance, no game data or game launch.
#include "on12_native_temporal.h"
#include "on12_scene_projection.h"
#include "on12_jitter_program.h"
#ifdef S7_ORDERED_PROJECTION_GPU_FIXTURE
#include "on12_projection_jitter.h"
#endif
#include "water_on12_identity.h"
#include <d3d12.h>
#include <d3d9on12.h>
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <cstdio>
#include <cmath>

using Microsoft::WRL::ComPtr;
namespace {
unsigned checks=0;bool Check(bool good,unsigned line){++checks;if(!good)std::fprintf(stderr,"native temporal GPU check failed at %u\n",line);return good;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
LRESULT CALLBACK WindowProc(HWND w,UINT m,WPARAM a,LPARAM b){return DefWindowProcW(w,m,a,b);}
struct Vertex {float x,y,z,w;DWORD color;};
bool Pixels(IDirect3DDevice9* device,std::vector<DWORD>& output){
    ComPtr<IDirect3DSurface9> source,copy;D3DLOCKED_RECT r{};
    if(FAILED(device->GetRenderTarget(0,&source))||FAILED(device->CreateOffscreenPlainSurface(64,64,D3DFMT_A8R8G8B8,D3DPOOL_SYSTEMMEM,&copy,nullptr))
        ||FAILED(device->GetRenderTargetData(source.Get(),copy.Get()))||FAILED(copy->LockRect(&r,nullptr,D3DLOCK_READONLY)))return false;
    output.resize(64*64);for(UINT y=0;y<64;++y)std::memcpy(output.data()+y*64,static_cast<const unsigned char*>(r.pBits)+y*r.Pitch,64*4);
    return SUCCEEDED(copy->UnlockRect());
}
ComPtr<ID3DBlob> Compile(const char* source,const char* entry,const char* target){ComPtr<ID3DBlob> code,error;
    const auto hr=D3DCompile(source,std::strlen(source),"authored-native-temporal",nullptr,nullptr,entry,target,D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3,0,&code,&error);
    if(FAILED(hr)&&error)std::fprintf(stderr,"%s",static_cast<const char*>(error->GetBufferPointer()));return code;
}
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2||wcslen(argv[1])!=64)return 1;std::string expected;
    for(size_t i=0;i<64;++i){if((argv[1][i]<L'0'||argv[1][i]>L'9')&&(argv[1][i]<L'a'||argv[1][i]>L'f'))return 1;expected+=static_cast<char>(argv[1][i]);}
    WNDCLASSW wc{};wc.lpfnWndProc=WindowProc;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"WePlayNativeTemporalFixture";
    REQUIRE(RegisterClassW(&wc));HWND window=CreateWindowW(wc.lpszClassName,L"Native temporal fixture",WS_OVERLAPPEDWINDOW,0,0,96,96,nullptr,nullptr,wc.hInstance,nullptr);REQUIRE(window);
    HMODULE module=LoadLibraryW(L"d3d9.dll");REQUIRE(module);using Create=IDirect3D9*(WINAPI*)(UINT);
    const auto create=reinterpret_cast<Create>(GetProcAddress(module,"Direct3DCreate9"));REQUIRE(create);
    ComPtr<IDirect3D9> api;api.Attach(create(D3D_SDK_VERSION));REQUIRE(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9> wrapped,device;REQUIRE(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&wrapped)));
    REQUIRE(WaterOn12Identity::Module(GetModuleHandleW(L"dxwrapper.dll"),WaterOn12Identity::Wrapper));
    HMODULE runtime=GetModuleHandleW(L"d3d9on12.dll");REQUIRE(WaterOn12Identity::Module(runtime,expected.c_str()));
    ComPtr<IDirect3DDevice9On12> bridge;ComPtr<ID3D12Device> d12;
    REQUIRE(SUCCEEDED(wrapped.As(&bridge))&&SUCCEEDED(bridge.As(&device))&&SUCCEEDED(bridge->GetD3D12Device(IID_PPV_ARGS(&d12))));
    using Call=DWORD(WINAPI*)(void*);const auto control=reinterpret_cast<Call>(GetProcAddress(runtime,"WePlayOn12TemporalControl")),query=reinterpret_cast<Call>(GetProcAddress(runtime,"WePlayOn12TemporalQuery"));
#ifndef S7_ORDERED_PROJECTION_GPU_FIXTURE
    REQUIRE(control&&query&&WaterOn12Identity::Belongs(reinterpret_cast<void*>(control),runtime)&&WaterOn12Identity::Belongs(reinterpret_cast<void*>(query),runtime));
#else
    (void)control;(void)query;
#endif
    const char* vs_source="float4x4 g_VSMainViewProjectionMatrix:register(c12);struct I{float4 p:POSITION0;float4 c:COLOR0;};struct O{float4 p:POSITION0;float4 c:COLOR0;};O VS(I i){O o;o.p=mul(g_VSMainViewProjectionMatrix,i.p);o.c=i.c;return o;}";
    auto vs=Compile(vs_source,"VS","vs_3_0"),ps=Compile("float4 PS(float4 c:COLOR0):COLOR0{return c;}","PS","ps_3_0");REQUIRE(vs&&ps);
    ComPtr<IDirect3DVertexShader9> original,reference,ui;ComPtr<IDirect3DPixelShader9> pixel;
    On12Graphics::JitterProgram patch;REQUIRE(On12Graphics::MakeJitterProgram(vs->GetBufferPointer(),vs->GetBufferSize(),patch));
    REQUIRE(SUCCEEDED(device->CreateVertexShader(static_cast<const DWORD*>(vs->GetBufferPointer()),&original))&&SUCCEEDED(device->CreateVertexShader(patch.words.data(),&reference)));
    auto ui_source=std::string(vs_source);
    for(auto at=ui_source.find("g_VSMainViewProjectionMatrix");at!=std::string::npos;at=ui_source.find("g_VSMainViewProjectionMatrix"))ui_source.replace(at,std::strlen("g_VSMainViewProjectionMatrix"),"g_UiProjection");
    auto ui_code=Compile(ui_source.c_str(),"VS","vs_3_0");REQUIRE(ui_code);
    REQUIRE(SUCCEEDED(device->CreateVertexShader(static_cast<const DWORD*>(ui_code->GetBufferPointer()),&ui))&&SUCCEEDED(device->CreatePixelShader(static_cast<const DWORD*>(ps->GetBufferPointer()),&pixel)));
    const D3DVERTEXELEMENT9 elements[]{{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_D3DCOLOR,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_COLOR,0},D3DDECL_END()};
    ComPtr<IDirect3DVertexDeclaration9> declaration;REQUIRE(SUCCEEDED(device->CreateVertexDeclaration(elements,&declaration))&&SUCCEEDED(device->SetVertexDeclaration(declaration.Get())));
    REQUIRE(SUCCEEDED(device->SetVertexShader(original.Get()))&&SUCCEEDED(device->SetPixelShader(pixel.Get())));
    REQUIRE(SUCCEEDED(device->SetRenderState(D3DRS_ZENABLE,FALSE))&&SUCCEEDED(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))&&SUCCEEDED(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE)));
    const float matrix[]{1.2f,0,0,0,0,1.5f,0,0,0,0,0.9f,1,0,0,-0.1f,0};REQUIRE(SUCCEEDED(device->SetVertexShaderConstantF(12,matrix,4)));
    const Vertex vertices[]{{-0.8f,-0.7f,1,1,0xffff0000},{0.8f,-0.4f,1.2f,1,0xff00ff00},{0.1f,0.9f,1.5f,1,0xff0000ff}};
    auto draw=[&](std::vector<DWORD>& pixels){if(FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0))||FAILED(device->BeginScene()))return false;
        const auto result=device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,vertices,sizeof(Vertex));const auto ended=device->EndScene();return SUCCEEDED(result)&&SUCCEEDED(ended)&&Pixels(device.Get(),pixels);};
    std::vector<DWORD> baseline,shifted,comparison,ui_off,ui_on,shadow_off,shadow_on;REQUIRE(draw(baseline));
#ifdef S7_ORDERED_PROJECTION_GPU_FIXTURE
    On12Graphics::ProjectionJitter ordered;
    auto** table=*reinterpret_cast<void***>(device.Get());
    REQUIRE(SUCCEEDED(ordered.Initialize(device.Get(),reinterpret_cast<On12Graphics::ProjectionJitter::Set>(table[94]),reinterpret_cast<On12Graphics::ProjectionJitter::Get>(table[95]))));
    REQUIRE(SUCCEEDED(ordered.Begin(2,64,64,true))&&SUCCEEDED(ordered.BeforeDraw(12,64,64))&&draw(shifted));
    float logical[16]{};REQUIRE(SUCCEEDED(ordered.GetConstants(12,logical,4))&&!std::memcmp(matrix,logical,sizeof(matrix)));
    REQUIRE(SUCCEEDED(ordered.Ui()));
    const auto offset=ordered.Jitter();const float jitter[]{2*offset[0]/64,-2*offset[1]/64,0,0};
    REQUIRE(SUCCEEDED(device->SetVertexShader(reference.Get()))&&SUCCEEDED(device->SetVertexShaderConstantF(patch.constant,jitter,1))&&draw(comparison));
    unsigned differences=0;REQUIRE(shifted==comparison);for(size_t i=0;i<shifted.size();++i)if(shifted[i]!=baseline[i])++differences;REQUIRE(differences>0);
    REQUIRE(SUCCEEDED(device->SetVertexShader(ui.Get()))&&draw(ui_off));
    REQUIRE(SUCCEEDED(ordered.Begin(3,64,64,true))&&SUCCEEDED(ordered.BeforeDraw(0,64,64))&&draw(ui_on)&&ui_on==ui_off);
    REQUIRE(SUCCEEDED(device->SetVertexShader(original.Get())));
    D3DVIEWPORT9 shadow_viewport{0,0,32,32,0,1};REQUIRE(SUCCEEDED(device->SetViewport(&shadow_viewport))&&draw(shadow_off));
    REQUIRE(SUCCEEDED(ordered.BeforeDraw(12,32,32))&&draw(shadow_on)&&shadow_on==shadow_off);
    D3DVIEWPORT9 full{0,0,64,64,0,1};REQUIRE(SUCCEEDED(device->SetViewport(&full))&&SUCCEEDED(ordered.BeforeDraw(12,64,64)));
    REQUIRE(SUCCEEDED(ordered.Suspend()));ComPtr<IDirect3DStateBlock9> block;REQUIRE(SUCCEEDED(device->CreateStateBlock(D3DSBT_ALL,&block)));
    float changed[16];std::memcpy(changed,matrix,sizeof(matrix));changed[12]=0.1f;
    REQUIRE(SUCCEEDED(device->SetVertexShaderConstantF(12,changed,4))&&SUCCEEDED(block->Apply())&&SUCCEEDED(ordered.Resume()));
    REQUIRE(SUCCEEDED(ordered.BeforeDraw(12,64,64))&&SUCCEEDED(ordered.Stop()));
    REQUIRE(SUCCEEDED(device->GetVertexShaderConstantF(12,logical,4))&&!std::memcmp(matrix,logical,sizeof(matrix)));
    const auto stats=ordered.Stats();REQUIRE(stats.applied==stats.restored&&stats.applied==3);
    std::printf("{\"passed\":true,\"checks\":%u,\"synthetic_only\":true,\"ordered_projection\":true,\"reference_matched\":true,\"ui_unchanged\":true,\"shadow_unchanged\":true,\"logical_constants_unchanged\":true,\"stateblock_restored\":true,\"shifted_pixels\":%u}\n",checks,differences);
    device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);block.Reset();original.Reset();reference.Reset();ui.Reset();pixel.Reset();declaration.Reset();device.Reset();bridge.Reset();wrapped.Reset();d12.Reset();api.Reset();DestroyWindow(window);return 0;
#else
    auto scope_for=[&](IDirect3DVertexShader9* shader,DWORD sequence){
        On12NativeTemporal::Control value;value.device12=reinterpret_cast<std::uintptr_t>(d12.Get());value.sequence=sequence;
        UINT bytes=0;std::vector<unsigned char> code;
        if(SUCCEEDED(shader->GetFunction(nullptr,&bytes))&&bytes&&bytes<=65536){code.resize(bytes);
            if(SUCCEEDED(shader->GetFunction(code.data(),&bytes))&&On12Graphics::SceneProjectionShader(code.data(),bytes)){
                value.flags=1;value.width=value.height=64;value.jitter_x=0.25f;value.jitter_y=-0.375f;
            }}return value;
    };
    auto c=scope_for(original.Get(),1);REQUIRE(c.flags==1);
    REQUIRE(control(&c)==1&&draw(shifted)); // No application shader/viewport rebind after control.
    float after[16]{};ComPtr<IDirect3DVertexShader9> bound;REQUIRE(SUCCEEDED(device->GetVertexShaderConstantF(12,after,4))&&!std::memcmp(matrix,after,sizeof(matrix)));
    REQUIRE(SUCCEEDED(device->GetVertexShader(&bound))&&bound.Get()==original.Get());bound.Reset();
    On12NativeTemporal::Control off;off.device12=c.device12;off.sequence=2;REQUIRE(control(&off)==1);
    const float jitter[]{2*c.jitter_x/64,-2*c.jitter_y/64,0,0};REQUIRE(SUCCEEDED(device->SetVertexShader(reference.Get()))&&SUCCEEDED(device->SetVertexShaderConstantF(patch.constant,jitter,1))&&draw(comparison));
    unsigned differences=0,bad=0,maximum_error=0;for(size_t i=0;i<shifted.size();++i){if(shifted[i]!=baseline[i])++differences;for(unsigned s:{0u,8u,16u}){
        const auto error=static_cast<unsigned>(std::abs(int((shifted[i]>>s)&255)-int((comparison[i]>>s)&255)));if(error>1)++bad;if(error>maximum_error)maximum_error=error;}}
    On12NativeTemporal::Query diagnostic;diagnostic.device12=c.device12;const auto queried=query(&diagnostic);
    std::fprintf(stderr,"native jitter diagnostic: query=%lu eligible=%llu shifted=%llu changed_pixels=%u bad_channels=%u max_error=%u resolves=%llu resolver=%lu owner=%lu metadata_scene=%lu\n",queried,
        static_cast<unsigned long long>(diagnostic.eligible_resolves),static_cast<unsigned long long>(diagnostic.shifted_resolves),differences,bad,maximum_error,
        static_cast<unsigned long long>(diagnostic.resolves),diagnostic.resolver_thread,diagnostic.control_owner,diagnostic.metadata_scene);
    REQUIRE(!bad);REQUIRE(differences>0);
    REQUIRE(SUCCEEDED(device->SetVertexShader(ui.Get()))&&draw(ui_off));auto ui_scope=scope_for(ui.Get(),3);REQUIRE(ui_scope.flags==0&&control(&ui_scope)==1&&draw(ui_on)&&ui_on==ui_off);
    off.sequence=4;REQUIRE(control(&off)==1&&SUCCEEDED(device->SetVertexShader(original.Get())));
    D3DVIEWPORT9 shadow_viewport{0,0,32,32,0,1};REQUIRE(SUCCEEDED(device->SetViewport(&shadow_viewport))&&draw(shadow_off));c.sequence=5;REQUIRE(control(&c)==1&&draw(shadow_on)&&shadow_on==shadow_off);
    off.sequence=6;REQUIRE(control(&off)==1);On12NativeTemporal::Query q;q.device12=c.device12;REQUIRE(query(&q)==1&&q.enabled==0&&q.shifted_resolves>0&&q.configurations==6);
    REQUIRE(control(&c)==On12NativeTemporal::StaleSequence); // Replaying an older frame cannot reactivate jitter.
    std::printf("{\"passed\":true,\"checks\":%u,\"synthetic_only\":true,\"native_jitter\":true,\"reference_matched\":true,\"ui_unchanged\":true,\"shadow_unchanged\":true,\"app_constants_unchanged\":true,\"shifted_pixels\":%u,\"shifted_resolves\":%llu}\n",checks,differences,static_cast<unsigned long long>(q.shifted_resolves));
    device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);original.Reset();reference.Reset();ui.Reset();pixel.Reset();declaration.Reset();device.Reset();bridge.Reset();wrapped.Reset();d12.Reset();api.Reset();DestroyWindow(window);return 0;
#endif
}
