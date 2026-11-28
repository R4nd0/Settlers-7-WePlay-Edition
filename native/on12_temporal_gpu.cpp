// Prepared during CPU work. No graphics calls unless explicitly run --gpu.
#include "on12_temporal_pass.h"
#include "on12_temporal_camera_pass.h"
#include "on12_temporal_graph.h"
#include <dxgi1_4.h>
#include <vector>
#include <cstdio>
#include <cstring>
#include <cmath>
#include <limits>

using Microsoft::WRL::ComPtr;
using namespace On12Graphics;
namespace {
unsigned checks=0;
bool Check(bool okay,unsigned line){++checks;if(!okay)std::fprintf(stderr,"temporal GPU check failed at %u\n",line);return okay;}
#define REQUIRE(x) do{if(!Check(!!(x),__LINE__))return 20;}while(false)
std::vector<unsigned char> Read(const wchar_t* path){FILE* file=nullptr;if(_wfopen_s(&file,path,L"rb")||!file)return{};std::fseek(file,0,SEEK_END);const long count=std::ftell(file);std::rewind(file);
    std::vector<unsigned char> bytes;if(count>0&&count<2*1024*1024){bytes.resize(static_cast<size_t>(count));if(std::fread(bytes.data(),1,bytes.size(),file)!=bytes.size())bytes.clear();}std::fclose(file);return bytes;}
D3D12_HEAP_PROPERTIES Heap(D3D12_HEAP_TYPE type){D3D12_HEAP_PROPERTIES p{};p.Type=type;p.CreationNodeMask=p.VisibleNodeMask=1;return p;}
D3D12_RESOURCE_DESC Buffer(UINT64 size){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_BUFFER;d.Width=size;d.Height=1;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Layout=D3D12_TEXTURE_LAYOUT_ROW_MAJOR;return d;}
D3D12_RESOURCE_DESC Texture(UINT w,UINT h,DXGI_FORMAT f,bool target){D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Format=f;d.Flags=target?D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET:D3D12_RESOURCE_FLAG_NONE;return d;}
void Barrier(ID3D12GraphicsCommandList* list,ID3D12Resource* r,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={r,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);}
float Half(unsigned short h){const bool sign=(h&0x8000)!=0;const unsigned e=(h>>10)&31,m=h&1023;
    const float value=e==0?std::ldexp(static_cast<float>(m),-24):e==31?(m?NAN:INFINITY):std::ldexp(1.0f+static_cast<float>(m)/1024,static_cast<int>(e)-15);return sign?-value:value;}
int CameraGpu(ID3D12Device* device){
    constexpr UINT w=8,h=8,n=w*h;
    Temporal::CameraMatrix previous{};previous[0]=1.5f;previous[5]=2;previous[10]=100.0f/99.9f;previous[11]=1;previous[14]=-0.1f*previous[10];
    auto current=previous;current[12]=-0.6f;current[13]=0.6f;current[14]-=0.5f*previous[10];current[15]=-0.5f;
    Temporal::CameraParameters p;REQUIRE(Temporal::BuildCamera(current,previous,100,w,h,0.25f,-0.375f,0,p));
    std::array<std::vector<float>,2> data;for(auto& v:data)v.resize(n);
    std::vector<Temporal::Motion> expected(n);std::vector<float> depth(n);
    for(UINT i=0;i<n;++i){data[0][i]=0.06f+0.001f*i;data[1][i]=i%w<2?0.0f:i%w<6?1.0f:0.25f;}
    data[0][0]=0;data[0][n-1]=1;data[1][18]=(std::numeric_limits<float>::quiet_NaN)();
    for(UINT i=0;i<n;++i)Temporal::CameraMotion(p,i%w,i/w,data[0][i],data[1][i],expected[i],depth[i]);
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;REQUIRE(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
    REQUIRE(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};srv_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;srv_desc.NumDescriptors=2;srv_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> srv;REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&srv_desc,IID_PPV_ARGS(&srv))));
    std::array<ComPtr<ID3D12Resource>,2> inputs,upload;
    for(UINT i=0;i<2;++i){auto d=Texture(w,h,DXGI_FORMAT_R32_FLOAT,false);auto heap=Heap(D3D12_HEAP_TYPE_DEFAULT);
        REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&inputs[i]))));
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&footprint,nullptr,nullptr,&bytes);
        auto b=Buffer(bytes);heap=Heap(D3D12_HEAP_TYPE_UPLOAD);REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&upload[i]))));
        void* mapped=nullptr;D3D12_RANGE empty{};REQUIRE(SUCCEEDED(upload[i]->Map(0,&empty,&mapped)));
        for(UINT y=0;y<h;++y)std::memcpy(static_cast<unsigned char*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch,data[i].data()+y*w,w*sizeof(float));upload[i]->Unmap(0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=upload[i].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=inputs[i].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        Barrier(list.Get(),inputs[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=d.Format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
        auto handle=srv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=static_cast<SIZE_T>(i)*device->GetDescriptorHandleIncrementSize(srv_desc.Type);device->CreateShaderResourceView(inputs[i].Get(),&view,handle);
    }
    std::array<ComPtr<ID3D12Resource>,2> targets,readback;std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,2> footprints{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,2> handles{};D3D12_DESCRIPTOR_HEAP_DESC rt_desc{};rt_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;rt_desc.NumDescriptors=2;
    ComPtr<ID3D12DescriptorHeap> rtv;REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&rt_desc,IID_PPV_ARGS(&rtv))));
    for(UINT i=0;i<2;++i){auto d=Texture(w,h,i?DXGI_FORMAT_R32_FLOAT:DXGI_FORMAT_R32G32B32A32_FLOAT,true);auto heap=Heap(D3D12_HEAP_TYPE_DEFAULT);
        REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&targets[i]))));
        handles[i]=rtv->GetCPUDescriptorHandleForHeapStart();handles[i].ptr+=static_cast<SIZE_T>(i)*device->GetDescriptorHandleIncrementSize(rt_desc.Type);device->CreateRenderTargetView(targets[i].Get(),nullptr,handles[i]);
        UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&footprints[i],nullptr,nullptr,&bytes);auto b=Buffer(bytes);heap=Heap(D3D12_HEAP_TYPE_READBACK);
        REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback[i]))));
    }
    auto vs=Read(L"on12_temporal_camera_vs.bin"),ps=Read(L"on12_temporal_camera_ps.bin");REQUIRE(!vs.empty()&&!ps.empty());CameraPass pass;
    REQUIRE(SUCCEEDED(pass.Initialize(device,vs.data(),vs.size(),ps.data(),ps.size()))&&SUCCEEDED(pass.Record(list.Get(),srv.Get(),handles,p)));
    for(UINT i=0;i<2;++i){Barrier(list.Get(),targets[i].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=targets[i].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback[i].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprints[i];list->CopyTextureRegion(&to,0,0,0,&from,nullptr);}
    REQUIRE(SUCCEEDED(list->Close()));ID3D12CommandList* commands[]{list.Get()};queue->ExecuteCommandLists(1,commands);
    ComPtr<ID3D12Fence> fence;REQUIRE(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence)))&&SUCCEEDED(queue->Signal(fence.Get(),1)));
    HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);REQUIRE(done);REQUIRE(SUCCEEDED(fence->SetEventOnCompletion(1,done)));const DWORD waited=WaitForSingleObject(done,10000);CloseHandle(done);REQUIRE(waited==WAIT_OBJECT_0);
    for(UINT t=0;t<2;++t){void* mapped=nullptr;REQUIRE(SUCCEEDED(readback[t]->Map(0,nullptr,&mapped)));
        for(UINT y=0;y<h;++y)for(UINT x=0;x<w;++x){const UINT i=y*w+x;const auto* row=static_cast<const unsigned char*>(mapped)+footprints[t].Offset+y*footprints[t].Footprint.RowPitch;
            if(t==0){float values[4]{};std::memcpy(values,row+x*16,sizeof(values));const auto& e=expected[i];const float wanted[]{e.x,e.y,e.previous_depth,e.confidence};for(UINT c=0;c<4;++c)REQUIRE(std::fabs(values[c]-wanted[c])<0.0003f);}
            else{float value=0;std::memcpy(&value,row+x*4,4);REQUIRE(std::fabs(value-depth[i])<0.0003f);}}
        readback[t]->Unmap(0,nullptr);
    }return 0;
}
#include "on12_temporal_graph_gpu.h"
}
int wmain(int argc,wchar_t** argv){
    if(argc!=2||wcscmp(argv[1],L"--gpu")){std::fputs("Explicit --gpu is required; no GPU was created.\n",stderr);return 2;}
    ComPtr<ID3D12Device> device;REQUIRE(SUCCEEDED(D3D12CreateDevice(nullptr,D3D_FEATURE_LEVEL_11_0,IID_PPV_ARGS(&device))));
    REQUIRE(CameraGpu(device.Get())==0);
    REQUIRE(GraphGpu(device.Get())==0);
    ComPtr<ID3D12CommandQueue> queue;D3D12_COMMAND_QUEUE_DESC q{};q.Type=D3D12_COMMAND_LIST_TYPE_DIRECT;REQUIRE(SUCCEEDED(device->CreateCommandQueue(&q,IID_PPV_ARGS(&queue))));
    ComPtr<ID3D12CommandAllocator> allocator;ComPtr<ID3D12GraphicsCommandList> list;
    REQUIRE(SUCCEEDED(device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT,IID_PPV_ARGS(&allocator))));
    REQUIRE(SUCCEEDED(device->CreateCommandList(0,D3D12_COMMAND_LIST_TYPE_DIRECT,allocator.Get(),nullptr,IID_PPV_ARGS(&list))));
    constexpr UINT width=9,height=9,count=width*height;
    Temporal::Image image;image.width=width;image.height=height;image.color.resize(count);image.motion.assign(count,{0,0,10,1});image.depth.assign(count,10);image.reactive.assign(count,0);
    Temporal::History history;history.color.assign(count,{0.6f,0.6f,0.6f,1});history.depth.assign(count,10);
    for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){const UINT i=y*width+x;const float c=(x+y)&1?1.0f:0.0f;image.color[i]={c,c,c,0.75f};if(x<2)history.depth[i]=1;if(y>4)image.motion[i].x=-0.5f;}
    image.color[40]={0.5f,0.5f,0.5f,0.4f};image.reactive[40]=1;image.motion[39].confidence=0;image.depth[0]=0;
    Temporal::Parameters p;p.width=width;p.height=height;p.history_valid=1;
    Temporal::Image expected;Temporal::History expected_history;REQUIRE(Temporal::Resolve(image,history,p,expected,expected_history));
    std::array<std::vector<float>,6> data;
    for(const auto& c:image.color)data[0].insert(data[0].end(),{c.r,c.g,c.b,c.a});
    for(const auto& c:history.color)data[1].insert(data[1].end(),{c.r,c.g,c.b,c.a});
    for(const auto& m:image.motion)data[2].insert(data[2].end(),{m.x,m.y,m.previous_depth,m.confidence});
    data[3]=image.depth;data[4]=history.depth;data[5]=image.reactive;
    std::array<ComPtr<ID3D12Resource>,6> inputs,uploads;
    D3D12_DESCRIPTOR_HEAP_DESC srv_desc{};srv_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;srv_desc.NumDescriptors=6;srv_desc.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
    ComPtr<ID3D12DescriptorHeap> srv;REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&srv_desc,IID_PPV_ARGS(&srv))));
    const UINT srv_stride=device->GetDescriptorHandleIncrementSize(srv_desc.Type);
    for(UINT i=0;i<6;++i){const UINT components=i<3?4u:1u;auto d=Texture(width,height,components==4?DXGI_FORMAT_R32G32B32A32_FLOAT:DXGI_FORMAT_R32_FLOAT,false);auto heap=Heap(D3D12_HEAP_TYPE_DEFAULT);
        REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&inputs[i]))));
        D3D12_PLACED_SUBRESOURCE_FOOTPRINT footprint{};UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&footprint,nullptr,nullptr,&bytes);
        auto b=Buffer(bytes);heap=Heap(D3D12_HEAP_TYPE_UPLOAD);REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_GENERIC_READ,nullptr,IID_PPV_ARGS(&uploads[i]))));
        void* mapped=nullptr;D3D12_RANGE empty{};REQUIRE(SUCCEEDED(uploads[i]->Map(0,&empty,&mapped)));
        for(UINT y=0;y<height;++y)std::memcpy(static_cast<unsigned char*>(mapped)+footprint.Offset+y*footprint.Footprint.RowPitch,data[i].data()+y*width*components,width*components*sizeof(float));uploads[i]->Unmap(0,nullptr);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=uploads[i].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;from.PlacedFootprint=footprint;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=inputs[i].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;list->CopyTextureRegion(&to,0,0,0,&from,nullptr);
        Barrier(list.Get(),inputs[i].Get(),D3D12_RESOURCE_STATE_COPY_DEST,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
        D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=d.Format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
        auto handle=srv->GetCPUDescriptorHandleForHeapStart();handle.ptr+=static_cast<SIZE_T>(i)*srv_stride;device->CreateShaderResourceView(inputs[i].Get(),&view,handle);
    }
    std::array<ComPtr<ID3D12Resource>,3> outputs,readback;std::array<D3D12_PLACED_SUBRESOURCE_FOOTPRINT,3> footprints{};
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,3> handles{};D3D12_DESCRIPTOR_HEAP_DESC rt_desc{};rt_desc.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;rt_desc.NumDescriptors=3;
    ComPtr<ID3D12DescriptorHeap> rtv;REQUIRE(SUCCEEDED(device->CreateDescriptorHeap(&rt_desc,IID_PPV_ARGS(&rtv))));const UINT rt_stride=device->GetDescriptorHandleIncrementSize(rt_desc.Type);
    constexpr DXGI_FORMAT formats[]{DXGI_FORMAT_R8G8B8A8_UNORM,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT};
    for(UINT i=0;i<3;++i){auto d=Texture(width,height,formats[i],true);auto heap=Heap(D3D12_HEAP_TYPE_DEFAULT);
        REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_RENDER_TARGET,nullptr,IID_PPV_ARGS(&outputs[i]))));
        handles[i]=rtv->GetCPUDescriptorHandleForHeapStart();handles[i].ptr+=static_cast<SIZE_T>(i)*rt_stride;device->CreateRenderTargetView(outputs[i].Get(),nullptr,handles[i]);
        UINT64 bytes=0;device->GetCopyableFootprints(&d,0,1,0,&footprints[i],nullptr,nullptr,&bytes);auto b=Buffer(bytes);heap=Heap(D3D12_HEAP_TYPE_READBACK);
        REQUIRE(SUCCEEDED(device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&b,D3D12_RESOURCE_STATE_COPY_DEST,nullptr,IID_PPV_ARGS(&readback[i]))));
    }
    auto vs=Read(L"on12_temporal_vs.bin"),ps=Read(L"on12_temporal_ps.bin");REQUIRE(!vs.empty()&&!ps.empty());TemporalPass pass;
    REQUIRE(SUCCEEDED(pass.Initialize(device.Get(),formats[0],vs.data(),vs.size(),ps.data(),ps.size())));
    REQUIRE(SUCCEEDED(pass.Record(list.Get(),srv.Get(),handles,p)));
    for(UINT i=0;i<3;++i){Barrier(list.Get(),outputs[i].Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_COPY_SOURCE);
        D3D12_TEXTURE_COPY_LOCATION from{};from.pResource=outputs[i].Get();from.Type=D3D12_TEXTURE_COPY_TYPE_SUBRESOURCE_INDEX;
        D3D12_TEXTURE_COPY_LOCATION to{};to.pResource=readback[i].Get();to.Type=D3D12_TEXTURE_COPY_TYPE_PLACED_FOOTPRINT;to.PlacedFootprint=footprints[i];list->CopyTextureRegion(&to,0,0,0,&from,nullptr);}
    REQUIRE(SUCCEEDED(list->Close()));ID3D12CommandList* submitted[]{list.Get()};queue->ExecuteCommandLists(1,submitted);
    ComPtr<ID3D12Fence> fence;REQUIRE(SUCCEEDED(device->CreateFence(0,D3D12_FENCE_FLAG_NONE,IID_PPV_ARGS(&fence))));REQUIRE(SUCCEEDED(queue->Signal(fence.Get(),1)));
    HANDLE done=CreateEventW(nullptr,FALSE,FALSE,nullptr);REQUIRE(done);REQUIRE(SUCCEEDED(fence->SetEventOnCompletion(1,done)));const DWORD waited=WaitForSingleObject(done,10000);CloseHandle(done);REQUIRE(waited==WAIT_OBJECT_0);
    for(UINT target=0;target<3;++target){void* mapped=nullptr;REQUIRE(SUCCEEDED(readback[target]->Map(0,nullptr,&mapped)));
        for(UINT y=0;y<height;++y)for(UINT x=0;x<width;++x){const UINT i=y*width+x;const auto* row=static_cast<const unsigned char*>(mapped)+footprints[target].Offset+y*footprints[target].Footprint.RowPitch;
            if(target==0){const auto e=expected.color[i];const float channels[]{e.r,e.g,e.b,e.a};for(UINT c=0;c<4;++c)REQUIRE(std::fabs(row[x*4+c]/255.0f-channels[c])<=1.1f/255);}
            if(target==1){const auto e=expected_history.color[i];const float channels[]{e.r,e.g,e.b,e.a};for(UINT c=0;c<4;++c){unsigned short h=0;std::memcpy(&h,row+x*8+c*2,2);REQUIRE(std::fabs(Half(h)-channels[c])<0.001f);}}
            if(target==2){float z=0;std::memcpy(&z,row+x*4,4);REQUIRE(std::fabs(z-expected_history.depth[i])<1e-5f);}}
        readback[target]->Unmap(0,nullptr);
    }
    const auto luid=device->GetAdapterLuid();std::printf("{\"passed\":true,\"checks\":%u,\"synthetic_only\":true,\"gpu_device_created\":true,\"cpu_reference_matched\":true,\"camera_reference_matched\":true,\"temporal_graph_matched\":true,\"adapter_luid_low\":%lu,\"adapter_luid_high\":%ld}\n",checks,luid.LowPart,luid.HighPart);return 0;
}
