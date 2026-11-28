#pragma once
// Included by the authored GPU harness after its shared helpers/macros.
int GraphGpu(ID3D12Device* device){
    using namespace On12Graphics;
    UINT w=9,h=9;
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;REQUIRE(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
    ComPtr<ID3D12Fence> fence,gate;REQUIRE(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))&&SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&gate))));
    std::array<ComPtr<ID3D12CommandAllocator>,3> allocators;std::array<ComPtr<ID3D12GraphicsCommandList>,3> lists;
    for(UINT i=0;i<3;++i){REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocators[i]))));
        REQUIRE(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocators[i].Get(),nullptr,IID_PPV_ARGS(&lists[i])))&&SUCCEEDED(lists[i]->Close()));}
    std::array<ComPtr<ID3D12Resource>,4> inputs,uploads;std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,4> footprints{};
    const DXGI_FORMAT formats[]{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R32G32B32A32_FLOAT,DXGI_FORMAT_R32_FLOAT,DXGI_FORMAT_R32_FLOAT};
    ComPtr<ID3D12Resource> output,readback;D3D12_PLACED_SUBRESOURCE_FOOTPRINT out_footprint{};
    auto resize=[&]()->bool{
    for(UINT i=0;i<4;++i){inputs[i].Reset();uploads[i].Reset();const auto d=Texture(w,h,formats[i],false);auto heap=Heap(D3D12_HEAP_TYPE_DEFAULT);
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,nullptr,IID_PPV_ARGS(&inputs[i]))))return false;
        UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&footprints[i],nullptr,nullptr,&bytes);auto b=Buffer(bytes);heap=Heap(D3D12_HEAP_TYPE_UPLOAD);
        if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&uploads[i]))))return false;
    }
    output.Reset();readback.Reset();const auto out_desc=Texture(w,h,DXGI_FORMAT_R8G8B8A8_UNORM,true);auto heap=Heap(D3D12_HEAP_TYPE_DEFAULT);
    if(FAILED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&out_desc,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&output))))return false;
    UINT64 bytes=0;device->GetCopyableFootprints(&out_desc,0,1,0,&out_footprint,nullptr,nullptr,&bytes);auto b=Buffer(bytes);heap=Heap(D3D12_HEAP_TYPE_READBACK);
    return SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback)));
    };REQUIRE(resize());
    const auto vs=Read(L"on12_temporal_vs.bin"),ps=Read(L"on12_temporal_ps.bin");TemporalGraph graph;
    REQUIRE(SUCCEEDED(graph.Initialize(device,fence.Get(),DXGI_FORMAT_R8G8B8A8_UNORM,vs.data(),vs.size(),ps.data(),ps.size())));
    auto wait=[&](UINT64 value){HANDLE event=CreateEventW(nullptr,FALSE,FALSE,nullptr);if(!event)return false;const auto hr=fence->SetEventOnCompletion(value,event);const auto result=SUCCEEDED(hr)?WaitForSingleObject(event,10000):WAIT_FAILED;CloseHandle(event);return result==WAIT_OBJECT_0;};
    std::vector<float> filtered;
    for(UINT frame=1;frame<=38;++frame){const UINT slot=(frame-1)%3;auto* list=lists[slot].Get();
        if(frame==36||frame==37){w=frame==36?5:9;h=frame==36?7:9;REQUIRE(resize());}
        REQUIRE(SUCCEEDED(allocators[slot]->Reset())&&SUCCEEDED(list->Reset(allocators[slot].Get(),nullptr)));
        for(UINT input=0;input<4;++input){void* mapped=nullptr;D3D12_RANGE empty{};REQUIRE(SUCCEEDED(uploads[input]->Map(0,&empty,&mapped)));
            for(UINT y=0;y<h;++y){auto* row=static_cast<unsigned char*>(mapped)+footprints[input].Offset+y*footprints[input].Footprint.RowPitch;
                for(UINT x=0;x<w;++x){if(input==0){const unsigned char value=frame>=35?128:((x+y+frame)&1?255:0);const unsigned char color[]{value,value,value,static_cast<unsigned char>(frame>=35?77:255)};std::memcpy(row+x*4,color,4);}
                    if(input==1){const float motion[]{0,0,10,1};std::memcpy(row+x*16,motion,16);}
                    if(input>=2){const float value=input==2?10.0f:0.0f;std::memcpy(row+x*4,&value,4);}}
            }uploads[input]->Unmap(0,nullptr);
            Barrier(list,inputs[input].Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COPY_DEST);
            D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=uploads[input].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprints[input];
            D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=inputs[input].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
            Barrier(list,inputs[input].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        }
        TemporalGraphInput in;in.color=inputs[0].Get();in.motion=inputs[1].Get();in.depth=inputs[2].Get();in.reactive=inputs[3].Get();in.output=output.Get();
        in.frame.enabled=true;in.frame.sequence=frame;in.frame.projection_epoch=1;in.frame.width=w;in.frame.height=h;in.frame.proofs=Temporal::AllProofs;
        in.frame.camera_cut=frame==35;
        const auto jitter=Temporal::Jitter(frame);in.frame.jitter_x=jitter[0];in.frame.jitter_y=jitter[1];
        if(frame==1){auto missing=in;missing.frame.proofs&=~Temporal::DynamicMotionAndReactive;REQUIRE(graph.Record(list,slot,missing)==S_FALSE);}
        if(frame==2){REQUIRE(graph.Record(list,slot,in)==S_OK);graph.Abort();REQUIRE(SUCCEEDED(list->Close())&&SUCCEEDED(list->Reset(allocators[slot].Get(),nullptr)));
            // Upload commands were also discarded. Their prior values are
            // still valid input for this aborted-frame lifecycle check.
        }
        REQUIRE(graph.Record(list,slot,in)==S_OK);
        Barrier(list,output.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=output.Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback.Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=out_footprint;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        Barrier(list,output.Get(),D3D12_RESOURCE_STATE_COPY_SOURCE,D3D12_RESOURCE_STATE_RENDER_TARGET);
        REQUIRE(SUCCEEDED(list->Close()));
        if(frame==1)REQUIRE(SUCCEEDED(queue->Wait(gate.Get(),1)));
        ID3D12CommandList* submitted[]{list};queue->ExecuteCommandLists(1,submitted);REQUIRE(SUCCEEDED(queue->Signal(fence.Get(),frame)));
        auto wrong=in.frame;++wrong.sequence;REQUIRE(!graph.Commit(wrong,frame)&&graph.Commit(in.frame,frame));
        if(frame==1){REQUIRE(graph.Record(list,slot,in)==S_FALSE);REQUIRE(SUCCEEDED(gate->Signal(1)));}
        REQUIRE(wait(frame));void* mapped=nullptr;REQUIRE(SUCCEEDED(readback->Map(0,nullptr,&mapped)));
        const auto* center=static_cast<const unsigned char*>(mapped)+out_footprint.Offset+(h/2)*out_footprint.Footprint.RowPitch+(w/2)*4;
        if(frame>=9&&frame<35)filtered.push_back(center[0]/255.0f);
        if(frame>=35)REQUIRE(center[0]==128&&center[1]==128&&center[2]==128&&center[3]==77);
        readback->Unmap(0,nullptr);
    }
    float mean=0,variance=0;for(float value:filtered)mean+=value/static_cast<float>(filtered.size());for(float value:filtered)variance+=(value-mean)*(value-mean)/static_cast<float>(filtered.size());
    const auto stats=graph.Stats();REQUIRE(stats.committed==38&&stats.reused==33&&stats.resets==5&&stats.aborted==1&&stats.busy==1);
    REQUIRE(variance<0.25f*0.55f);
    std::fprintf(stderr,"temporal graph: committed=%llu reused=%llu aborted=%llu variance=%f\n",static_cast<unsigned long long>(stats.committed),static_cast<unsigned long long>(stats.reused),static_cast<unsigned long long>(stats.aborted),variance);
    REQUIRE(SUCCEEDED(graph.Shutdown()));return 0;
}
