#pragma once
// Authored end-to-end D3D9 -> shared native queue -> TAA -> D3D9 checks.
int TemporalHandoffGpu(IDirect3DDevice9* device,ID3DBlob* spatial_vs,ID3DBlob* spatial_ps){
    constexpr UINT w=8,h=8,n=w*h;constexpr UINT compile=D3DCOMPILE_ENABLE_STRICTNESS|D3DCOMPILE_WARNINGS_ARE_ERRORS|D3DCOMPILE_OPTIMIZATION_LEVEL3;
    std::array<ComPtr<ID3DBlob>,4> shaders;ComPtr<ID3DBlob> error;
    for(UINT i=0;i<4;++i){error.Reset();const auto hr=D3DCompileFromFile(i<2?L"on12_temporal.hlsl":L"on12_temporal_camera.hlsl",nullptr,nullptr,i%2?"PS":"VS",i%2?"ps_5_0":"vs_5_0",compile,0,&shaders[i],&error);
        if(FAILED(hr)&&error)std::fprintf(stderr,"%s",static_cast<const char*>(error->GetBufferPointer()));REQUIRE(SUCCEEDED(hr));}
    FrameContext context;REQUIRE(SUCCEEDED(context.Initialize(device,spatial_vs->GetBufferPointer(),spatial_vs->GetBufferSize(),spatial_ps->GetBufferPointer(),spatial_ps->GetBufferSize())));
    auto prepare=[&](FrameContext& c,bool invalid){return c.PrepareTemporal(shaders[0]->GetBufferPointer(),shaders[0]->GetBufferSize(),shaders[invalid?0:1]->GetBufferPointer(),shaders[invalid?0:1]->GetBufferSize(),shaders[2]->GetBufferPointer(),shaders[2]->GetBufferSize(),shaders[3]->GetBufferPointer(),shaders[3]->GetBufferSize());};
    REQUIRE(SUCCEEDED(prepare(context,false)));
    ComPtr<IDirect3DSurface9> color,target;REQUIRE(SUCCEEDED(device->CreateRenderTarget(w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&color,nullptr))&&SUCCEEDED(device->CreateRenderTarget(w,h,D3DFMT_A8R8G8B8,D3DMULTISAMPLE_NONE,0,FALSE,&target,nullptr)));
    std::array<ComPtr<IDirect3DTexture9>,3> textures;std::array<ComPtr<IDirect3DSurface9>,3> data;
    ComPtr<IDirect3DSurface9> previous,depth;D3DVIEWPORT9 viewport{};
    REQUIRE(SUCCEEDED(device->GetRenderTarget(0,&previous))&&SUCCEEDED(device->GetDepthStencilSurface(&depth))&&SUCCEEDED(device->GetViewport(&viewport))&&SUCCEEDED(device->SetDepthStencilSurface(nullptr)));
    for(UINT i=0;i<3;++i){REQUIRE(SUCCEEDED(device->CreateTexture(w,h,1,D3DUSAGE_RENDERTARGET,D3DFMT_R32F,D3DPOOL_DEFAULT,&textures[i],nullptr))&&SUCCEEDED(textures[i]->GetSurfaceLevel(0,&data[i])));
        REQUIRE(SUCCEEDED(device->SetRenderTarget(0,data[i].Get()))&&SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET,i==0?0xff190000:i==1?0xffffffff:0xff000000,1,0)));
        const D3DRECT area{static_cast<LONG>(i==1?0:6),0,static_cast<LONG>(i==1?2:8),8};
        if(i)REQUIRE(SUCCEEDED(device->Clear(1,&area,D3DCLEAR_TARGET,i==1?0xff000000:0xffffffff,1,0)));
    }
    REQUIRE(SUCCEEDED(device->SetRenderTarget(0,previous.Get()))&&SUCCEEDED(device->SetDepthStencilSurface(depth.Get()))&&SUCCEEDED(device->SetViewport(&viewport)));
    On12Graphics::TemporalSceneSettings in;in.normalized_depth=data[0].Get();in.static_confidence=data[1].Get();in.reactive=data[2].Get();
    in.frame.enabled=true;in.frame.projection_epoch=1;in.frame.width=w;in.frame.height=h;in.frame.proofs=On12Graphics::Temporal::AllProofs;
    On12Graphics::Temporal::CameraMatrix camera{};camera[0]=1.5f;camera[5]=2;camera[10]=100.0f/99.9f;camera[11]=1;camera[14]=-0.1f*camera[10];
    On12Graphics::Temporal::History history;On12Graphics::Temporal::HistoryState state;SpatialSettings spatial;spatial.enabled=1;
    for(UINT frame=1;frame<=4;++frame){in.frame.sequence=frame;const auto jitter=On12Graphics::Temporal::Jitter(frame);in.frame.jitter_x=jitter[0];in.frame.jitter_y=jitter[1];
        REQUIRE(On12Graphics::Temporal::BuildCamera(camera,camera,100,w,h,jitter[0],jitter[1],0,in.camera));
        On12Graphics::Temporal::Image image;image.width=w;image.height=h;image.color.resize(n);image.depth.resize(n);image.motion.resize(n);image.reactive.resize(n);
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){const auto i=y*w+x;const bool white=((x+y+frame)&1)!=0;const RECT pixel{static_cast<LONG>(x),static_cast<LONG>(y),static_cast<LONG>(x+1),static_cast<LONG>(y+1)};
            REQUIRE(SUCCEEDED(device->ColorFill(color.Get(),&pixel,white?0x4dffffff:0x4d000000)));const float value=white?1.0f:0.0f;
            image.color[i]={value,value,value,77.0f/255};image.reactive[i]=x>=6?1.0f:0.0f;
            REQUIRE(On12Graphics::Temporal::CameraMotion(in.camera,x,y,25.0f/255,x<2?0.0f:1.0f,image.motion[i],image.depth[i]));
        }
        if(frame==1){auto invalid=in;invalid.normalized_depth=color.Get();REQUIRE(context.RenderTemporal(color.Get(),target.Get(),spatial,invalid)==E_INVALIDARG&&!context.Stats().checkouts);}
        if(frame==4){auto missing=in;missing.frame.proofs&=~On12Graphics::Temporal::DynamicMotionAndReactive;const auto before=context.Stats().checkouts;
            REQUIRE(context.RenderTemporal(color.Get(),target.Get(),spatial,missing)==S_FALSE&&context.Stats().checkouts==before);state.Abort();}
        const auto decision=state.Prepare(in.frame,in.quality);On12Graphics::Temporal::Image expected;On12Graphics::Temporal::History next;
        REQUIRE(decision.dispatch&&On12Graphics::Temporal::Resolve(image,history,decision.parameters,expected,next));
        auto* output=frame==2?color.Get():target.Get();const auto result=context.RenderTemporal(color.Get(),output,spatial,in);
        if(result!=S_OK)std::fprintf(stderr,"temporal handoff=%08lx frame=%u\n",result,frame);REQUIRE(result==S_OK&&context.SafeForD3D9()&&state.Commit(in.frame));
        std::vector<DWORD> pixels;REQUIRE(Read(device,output,w,h,pixels));
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){const auto i=y*w+x;const auto value=static_cast<unsigned>(std::lround(expected.color[i].r*255));
            REQUIRE(Color(pixels[i],0xff000000|value*0x010101,2)&&((pixels[i]>>24)&255)==77);
            if(x<2||x>=6)REQUIRE((pixels[i]&255)==static_cast<unsigned>(image.color[i].r*255));
        }history=std::move(next);
    }
    REQUIRE(SUCCEEDED(context.Drain(5000)));const auto stats=context.Stats();
    REQUIRE(stats.temporal_applied==4&&stats.temporal_rejected==1&&stats.checkouts==19&&stats.returns==19&&!stats.failed_returns);
    REQUIRE(SUCCEEDED(context.Shutdown(5000)));
    FrameContext bad;REQUIRE(SUCCEEDED(bad.Initialize(device,spatial_vs->GetBufferPointer(),spatial_vs->GetBufferSize(),spatial_ps->GetBufferPointer(),spatial_ps->GetBufferSize()))&&SUCCEEDED(prepare(bad,true)));
    REQUIRE(FAILED(bad.RenderTemporal(color.Get(),target.Get(),spatial,in))&&bad.SafeForD3D9()&&bad.Stats().checkouts==5&&bad.Stats().returns==5&&SUCCEEDED(bad.Shutdown(5000)));
    D3DVIEWPORT9 after{};ComPtr<IDirect3DSurface9> current;
    REQUIRE(SUCCEEDED(device->GetViewport(&after))&&!std::memcmp(&after,&viewport,sizeof(viewport))&&SUCCEEDED(device->GetRenderTarget(0,&current))&&current.Get()==previous.Get());
    std::fprintf(stderr,"temporal handoff: frames=%llu checkouts=%llu returns=%llu unclassified/reactive/in-place/failed-PSO passed\n",static_cast<unsigned long long>(stats.temporal_applied),static_cast<unsigned long long>(stats.checkouts),static_cast<unsigned long long>(stats.returns));return 0;
}
