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
    Ref<IDirect3DSurface9> original_target;
    if(FAILED(d->GetRenderTarget(0,&original_target.value)))return 12;
    IDirect3DVertexBuffer9* vb=nullptr;IDirect3DIndexBuffer9* ib=nullptr;IDirect3DPixelShader9* ps=nullptr;
    const DWORD program[]={0xffff0300,0x05000051,0xa00f00df,0x3f800000,0,0x3f800000,0x3f800000,
                           0x02000001,0x800f0800,0xa0e400df,0x0000ffff};
    unsigned stage=10;DWORD observed_pixel=0;HRESULT sampled_hr=S_OK;
    bool okay=SUCCEEDED(d->CreateRenderTarget(8,8,D3DFMT_X8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr))
        &&SUCCEEDED(d->CreateOffscreenPlainSurface(8,8,D3DFMT_X8R8G8B8,D3DPOOL_SYSTEMMEM,&staging,nullptr))
        &&SUCCEEDED(d->CreateVertexBuffer(3*4*sizeof(float),D3DUSAGE_WRITEONLY,D3DFVF_XYZRHW,D3DPOOL_MANAGED,&vb,nullptr))
        &&SUCCEEDED(d->CreateIndexBuffer(3*sizeof(WORD),D3DUSAGE_WRITEONLY,D3DFMT_INDEX16,D3DPOOL_MANAGED,&ib,nullptr))
        &&SUCCEEDED(d->CreatePixelShader(program,&ps));
    if(okay){const auto before=pixel_reads;okay=!PixelTarget(ps)&&!PixelTarget(ps)&&pixel_reads==before+1&&pixel_cache.count==1;}
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
    // Created before admission: its shared wrapper vtable must also invalidate
    // a later shadow. An interceptor attached only to new blocks is unsafe.
    Ref<IDirect3DStateBlock9> preexisting;
    if(okay)okay=SUCCEEDED(d->CreateStateBlock(D3DSBT_VERTEXSTATE,&preexisting.value));
    for(unsigned phase=0;okay&&phase<4;++phase) {
        stage=100+phase*100;
        if(phase==1){Ref<IDirect3DDevice9On12> bridge;
            if(d->QueryInterface(__uuidof(IDirect3DDevice9On12),reinterpret_cast<void**>(&bridge.value))==S_OK)config.flags=3;
            Attach(d);okay=admitted==d&&!failure;
            const float zeros[4]{};
            if(okay)okay=SUCCEEDED(d->SetRenderState(D3DRS_ZENABLE,FALSE))&&SUCCEEDED(d->SetTexture(0,nullptr))
                &&SUCCEEDED(d->SetSamplerState(0,D3DSAMP_MINFILTER,D3DTEXF_POINT))&&SUCCEEDED(d->SetVertexShaderConstantF(0,zeros,1))
                &&SUCCEEDED(d->SetPixelShaderConstantF(0,zeros,1))&&SUCCEEDED(d->SetVertexShader(nullptr))&&SUCCEEDED(d->SetPixelShader(ps))
                &&SUCCEEDED(d->SetStreamSource(0,vb,0,4*sizeof(float)))&&SUCCEEDED(d->SetIndices(ib))
                &&SUCCEEDED(d->StretchRect(original_target.value,nullptr,target,nullptr,D3DTEXF_NONE));}
        if(phase==2&&on12_hr==S_OK)benchmark_legacy=true;
        if(phase==3&&on12_hr==S_OK){benchmark_legacy=false;benchmark_shader_state=true;
            Ref<IDirect3DStateBlock9> pixel_block;
            okay=SUCCEEDED(d->SetPixelShader(ps))&&pixel_state_known&&!pixel_state_target
                &&SUCCEEDED(preexisting.value->Apply())&&!pixel_state_known
                &&SUCCEEDED(d->BeginStateBlock())&&SUCCEEDED(d->SetPixelShader(nullptr))&&!pixel_state_known
                &&SUCCEEDED(d->EndStateBlock(&pixel_block.value))&&!pixel_state_known
                &&SUCCEEDED(d->SetPixelShader(ps))&&pixel_state_known
                &&SUCCEEDED(pixel_block.value->Apply())&&!pixel_state_known
                &&SUCCEEDED(d->SetPixelShader(ps))&&pixel_state_known&&!pixel_state_target;}
        if(okay){stage=110+phase*100;okay=SUCCEEDED(d->Clear(0,nullptr,D3DCLEAR_TARGET,0xff000000,1,0));}
        if(okay)okay=SUCCEEDED(d->BeginScene());
        if(okay){stage=120+phase*100;alignas(16) unsigned char before[512]{},after[512]{};SetLastError(0x1357);_fxsave(before);
            const HRESULT drawn=d->DrawIndexedPrimitive(D3DPT_TRIANGLELIST,0,0,3,0,1);_fxsave(after);
            const DWORD error=GetLastError();
            // XMM registers are caller-saved in this ABI; the fixture compiler
            // may use them to initialize the capture buffers. Compare the
            // architectural control/status fields rather than caller registers.
            const bool fp_equal=!std::memcmp(before,after,4)&&!std::memcmp(before+24,after+24,4);
            if(phase>0&&!fp_equal){DWORD csr_before=0,csr_after=0;std::memcpy(&csr_before,before+24,4);std::memcpy(&csr_after,after+24,4);
                std::fprintf(stderr,"FP control/status mismatch: x87 %02x%02x/%02x%02x status %02x%02x/%02x%02x MXCSR %08lx/%08lx\n",before[1],before[0],after[1],after[0],before[3],before[2],after[3],after[2],csr_before,csr_after);}
            okay=SUCCEEDED(drawn)&&(phase==0||(error==0x1357&&fp_equal));}
        const HRESULT ended=d->EndScene();if(okay)stage=130+phase*100;okay=okay&&SUCCEEDED(ended)&&sample();
        if(okay&&phase==1&&on12_hr==S_OK) {
            benchmark_constants=true;Ref<IDirect3DStateBlock9> block;
            float saved[8]{},observed[8]{};const float a[]={1,2,3,4,5,6,7,8},b[]={9,10,11,12};
            const auto skipped=constants_skipped;
            okay=constants_armed&&SUCCEEDED(d->GetVertexShaderConstantF(0,saved,2))
                &&SUCCEEDED(d->SetVertexShaderConstantF(0,a,2))&&SUCCEEDED(d->SetVertexShaderConstantF(0,a,2))&&constants_skipped==skipped+1;
            const auto captured=constants_recorded;
            if(okay)okay=SUCCEEDED(d->BeginStateBlock())&&SUCCEEDED(d->SetVertexShaderConstantF(0,a,1))
                &&SUCCEEDED(d->EndStateBlock(&block.value))&&block.value&&constants_recorded==captured+1
                &&SUCCEEDED(d->SetVertexShaderConstantF(0,b,1))&&SUCCEEDED(block.value->Apply())
                &&SUCCEEDED(d->GetVertexShaderConstantF(0,observed,1))&&!std::memcmp(a,observed,16);
            // Hint still says B while Apply restored A. It must not suppress B.
            if(okay)okay=SUCCEEDED(d->SetVertexShaderConstantF(0,b,1))&&SUCCEEDED(d->GetVertexShaderConstantF(0,observed,1))
                &&!std::memcmp(b,observed,16)&&SUCCEEDED(block.value->Apply());
            if(okay)okay=SUCCEEDED(d->SetVertexShaderConstantF(1,b,1))&&SUCCEEDED(d->GetVertexShaderConstantF(0,observed,2))
                &&!std::memcmp(a,observed,16)&&!std::memcmp(b,observed+4,16)
                &&d->SetVertexShaderConstantF(256,a,1)==D3DERR_INVALIDCALL;
            const DWORD raw[]={0x80000000u,0x7fc01234u,0x3f800000u,0};
            if(okay)okay=SUCCEEDED(d->SetVertexShaderConstantF(200,reinterpret_cast<const float*>(raw),1))
                &&SUCCEEDED(d->GetVertexShaderConstantF(200,observed,1))&&!std::memcmp(raw,observed,16)
                &&SUCCEEDED(d->SetVertexShaderConstantF(0,saved,2));
            if(okay){benchmark_shadow=true;constants_hint.Clear();
                const auto queries=constants_queries,duplicates=constants_skipped,invalidations=constants_invalidations;
                okay=SUCCEEDED(d->SetVertexShaderConstantF(0,b,1))&&SUCCEEDED(d->SetVertexShaderConstantF(0,b,1))
                    &&constants_skipped==duplicates+1&&constants_queries==queries
                    &&SUCCEEDED(preexisting.value->Apply())&&constants_invalidations==invalidations+1
                    &&SUCCEEDED(d->SetVertexShaderConstantF(0,b,1))&&SUCCEEDED(d->GetVertexShaderConstantF(0,observed,1))
                    &&!std::memcmp(b,observed,16)&&constants_skipped==duplicates+1
                    &&SUCCEEDED(d->SetVertexShaderConstantF(0,a,1))&&SUCCEEDED(block.value->Apply())
                    &&SUCCEEDED(d->SetVertexShaderConstantF(0,b,1))&&SUCCEEDED(d->GetVertexShaderConstantF(0,observed,1))
                    &&!std::memcmp(b,observed,16)&&SUCCEEDED(d->SetVertexShaderConstantF(0,saved,2));
                Ref<IDirect3DStateBlock9> recorded;
                if(okay)okay=SUCCEEDED(d->SetVertexShaderConstantF(0,a,1))&&SUCCEEDED(d->BeginStateBlock())
                    &&SUCCEEDED(d->SetVertexShaderConstantF(0,b,1))&&SUCCEEDED(d->EndStateBlock(&recorded.value))
                    &&SUCCEEDED(d->SetVertexShaderConstantF(0,a,1))&&SUCCEEDED(d->GetVertexShaderConstantF(0,observed,1))
                    &&!std::memcmp(a,observed,16)&&SUCCEEDED(recorded.value->Apply())
                    &&SUCCEEDED(d->SetVertexShaderConstantF(0,a,1))&&SUCCEEDED(d->GetVertexShaderConstantF(0,observed,1))
                    &&!std::memcmp(a,observed,16)&&SUCCEEDED(d->SetVertexShaderConstantF(0,saved,2));
                benchmark_shadow=false;}
            benchmark_constants=false;
        }
        D3DVIEWPORT9 after{};IDirect3DPixelShader9* bound=nullptr;DWORD z=1,cull=0,blend=1;
        if(okay)stage=140+phase*100;
        if(okay)okay=SUCCEEDED(d->GetViewport(&after))&&!std::memcmp(&after,&viewport,sizeof(after))
            &&SUCCEEDED(d->GetPixelShader(&bound))&&bound==ps&&SUCCEEDED(d->GetRenderState(D3DRS_ZENABLE,&z))&&!z
            &&SUCCEEDED(d->GetRenderState(D3DRS_CULLMODE,&cull))&&cull==D3DCULL_NONE
            &&SUCCEEDED(d->GetRenderState(D3DRS_ALPHABLENDENABLE,&blend))&&!blend;
        if(bound)bound->Release();
        if(okay)okay=SUCCEEDED(d->Present(nullptr,nullptr,nullptr,nullptr));
    }
    okay=okay&&draws==3&&!matches&&!snapshot&&!failure&&!failed_draws&&presents==3;
    if(on12_hr==S_OK)okay=okay&&pixel_gets_avoided==1;
    if(on12_hr==S_OK)for(const auto& metric:api_stats)okay=okay&&metric.calls>0;
    bool reset_passed=false;
    if(okay)okay=SUCCEEDED(d->SetPixelShader(nullptr))&&SUCCEEDED(d->SetStreamSource(0,nullptr,0,0))&&SUCCEEDED(d->SetIndices(nullptr))
        &&SUCCEEDED(d->SetRenderTarget(0,original_target.value));
    if(preexisting.value){preexisting.value->Release();preexisting.value=nullptr;}
    if(ps){ps->Release();ps=nullptr;}if(ib){ib->Release();ib=nullptr;}if(vb){vb->Release();vb=nullptr;}
    if(staging){staging->Release();staging=nullptr;}if(target){target->Release();target=nullptr;}
    if(original_target.value){original_target.value->Release();original_target.value=nullptr;}
    if(okay){reset_passed=SUCCEEDED(d->Reset(&pp))&&cache_resets==1&&!pixel_cache.count&&!vertex_cache.count;okay=reset_passed;}
    if(okay){const auto before=pixel_reads;okay=SUCCEEDED(d->CreatePixelShader(program,&ps))&&!PixelTarget(ps)&&!PixelTarget(ps)&&pixel_reads==before+1;}
    pixel_cache.Clear();vertex_cache.Clear();
    std::printf("{\"non_target_pixels_and_state_passed\":%s,\"hooked_draws\":%llu,\"snapshot_done\":false,\"on12_query_hr\":%ld,\"stage\":%u,\"pixel\":%lu,\"sample_hr\":%ld,\"failure\":%lu,\"cache_reset_and_reclassification_passed\":%s,\"synthetic_only\":true}\n",
        okay?"true":"false",static_cast<unsigned long long>(draws),static_cast<long>(on12_hr),stage,observed_pixel,static_cast<long>(sampled_hr),failure,reset_passed&&okay?"true":"false");
    if(ps)ps->Release();if(ib)ib->Release();if(vb)vb->Release();if(staging)staging->Release();if(target)target->Release();
    d->Release();api->Release();DestroyWindow(window);return okay?0:10;
}
