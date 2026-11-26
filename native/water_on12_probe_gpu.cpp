// Own tiny GPU scene only. Confirms non-target draw and state transparency.
#include "water_on12_probe.cpp"

int wmain(int argc,wchar_t** argv) {
    if(argc!=2)return 1;
    Config c{};c.size=sizeof(c);c.version=1;
    const std::wstring report=std::wstring(argv[1])+L"\\water-probe.private.json";
    if(wcscpy_s(c.report,report.c_str())||!Shape(c)||!Parents(c.report))return 2;
    config=c;directory=argv[1];
    WNDCLASSW wc{};wc.lpfnWndProc=DefWindowProcW;wc.hInstance=GetModuleHandleW(nullptr);wc.lpszClassName=L"WePlayWaterObserverGPU";
    if(!RegisterClassW(&wc))return 3;
    HWND window=CreateWindowW(wc.lpszClassName,L"Water observer GPU fixture",WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,wc.hInstance,nullptr);
    if(!window)return 4;
    HMODULE renderer=LoadLibraryW(L"d3d9.dll");if(!renderer)return 5;
    using Create=IDirect3D9* (WINAPI*)(UINT);const auto create=reinterpret_cast<Create>(GetProcAddress(renderer,"Direct3DCreate9"));
    if(!create)return 6;IDirect3D9* api=create(D3D_SDK_VERSION);if(!api)return 7;
    D3DPRESENT_PARAMETERS pp{};pp.Windowed=TRUE;pp.BackBufferWidth=320;pp.BackBufferHeight=240;
    pp.BackBufferFormat=D3DFMT_X8R8G8B8;pp.SwapEffect=D3DSWAPEFFECT_DISCARD;pp.hDeviceWindow=window;pp.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* d=nullptr;
    if(FAILED(api->CreateDevice(0,D3DDEVTYPE_HAL,window,D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_FPU_PRESERVE|D3DCREATE_MULTITHREADED,&pp,&d)))return 8;
    wrapper=GetModuleHandleW(L"dxwrapper.dll");if(!wrapper||!Module(wrapper,Wrapper))return 9;
    apphelp=GetModuleHandleW(L"apphelp.dll");
    if(apphelp&&!Module(apphelp,"34655b97cbcaa5b93733da1d9118e0663735c442eb0016671a98389f3cde863e"))return 11;
    auto** observed_table=*reinterpret_cast<void***>(d);
    for(unsigned index: {0u,17u,82u}) {
        MEMORY_BASIC_INFORMATION memory{};VirtualQuery(observed_table[index],&memory,sizeof(memory));
        wchar_t module[512]{};GetModuleFileNameW(static_cast<HMODULE>(memory.AllocationBase),module,512);
        std::fwprintf(stderr,L"slot %u owner %ls\n",index,module);
    }
    MEMORY_BASIC_INFORMATION table_memory{};VirtualQuery(observed_table,&table_memory,sizeof(table_memory));
    std::fprintf(stderr,"vtable memory type %lu; wrapper ownership %d\n",table_memory.Type,Belongs(observed_table,wrapper)?1:0);
    IDirect3DSurface9* target=nullptr;IDirect3DSurface9* staging=nullptr;
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;IDirect3DPixelShader9* ps=nullptr;
    const DWORD program[]={0xffff0300,0x05000051,0xa00f00df,0x3f800000,0,0x3f800000,0x3f800000,
                           0x02000001,0x800f0800,0xa0e400df,0x0000ffff};
    unsigned stage=10;DWORD observed_pixel=0;HRESULT sampled_hr=S_OK;
    bool okay=SUCCEEDED(d->CreateRenderTarget(8,8,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr))
        &&SUCCEEDED(d->CreateOffscreenPlainSurface(8,8,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr))
        &&SUCCEEDED(d->CreateVertexBuffer(3*4*sizeof(float),D3DUSAGE_WRITEONLY,D3DFVF_XYZRHW,D3DPOOL_MANAGED,&vb,nullptr))
        &&SUCCEEDED(d->CreateIndexBuffer(3*sizeof(WORD),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr))
        &&SUCCEEDED(d->CreatePixelShader(program,&ps));
    void* data=nullptr;const float vertices[]={-.5f,-.5f,.5f,1,15.5f,-.5f,.5f,1,-.5f,15.5f,.5f,1};const WORD indices[]={0,1,2};
    if(okay)stage=20;
    if(okay&&SUCCEEDED(vb->Lock(0,0,&data,0))){std::memcpy(data,vertices,sizeof(vertices));okay=SUCCEEDED(vb->Unlock());}else okay=false;
    if(okay&&SUCCEEDED(ib->Lock(0,0,&data,0))){std::memcpy(data,indices,sizeof(indices));okay=SUCCEEDED(ib->Unlock());}else okay=false;
    D3DVIEWPORT9 viewport{0,0,8,8,0,1};
    if(okay)stage=30;
    if(okay)okay=SUCCEEDED(d->SetRenderTarget(0,target))&&SUCCEEDED(d->SetDepthStencilSurface(nullptr))
        &&SUCCEEDED(d->SetViewport(&viewport))&&SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE,FALSE))
        &&SUCCEEDED(d->SetRenderState(D3DRS_CULLMODE,D3DCULL_NONE))&&SUCCEEDED(d->SetRenderState(D3DRS_ALPHABLENDENABLE,FALSE))
        &&SUCCEEDED(d->SetFVF(D3DFVF_XYZRHW))&&SUCCEEDED(d->SetStreamSource(0,vb,0,4*sizeof(float)))
        &&SUCCEEDED(d->SetIndices(ib))&&SUCCEEDED(d->SetPixelShader(ps));
    auto sample=[&](){
        sampled_hr=d->GetRenderTargetData(target,staging);if(FAILED(sampled_hr))return false;
        D3DLOCKED_RECT locked{};sampled_hr=staging->LockRect(&locked,nullptr,D3DLOCK_READONLY);if(FAILED(sampled_hr))return false;
        const auto* row=static_cast<const unsigned char*>(locked.pBits)+2*locked.Pitch;
        observed_pixel=*reinterpret_cast<const DWORD*>(row+2*4);
        const bool matched=(observed_pixel&0xffffffu)==0xff00ffu;
        const HRESULT unlock=staging->UnlockRect();return matched&&SUCCEEDED(unlock);
    };
    for(unsigned phase=0;okay&&phase<2;++phase) {
        stage=100+phase*100;
        if(phase==1){Attach(d);okay=admitted==d&&!failure;}
        if(okay){stage=110+phase*100;okay=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0));}
        if(okay)okay=SUCCEEDED(d->BeginScene());
        if(okay){stage=120+phase*100;SetLastError(0x1357);const HRESULT drawn=d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1);
            const DWORD error=GetLastError();okay=SUCCEEDED(drawn)&&(phase==0||error==0x1357);}
        const HRESULT ended=d->EndScene();if(okay)stage=130+phase*100;okay=okay&&SUCCEEDED(ended)&&sample();
        D3DVIEWPORT9 after{};IDirect3DPixelShader9* bound=nullptr;DWORD z=1,cull=0,blend=1;
        if(okay)stage=140+phase*100;
        if(okay)okay=SUCCEEDED(d->GetViewport(&after))&&!std::memcmp(&after,&viewport,sizeof(after))
            &&SUCCEEDED(d->GetPixelShader(&bound))&&bound==ps&&SUCCEEDED(d->GetRenderState(D3DRS_ZENABLE,&z))&&!z
            &&SUCCEEDED(d->GetRenderState(D3DRS_CULLMODE,&cull))&&cull==D3DCULL_NONE
            &&SUCCEEDED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))&&!blend;
        if(bound)bound->Release();
        if(okay)okay=SUCCEEDED(d->Present(nullptr,nullptr,nullptr,nullptr));
    }
    okay=okay&&draws==1&&!matches&&!snapshot&&!failure&&!failed_draws&&presents==1;
    std::printf("{\"non_target_pixels_and_state_passed\":%s,\"hooked_draws\":%llu,\"snapshot_done\":false,\"on12_query_hr\":%ld,\"stage\":%u,\"pixel\":%lu,\"sample_hr\":%ld,\"failure\":%lu,\"synthetic_only\":true}\n",
        okay?"true":"false",static_cast<unsigned long long>(draws),static_cast<long>(on12_hr),stage,observed_pixel,static_cast<long>(sampled_hr),failure);
    if(ps)ps->Release();if(ib)ib->Release();if(vb)vb->Release();if(staging)staging->Release();if(target)target->Release();
    d->Release();api->Release();DestroyWindow(window);return okay?0:10;
}
