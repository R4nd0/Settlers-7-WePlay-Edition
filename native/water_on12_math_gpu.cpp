// Authored scalar reproduction only. No original shaders, game code or textures.
#include <windows.h>
#include <d3d9.h>
#include <d3d12.h>
#include <d3d9on12.h>
#include <cfloat>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <vector>

int main() {
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"WaterOn12MathFixture";
    if(!RegisterClassW(&wc))return 1;
    HWND window=CreateWindowW(wc.lpszClassName,L"Water math GPU fixture",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,wc.hInstance,nullptr);
    HMODULE renderer=LoadLibraryW(L"d3d9.dll");if(!window||!renderer)return 2;
    using Create=IDirect3D9* (WINAPI*)(UINT);auto create=reinterpret_cast<Create>(GetProcAddress(renderer,"Direct3DCreate9"));
    if(!create)return 3;IDirect3D9* api=create(D3D_SDK_VERSION);if(!api)return 4;
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.BackBufferWidth=320;pp.BackBufferHeight=240;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;
    HRESULT hr=api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE|D3DCREATE_MULTITHREADED,&pp,&d);
    if(FAILED(hr)){std::printf("{\"create_hr\":%ld}\n",static_cast<long>(hr));return 5;}
    IDirect3DDevice9On12* bridge=nullptr;const HRESULT query=d->QueryInterface(__uuidof(IDirect3DDevice9On12),reinterpret_cast<void**>(&bridge));if(bridge)bridge->Release();
    const DWORD vertex[]={0xfffe0300,
        0x0200001f,0x80000000,0x900f0000, // input position
        0x0200001f,0x80000000,0xe00f0000, // clip position
        0x0200001f,0x80000005,0xe00f0001, // float TEXCOORD0 result
        0x02000001,0xe00f0000,0x90e40000,
        0x03000020,0x80010000,0xa0000000,0xa0550000, // pow r0.x, c0.x, c0.y
        0x03000005,0x80010000,0x80000000,0xa0550001, // factor 2 before zero multiplier
        0x03000005,0x80010000,0x80000000,0xa0000000, // mul by runtime zero
        0x03000002,0xe0070001,0x80000000,0xa0aa0000, // add finite baseline
        0x02000001,0xe0080001,0xa0ff0000,0x0000ffff};
    std::vector<DWORD> guarded(std::begin(vertex),std::end(vertex));
    guarded.insert(guarded.begin()+21,{0x0300000a,0x80010000,0x80000000,0xa0000001}); // finite cap after POW product
    const DWORD pixel[]={0xffff0300,0x0200001f,0x80000005,0x900f0000,0x02000001,0x800f0800,0x90e40000,0x0000ffff};
    IDirect3DVertexShader9* vs[2]{};IDirect3DPixelShader9* ps=nullptr;IDirect3DVertexDeclaration9* declaration=nullptr;
    IDirect3DSurface9* target=nullptr;IDirect3DSurface9* staging=nullptr;
    const D3DVERTEXELEMENT9 elements[]={{0,0,D3DDECLTYPE_FLOAT4,D3DDECLMETHOD_DEFAULT,D3DDECLUSAGE_POSITION,0},D3DDECL_END()};
    const HRESULT created[]={d->CreateVertexShader(vertex,&vs[0]),d->CreateVertexShader(guarded.data(),&vs[1]),
        d->CreatePixelShader(pixel,&ps),d->CreateVertexDeclaration(elements,&declaration),
        d->CreateRenderTarget(4,4,D3DFMT_A32B32G32R32F,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr),
        d->CreateOffscreenPlainSurface(4,4,D3DFMT_A32B32G32R32F,D3DPOOL_SYSTEMMEM,&staging,nullptr)};
    bool okay=true;for(HRESULT value:created)okay=okay&&SUCCEEDED(value);
    if(!okay){std::printf("{\"creation_hrs\":[%ld,%ld,%ld,%ld,%ld,%ld]}\n",created[0],created[1],created[2],created[3],created[4],created[5]);return 6;}
    D3DVIEWPORT9 viewport{0,0,4,4,0,1};const float positions[]={-1,-1,.5f,1,3,-1,.5f,1,-1,3,.5f,1};
    const float maximum[]={FLT_MAX,2,0,0};
    okay=SUCCEEDED(d->SetRenderTarget(0,target))&&SUCCEEDED(d->SetDepthStencilSurface(nullptr))&&SUCCEEDED(d->SetViewport(&viewport))
        &&SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE,FALSE))&&SUCCEEDED(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))
        &&SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))&&SUCCEEDED(d->SetVertexDeclaration(declaration))
        &&SUCCEEDED(d->SetPixelShader(ps))&&SUCCEEDED(d->SetVertexShaderConstantF(1,maximum,1));
    std::printf("{\"synthetic_only\":true,\"on12_query_hr\":%ld,\"cases\":[",static_cast<long>(query));
    for(unsigned index=0;okay&&index<4;++index) {
        const float input[]={index%2?1.f:0.f,-1.5f,.25f,1.f};
        okay=SUCCEEDED(d->SetVertexShader(vs[index/2]))&&SUCCEEDED(d->SetVertexShaderConstantF(0,input,1))
            &&SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0,1,0))&&SUCCEEDED(d->BeginScene());
        if(okay)hr=d->DrawPrimitiveUP(D3DPT_TRIANGLELIST,1,positions,4*sizeof(float));else hr=E_FAIL;
        const HRESULT ended=d->EndScene();okay=okay&&SUCCEEDED(hr)&&SUCCEEDED(ended)&&SUCCEEDED(d->GetRenderTargetData(target,staging));
        D3DLOCKED_RECT locked{};if(okay)okay=SUCCEEDED(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY));
        if(!okay)break;
        float values[4]{};std::memcpy(values,static_cast<const unsigned char*>(locked.pBits)+locked.Pitch+16,sizeof(values));
        const HRESULT unlocked=staging->UnlockRect();okay=SUCCEEDED(unlocked)&&std::isfinite(values[3])&&std::fabs(values[3]-1.f)<.0001f;
        if(index)std::printf(",");std::printf("{\"guarded\":%s,\"base\":%g,\"rgba\":[",index>=2?"true":"false",input[0]);
        for(unsigned c=0;c<4;++c){if(c)std::printf(",");if(std::isfinite(values[c]))std::printf("%.9g",values[c]);else std::printf("null");}
        std::uint32_t bits[4]{};std::memcpy(bits,values,sizeof(bits));
        std::printf("],\"bits\":[%u,%u,%u,%u]}",bits[0],bits[1],bits[2],bits[3]);
    }
    std::printf("],\"draws_and_readbacks_passed\":%s}\n",okay?"true":"false");
    staging->Release();target->Release();declaration->Release();ps->Release();vs[0]->Release();vs[1]->Release();d->Release();api->Release();DestroyWindow(window);
    return okay?0:7;
}
