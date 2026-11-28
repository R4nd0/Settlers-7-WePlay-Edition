// Authored geometry only: current bytecode plus previous bones/wind/world data.
#include "water_on12_identity.h"
#include <d3d9.h>
#include <d3d9on12.h>
#include <wrl/client.h>
#include <array>
#include <vector>
#include <cstdio>
#include <cmath>
#include <algorithm>

using Microsoft::WRL::ComPtr;
namespace {
unsigned checks=0;
bool Check(bool value,unsigned line){++checks;if(!value)std::fprintf(stderr,"motion GPU check failed at %u\n",line);return value;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
LRESULT CALLBACK Proc(HWND window,UINT message,WPARAM w,LPARAM l){return DefWindowProcW(window,message,w,l);}
std::vector<DWORD> Program(const wchar_t* name){FILE* file=nullptr;if(_wfopen_s(&file,name,L"rb")||!file)return{};
    std::fseek(file,0,SEEK_END);const long size=std::ftell(file);std::rewind(file);std::vector<DWORD> code;
    if(size>=16&&size<=65536&&size%4==0){code.resize(static_cast<size_t>(size)/4);if(std::fread(code.data(),1,size,file)!=static_cast<size_t>(size))code.clear();}
    std::fclose(file);return code;
}
template<class T> bool Read(IDirect3DDevice9* device,IDirect3DSurface9* surface,D3DFORMAT format,std::vector<T>& output){
    ComPtr<IDirect3DSurface9> copy;if(FAILED(device->CreateOffscreenPlainSurface(64,64,format,D3DPOOL_SYSTEMMEM,&copy,nullptr))||FAILED(device->GetRenderTargetData(surface,copy.Get())))return false;
    D3DLOCKED_RECT lock{};if(FAILED(copy->LockRect(&lock,nullptr,D3DLOCK_READONLY)))return false;output.resize(64*64);
    for(UINT y=0;y<64;++y)std::memcpy(output.data()+y*64,static_cast<const unsigned char*>(lock.pBits)+y*lock.Pitch,64*sizeof(T));return SUCCEEDED(copy->UnlockRect());
}
using V4=std::array<float,4>;using Bank=std::array<V4,256>;
void Identity(Bank& bank,UINT start){for(UINT col=0;col<4;++col){bank[start+col]={};bank[start+col][col]=1;}}
void Bones(Bank& bank,float shift,float angle){
    for(UINT bone=0;bone<2;++bone){const float a=bone?angle:0;const float c=std::cos(a),s=std::sin(a);
        bank[80+bone*3]={c,-s,0,shift*(bone?1:-1)};bank[81+bone*3]={s,c,0,shift*0.3f};bank[82+bone*3]={0,0,1,0};}
}
struct Vertex{float x,y,z,w,bone;};
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2||wcslen(argv[1])!=64)return 1;std::string expected;
    for(UINT i=0;i<64;++i){const auto c=argv[1][i];if(!((c>=L'0'&&c<=L'9')||(c>=L'a'&&c<=L'f')))return 1;expected+=static_cast<char>(c);}
    WNDCLASSW wc{};wc.hInstance=GetModuleHandleW(nullptr);wc.lpfnWndProc=Proc;wc.lpszClassName=L"WePlayMotionFixture";REQUIRE(RegisterClassW(&wc));
    HWND window=CreateWindowW(wc.lpszClassName,L"On12 motion fixture",WS_OVERLAPPEDWINDOW,0,0,96,96,nullptr,nullptr,wc.hInstance,nullptr);REQUIRE(window);
    HMODULE module=LoadLibraryW(L"d3d9.dll");REQUIRE(module);using Create=IDirect3D9*(WINAPI*)(UINT);const auto create=reinterpret_cast<Create>(GetProcAddress(module,"Direct3DCreate9"));REQUIRE(create);
    ComPtr<IDirect3D9> api;api.Attach(create(D3D_SDK_VERSION));REQUIRE(api);
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.hDeviceWindow=window;pp.BackBufferWidth=pp.BackBufferHeight=64;pp.BackBufferFormat=D3DFMT_A8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9> wrapped,device;REQUIRE(SUCCEEDED(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&wrapped)));
    REQUIRE(WaterOn12Identity::Module(GetModuleHandleW(L"dxwrapper.dll"),WaterOn12Identity::Wrapper)&&WaterOn12Identity::Module(GetModuleHandleW(L"d3d9on12.dll"),expected.c_str()));
    ComPtr<IDirect3DDevice9On12> bridge;REQUIRE(SUCCEEDED(wrapped.As(&bridge))&&SUCCEEDED(bridge.As(&device)));
    std::array<ComPtr<IDirect3DVertexShader9>,3> vertex;
    const wchar_t* files[]{L"current.vs.bin",L"combined.vs.bin",L"reference.vs.bin"};
    for(UINT i=0;i<3;++i){const auto code=Program(files[i]);REQUIRE(!code.empty()&&SUCCEEDED(device->CreateVertexShader(code.data(),&vertex[i])));}
    std::array<ComPtr<IDirect3DPixelShader9>,2> pixel;
    for(UINT i=0;i<2;++i){const auto code=Program(i?L"previous.ps.bin":L"color.ps.bin");REQUIRE(!code.empty()&&SUCCEEDED(device->CreatePixelShader(code.data(),&pixel[i])));}
    ComPtr<IDirect3DSurface9> color,positions;REQUIRE(SUCCEEDED(device->CreateRenderTarget(64,64,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&color,nullptr))
        &&SUCCEEDED(device->CreateRenderTarget(64,64,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&positions,nullptr)));
    ComPtr<IDirect3DTexture9> previous;
    REQUIRE(SUCCEEDED(device->CreateTexture(288,1,1,D3DUSAGE_DYNAMIC,D3DFMT_A32B32G32R32F,D3DPOOL_DEFAULT,&previous,nullptr)));
    REQUIRE(SUCCEEDED(device->SetTexture(D3DVERTEXTEXTURESAMPLER3,previous.Get())));
    for(auto state:{D3DSAMP_MINFILTER,D3DSAMP_MAGFILTER})REQUIRE(SUCCEEDED(device->SetSamplerState(D3DVERTEXTEXTURESAMPLER3,state,D3DTEXF_POINT)));
    REQUIRE(SUCCEEDED(device->SetSamplerState(D3DVERTEXTEXTURESAMPLER3,D3DSAMP_MIPFILTER,D3DTEXF_NONE)));
    for(auto state:{D3DSAMP_ADDRESSU,D3DSAMP_ADDRESSV})REQUIRE(SUCCEEDED(device->SetSamplerState(D3DVERTEXTEXTURESAMPLER3,state,D3DTADDRESS_CLAMP)));
    const D3DVERTEXELEMENT9 declaration[]{{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},{0,16,D3DDECLTYPE_FLOAT1,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_TEXCOORD,0},D3DDECL_END()};
    ComPtr<IDirect3DVertexDeclaration9> layout;REQUIRE(SUCCEEDED(device->CreateVertexDeclaration(declaration,&layout))&&SUCCEEDED(device->SetVertexDeclaration(layout.Get())));
    REQUIRE(SUCCEEDED(device->SetRenderState(D3DRS_ZENABLE,FALSE))&&SUCCEEDED(device->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))&&SUCCEEDED(device->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE)));
    const Vertex triangles[]{{-.75f,-.65f,1.2f,1,0},{.7f,-.55f,1.4f,1,1},{.05f,.75f,1.1f,1,1}};
    auto draw=[&](UINT vs,UINT ps,IDirect3DSurface9* target){
        if(FAILED(device->SetRenderTarget(0,target))||FAILED(device->SetVertexShader(vertex[vs].Get()))||FAILED(device->SetPixelShader(pixel[ps].Get()))||FAILED(device->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0))||FAILED(device->BeginScene()))return false;
        const auto hr=device->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,triangles,sizeof(Vertex));const auto ended=device->EndScene();return SUCCEEDED(hr)&&SUCCEEDED(ended);
    };
    unsigned total_pixels=0,changed_previous=0;float maximum_error=0;std::vector<V4> baseline;
    for(UINT test=0;test<5;++test){Bank current{};Identity(current,12);Identity(current,68);current[14][2]=.3f;current[71][0]=.05f;
        Bones(current,.02f,.04f);current[43]={.08f,0,0,0};current[63]={.4f,0,0,0};
        if(test==4){current[12][0]=1.1f;current[13][1]=1.2f;current[14]={0,0,.9f,1};current[15]={0,0,-.1f,0};}
        Bank before=current;
        if(test>=1){before[71][0]=-.15f;before[71][1]=.06f;}
        if(test>=2)Bones(before,-.07f,.18f);
        if(test>=3){before[43][0]=.14f;before[63][0]=1.15f;}
        if(test==4){before[12][0]=1.05f;before[15][0]=-.06f;}
        REQUIRE(SUCCEEDED(device->SetVertexShaderConstantF(0,current[0].data(),256)));
        REQUIRE(SUCCEEDED(device->SetVertexShaderConstantF(232,before[12].data(),4))&&SUCCEEDED(device->SetVertexShaderConstantF(236,before[68].data(),4))
            &&SUCCEEDED(device->SetVertexShaderConstantF(240,before[80].data(),6))&&SUCCEEDED(device->SetVertexShaderConstantF(246,before[43].data(),1))&&SUCCEEDED(device->SetVertexShaderConstantF(247,before[63].data(),1)));
        std::array<V4,288> snapshot{};std::copy(before.begin(),before.end(),snapshot.begin());D3DLOCKED_RECT lock{};
        REQUIRE(SUCCEEDED(previous->LockRect(0,&lock,nullptr,D3DLOCK_DISCARD)));std::memcpy(lock.pBits,snapshot.data(),sizeof(snapshot));REQUIRE(SUCCEEDED(previous->UnlockRect(0)));
        std::vector<DWORD> original,combined;REQUIRE(draw(0,0,color.Get())&&Read(device.Get(),color.Get(),D3DFMT_A8R8G8B8,original));
        REQUIRE(draw(1,0,color.Get())&&Read(device.Get(),color.Get(),D3DFMT_A8R8G8B8,combined)&&combined==original);
        std::vector<V4> actual,reference;REQUIRE(draw(1,1,positions.Get())&&Read(device.Get(),positions.Get(),D3DFMT_A32B32G32R32F,actual));
        REQUIRE(draw(2,1,positions.Get())&&Read(device.Get(),positions.Get(),D3DFMT_A32B32G32R32F,reference));
        UINT covered=0;
        for(UINT i=0;i<64*64;++i){if(reference[i][3]>0)++covered;
            for(UINT c=0;c<4;++c){const float error=std::fabs(actual[i][c]-reference[i][c]);maximum_error=(std::max)(maximum_error,error);REQUIRE(std::isfinite(actual[i][c])&&error<.0002f);}
            if(test>0&&test<4&&reference[i][3]>0&&std::fabs(actual[i][0]-baseline[i][0])>.001f)++changed_previous;
        }REQUIRE(covered>200);total_pixels+=covered;if(test==0)baseline=actual;
    }
    REQUIRE(changed_previous>200);
    std::printf("{\"passed\":true,\"checks\":%u,\"synthetic_only\":true,\"current_image_unchanged\":true,\"previous_reference_matched\":true,\"bone_wind_rigid_perspective\":true,\"cases\":5,\"covered_pixels\":%u,\"changed_previous_pixels\":%u,\"maximum_error\":%.9f}\n",checks,total_pixels,changed_previous,maximum_error);
    device->SetTexture(D3DVERTEXTEXTURESAMPLER3,nullptr);device->SetVertexShader(nullptr);device->SetPixelShader(nullptr);previous.Reset();positions.Reset();color.Reset();layout.Reset();
    for(auto& item:vertex)item.Reset();for(auto& item:pixel)item.Reset();device.Reset();bridge.Reset();wrapped.Reset();api.Reset();DestroyWindow(window);return 0;
}
