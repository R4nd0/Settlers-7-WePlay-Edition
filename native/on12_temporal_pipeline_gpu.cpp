// Reuse the authored device/resource helpers and existing conformance suite.
#define wmain TemporalCoreFixtureMain
#include "on12_temporal_gpu.cpp"
#undef wmain
#include "on12_temporal_pipeline.h"

int wmain(int argc,wchar_t** argv){
    if(argc!=2||wcscmp(argv[1],L"--gpu"))return 1;
    const auto core=TemporalCoreFixtureMain(argc,argv);if(core)return core;
    ComPtr<ID3D12Device> device;REQUIRE(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
    constexpr UINT w=8,h=8,n=w*h;
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;
    REQUIRE(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;ComPtr<ID3D12Fence> fence;
    REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator)))
        &&SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list)))
        &&SUCCEEDED(list->Close())&&SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));
    std::array<ComPtr<ID3D12Resource>,4> inputs,uploads;std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,4> footprints{};
    for(UINT i=0;i<4;++i){auto d=Texture(w,h,i?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R8G8B8A8_UNORM,false);auto heap=Heap(D3D12_HEAP_TYPE_DEFAULT);
        REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&inputs[i]))));
        UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&footprints[i],nullptr,nullptr,&bytes);auto b=Buffer(bytes);heap=Heap(D3D12_HEAP_TYPE_UPLOAD);
        REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&uploads[i]))));
    }
    ComPtr<ID3D12Resource> output,readback;auto d=Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,true);auto heap=Heap(D3D12_HEAP_TYPE_DEFAULT);
    REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&output))));
    D3D12_PLACED_SUBRESOURCE_FOOTPRINT fp{};UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&fp,nullptr,nullptr,&bytes);auto b=Buffer(bytes);heap=Heap(D3D12_HEAP_TYPE_READBACK);
    REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback))));
    const auto tv=Read(L"on12_temporal_vs.bin"),tp=Read(L"on12_temporal_ps.bin"),cv=Read(L"on12_temporal_camera_vs.bin"),cp=Read(L"on12_temporal_camera_ps.bin");
    TemporalPipeline pipeline;REQUIRE(SUCCEEDED(pipeline.Initialize(device.Get(),fence.Get(),d.Format,tv.data(),tv.size(),tp.data(),tp.size(),cv.data(),cv.size(),cp.data(),cp.size())));
    Temporal::CameraMatrix camera{};camera[0]=1.5f;camera[5]=2;camera[10]=100.0f/99.9f;camera[11]=1;camera[14]=-0.1f*camera[10];
    Temporal::History history;Temporal::HistoryState state;
    for(UINT frame=1;frame<=8;++frame){
        REQUIRE(SUCCEEDED(allocator->Reset())&&SUCCEEDED(list->Reset(allocator.Get(),nullptr)));
        TemporalPipelineInput in;in.color=inputs[0].Get();in.normalized_depth=inputs[1].Get();in.static_confidence=inputs[2].Get();in.reactive=inputs[3].Get();in.output=output.Get();
        in.frame.enabled=true;in.frame.sequence=frame;in.frame.projection_epoch=1;in.frame.width=w;in.frame.height=h;in.frame.proofs=Temporal::AllProofs;
        const auto jitter=Temporal::Jitter(frame);in.frame.jitter_x=jitter[0];in.frame.jitter_y=jitter[1];
        REQUIRE(Temporal::BuildCamera(camera,camera,100,w,h,jitter[0],jitter[1],0,in.camera));
        Temporal::Image image;image.width=w;image.height=h;image.color.resize(n);image.depth.resize(n);image.motion.resize(n);image.reactive.resize(n);
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){const auto i=y*w+x;const float value=((x+y+frame)&1)?1.0f:0.0f;
            image.color[i]={value,value,value,77.0f/255};image.reactive[i]=x>=6?1.0f:0.0f;
            REQUIRE(Temporal::CameraMotion(in.camera,x,y,0.1f,x<2?0.0f:1.0f,image.motion[i],image.depth[i]));
        }
        for(UINT k=0;k<4;++k){void* mapped=nullptr;D3D12_RANGE empty{};REQUIRE(SUCCEEDED(uploads[k]->Map(0,&empty,&mapped)));
            for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){auto* destination=static_cast<unsigned char*>(mapped)+footprints[k].Offset+y*footprints[k].Footprint.RowPitch+x*4;
                if(k==0){const unsigned char value=image.color[y*w+x].r>0.5f?255:0;const unsigned char rgba[]{value,value,value,77};std::memcpy(destination,rgba,4);}
                else{const float value=k==1?0.1f:k==2?(x<2?0.0f:1.0f):image.reactive[y*w+x];std::memcpy(destination,&value,4);}
            }uploads[k]->Unmap(0,nullptr);
            Barrier(list.Get(),inputs[k].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=uploads[k].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprints[k];
            D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=inputs[k].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            Barrier(list.Get(),inputs[k].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        const auto slot=(frame-1)%3;
        if(frame==1){auto missing=in;missing.frame.proofs&=~Temporal::DynamicMotionAndReactive;REQUIRE(pipeline.Record(list.Get(),slot,missing)==S_FALSE);}
        const auto decision=state.Prepare(in.frame,in.quality);Temporal::Image expected;Temporal::History next;
        REQUIRE(decision.dispatch&&Temporal::Resolve(image,history,decision.parameters,expected,next));
        REQUIRE(pipeline.Record(list.Get(),slot,in)==S_OK);
        Barrier(list.Get(),output.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=output.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=fp;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        Barrier(list.Get(),output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
        REQUIRE(SUCCEEDED(list->Close()));ID3D12CommandList* lists[]{list.Get()};queue->ExecuteCommandLists(1,lists);REQUIRE(SUCCEEDED(queue->Signal(fence.Get(),frame))&&pipeline.Commit(in.frame,frame)&&state.Commit(in.frame));
        HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);REQUIRE(event);const auto signaled=fence->SetEventOnCompletion(frame,event);const auto waited=SUCCEEDED(signaled)?WaitForSingleObject(event,10000):WAIT_FAILED;CloseHandle(event);REQUIRE(waited==WAIT_OBJECT_0);
        void* mapped=nullptr;REQUIRE(SUCCEEDED(readback->Map(0,nullptr,&mapped)));
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){const auto* pixel=static_cast<const unsigned char*>(mapped)+fp.Offset+y*fp.Footprint.RowPitch+x*4;const auto& e=expected.color[y*w+x];
            REQUIRE(std::fabs(pixel[0]/255.0f-e.r)<=2.0f/255&&std::fabs(pixel[1]/255.0f-e.g)<=2.0f/255&&std::fabs(pixel[2]/255.0f-e.b)<=2.0f/255&&pixel[3]==77);
            if(x<2||x>=6)REQUIRE(pixel[0]==static_cast<unsigned char>(image.color[y*w+x].r*255));
        }readback->Unmap(0,nullptr);history=std::move(next);
    }
    const auto stats=pipeline.Stats();REQUIRE(stats.committed==8&&stats.reused==7&&SUCCEEDED(pipeline.Shutdown()));
    std::printf("{\"passed\":true,\"checks\":%u,\"synthetic_only\":true,\"pipeline_reference_matched\":true,\"unclassified_rejected\":true,\"reactive_rejected\":true,\"game_taa_enabled\":false}\n",checks);return 0;
}
