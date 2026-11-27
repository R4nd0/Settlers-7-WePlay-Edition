// Authored GPU scene for the optional locally built On12 constant-buffer trial.
#include "water_on12_identity.h"
#include <d3d9.h>
#include <d3d9on12.h>
#include <cstdio>

template<class T> struct Owned {
    T* p=nullptr;
    ~Owned(){Reset();}
    void Reset(){if(p){p->Release();p=nullptr;}}
};
struct Scene {
    Owned<IDirect3DSurface9> target,staging;
    Owned<IDirect3DVertexBuffer9> vertices;
    Owned<IDirect3DIndexBuffer9> indices;
    Owned<IDirect3DPixelShader9> low,high;
    bool Init(IDirect3DDevice9* d){
        const DWORD lowProgram[]={0xffff0300,0x02000001,0x800f0800,0xa0e40000,0x0000ffff};
        const DWORD highProgram[]={0xffff0300,0x02000001,0x800f0800,0xa0e40064,0x0000ffff};
        const float points[]={-.5f,-.5f,.5f,1,15.5f,-.5f,.5f,1,-.5f,15.5f,.5f,1};
        const WORD order[]={0,1,2};void* data=nullptr;
        if(FAILED(d->CreateRenderTarget(8,8,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target.p,nullptr))
            ||FAILED(d->CreateOffscreenPlainSurface(8,8,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&staging.p,nullptr))
            ||FAILED(d->CreateVertexBuffer(sizeof(points),D3DUSAGE_WRITEONLY,D3DFVF_XYZRHW,D3DPOOL_MANAGED,&vertices.p,nullptr))
            ||FAILED(d->CreateIndexBuffer(sizeof(order),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&indices.p,nullptr))
            ||FAILED(d->CreatePixelShader(lowProgram,&low.p))||FAILED(d->CreatePixelShader(highProgram,&high.p)))return false;
        if(FAILED(vertices.p->Lock(0,0,&data,0)))return false;
        std::memcpy(data,points,sizeof(points));if(FAILED(vertices.p->Unlock()))return false;
        if(FAILED(indices.p->Lock(0,0,&data,0)))return false;
        std::memcpy(data,order,sizeof(order));if(FAILED(indices.p->Unlock()))return false;
        D3DVIEWPORT9 viewport{0,0,8,8,0,1};
        return SUCCEEDED(d->SetRenderTarget(0,target.p))&&SUCCEEDED(d->SetDepthStencilSurface(nullptr))
            &&SUCCEEDED(d->SetViewport(&viewport))&&SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE,FALSE))
            &&SUCCEEDED(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))
            &&SUCCEEDED(d->SetFVF(D3DFVF_XYZRHW))&&SUCCEEDED(d->SetStreamSource(0,vertices.p,0,4*sizeof(float)))
            &&SUCCEEDED(d->SetIndices(indices.p));
    }
    bool Draw(IDirect3DDevice9* d,IDirect3DPixelShader9* shader,DWORD expected){
        if(FAILED(d->SetPixelShader(shader))||FAILED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0))||FAILED(d->BeginScene()))return false;
        const HRESULT drawn=d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1),ended=d->EndScene();
        if(FAILED(drawn)||FAILED(ended)||FAILED(d->GetRenderTargetData(target.p,staging.p)))return false;
        D3DLOCKED_RECT locked{};if(FAILED(staging.p->LockRect(&locked,nullptr,D3DLOCK_READONLY)))return false;
        const auto* row=static_cast<const unsigned char*>(locked.pBits)+2*locked.Pitch;
        DWORD pixel=0;std::memcpy(&pixel,row+8,4);
        const bool same=(pixel&0xffffffu)==(expected&0xffffffu);const HRESULT unlocked=staging.p->UnlockRect();
        if(!same)std::fprintf(stderr,"pixel %08lx expected %08lx\n",pixel,expected);
        return same&&SUCCEEDED(unlocked);
    }
    bool Release(IDirect3DDevice9* d){
        Owned<IDirect3DSurface9> back;
        if(FAILED(d->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&back.p))||FAILED(d->SetRenderTarget(0,back.p)))return false;
        if(FAILED(d->SetPixelShader(nullptr))||FAILED(d->SetStreamSource(0,nullptr,0,0))||FAILED(d->SetIndices(nullptr)))return false;
        high.Reset();low.Reset();indices.Reset();vertices.Reset();staging.Reset();target.Reset();return true;
    }
};
int wmain(int argc,wchar_t** argv){
    const bool stock=argc==3&&!wcscmp(argv[2],L"--stock");
    if((argc!=2&&!stock)||wcslen(argv[1])!=64)return 1;std::string pin;
    for(unsigned i=0;i<64;++i){const auto c=argv[1][i];if(!((c>='0'&&c<='9')||(c>='a'&&c<='f')))return 1;pin+=static_cast<char>(c);}
    wchar_t system[512]{};const auto length=GetSystemDirectoryW(system,512);if(!length||length>=480)return 2;
    wcscat_s(system,L"\\d3d9.dll");HMODULE runtime=LoadLibraryExW(system,nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);if(!runtime)return 3;
    using Create=IDirect3D9* (WINAPI*)(UINT,D3D9ON12_ARGS*,UINT);
    const auto create=reinterpret_cast<Create>(GetProcAddress(runtime,"Direct3DCreate9On12"));if(!create)return 4;
    D3D9ON12_ARGS args{};args.Enable9On12=TRUE;Owned<IDirect3D9> api;api.p=create(D3D_SDK_VERSION,&args,1);if(!api.p)return 5;
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"On12ConstantReuseFixture";
    if(!RegisterClassW(&wc))return 6;
    HWND window=CreateWindowW(wc.lpszClassName,L"On12 constants GPU fixture",WS_OVERLAPPEDWINDOW,0,0,32,32,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window)return 7;
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.BackBufferWidth=32;pp.BackBufferHeight=32;pp.BackBufferFormat=D3DFMT_X8R8G8B8;
    pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    Owned<IDirect3DDevice9> d;const HRESULT creation=api.p->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE,&pp,&d.p);
    if(FAILED(creation)){std::fprintf(stderr,"CreateDevice %08lx\n",static_cast<unsigned long>(creation));return 8;}
    HMODULE driver=GetModuleHandleW(L"d3d9on12.dll");if(!WaterOn12Identity::Module(driver,pin.c_str()))return 9;
    using Control=DWORD (WINAPI*)(void*);
    const auto set=reinterpret_cast<Control>(GetProcAddress(driver,"WePlayOn12SetConstantReuse"));
    const auto count=reinterpret_cast<Control>(GetProcAddress(driver,"WePlayOn12GetConstantReuses"));
    if(!stock&&(!set||!count))return 10;
    Owned<IDirect3DDevice9On12> bridge;
    if(FAILED(d.p->QueryInterface(__uuidof(IDirect3DDevice9On12),reinterpret_cast<void**>(&bridge.p))))return 11;
    Scene scene;unsigned stage=12,draws=0,resets=0;bool okay=scene.Init(d.p);
    const float a[]={1,0,1,1},b[]={0,1,0,1};constexpr DWORD magenta=0xffff00ff,green=0xff00ff00;
    const DWORD before=stock?0:count(nullptr);
    auto draw=[&](IDirect3DPixelShader9* shader,DWORD color){++stage;if(!okay)return;okay=scene.Draw(d.p,shader,color);if(okay)++draws;};
    auto put=[&](UINT at,const float* value){if(okay)okay=SUCCEEDED(d.p->SetPixelShaderConstantF(at,value,1));};
    unsigned phase=0;
    for(const DWORD mode:{0u,3u,0u,1u,3u}){
        if(!okay)break;okay=stock||(set(reinterpret_cast<void*>(static_cast<std::uintptr_t>(mode)))&3u)==mode;
        put(0,a);draw(scene.low.p,magenta);put(100,b);draw(scene.low.p,magenta);
        draw(scene.high.p,green);put(0,b);draw(scene.low.p,green);
        // Pending high-register changes must survive reuse of the short shader.
        put(100,a);draw(scene.low.p,green);draw(scene.high.p,magenta);
        Owned<IDirect3DStateBlock9> block;
        put(0,a);put(100,b);
        if(okay)okay=SUCCEEDED(d.p->CreateStateBlock(D3DSBT_PIXELSTATE,&block.p));
        put(0,b);put(100,a);draw(scene.low.p,green);
        if(okay)okay=SUCCEEDED(block.p->Apply());draw(scene.low.p,magenta);draw(scene.high.p,green);
        block.Reset();
        if(phase++==3&&okay){okay=scene.Release(d.p)&&SUCCEEDED(d.p->Reset(&pp))&&scene.Init(d.p);if(okay)++resets;
            // Keep the same mode epoch and the same values as before Reset.
            put(0,a);put(100,b);draw(scene.low.p,magenta);draw(scene.high.p,green);}
    }
    const DWORD reuses=stock?0:count(nullptr)-before;
    okay=okay&&(stock||reuses>0)&&resets==1&&draws==47&&(stock||set(reinterpret_cast<void*>(4))==0xffffffffu);
    const DWORD disabled=stock?0:set(nullptr);okay=okay&&!(disabled&3u)&&scene.Release(d.p);
    std::printf("{\"passed\":%s,\"stage\":%u,\"draws\":%u,\"resets\":%u,\"profiled_reuses\":%lu,\"synthetic_only\":true}\n",okay?"true":"false",stage,draws,resets,reuses);
    bridge.Reset();d.Reset();api.Reset();DestroyWindow(window);return okay?0:12;
}
