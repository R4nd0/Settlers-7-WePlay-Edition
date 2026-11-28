#include "on12_temporal_pipeline.h"

namespace On12Graphics {
using Microsoft::WRL::ComPtr;
namespace {
void PipelineBarrier(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
bool PipelineImage(const D3D12_RESOURCE_DESC& d,UINT w,UINT h){return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==w&&d.Height==h&&d.DepthOrArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1;}
}
struct TemporalPipeline::Impl {
    struct Slot {
        ComPtr<ID3D12Resource> motion,depth,source,confidence;
        ComPtr<ID3D12DescriptorHeap> srv,rtv;
        D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;UINT64 done=0;
    };
    ComPtr<ID3D12Device> device;ComPtr<ID3D12Fence> fence;CameraPass camera;TemporalGraph graph;
    std::array<Slot,3> slots;DWORD owner=0;UINT pending_slot=0;bool pending=false;
    HRESULT Images(Slot& slot,UINT w,UINT h){
        if(slot.motion&&PipelineImage(slot.motion->GetDesc(),w,h))return S_OK;
        ComPtr<ID3D12Resource> motion,depth;D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        d.Format=DXGI_FORMAT_R32G32B32A32_FLOAT;auto hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&motion));if(FAILED(hr))return hr;
        d.Format=DXGI_FORMAT_R32_FLOAT;hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&depth));if(FAILED(hr))return hr;
        slot.motion=motion;slot.depth=depth;slot.state=D3D12_RESOURCE_STATE_COMMON;return S_OK;
    }
};
TemporalPipeline::TemporalPipeline():impl_(new Impl){}
TemporalPipeline::~TemporalPipeline(){if(FAILED(Shutdown()))(void)impl_.release();}
TemporalGraphStats TemporalPipeline::Stats()const{return impl_->graph.Stats();}
HRESULT TemporalPipeline::Initialize(ID3D12Device* device,ID3D12Fence* fence,DXGI_FORMAT format,
    const void* tv,size_t tvb,const void* tp,size_t tpb,const void* cv,size_t cvb,const void* cp,size_t cpb){
    if(!device||!fence||impl_->device)return E_INVALIDARG;auto p=std::make_unique<Impl>();
    auto hr=p->graph.Initialize(device,fence,format,tv,tvb,tp,tpb);if(FAILED(hr))return hr;
    hr=p->camera.Initialize(device,cv,cvb,cp,cpb);if(FAILED(hr))return hr;
    for(auto& slot:p->slots){D3D12_DESCRIPTOR_HEAP_DESC h{};h.NumDescriptors=2;h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;h.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr=device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&slot.srv));if(FAILED(hr))return hr;
        h.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;h.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;hr=device->CreateDescriptorHeap(&h,IID_PPV_ARGS(&slot.rtv));if(FAILED(hr))return hr;
    }
    p->device=device;p->fence=fence;p->owner=GetCurrentThreadId();impl_.swap(p);return S_OK;
}
HRESULT TemporalPipeline::Record(ID3D12GraphicsCommandList* list,UINT index,const TemporalPipelineInput& in){
    auto& p=*impl_;if(!p.device||!list||index>=p.slots.size()||p.pending)return E_INVALIDARG;if(GetCurrentThreadId()!=p.owner)return RPC_E_WRONG_THREAD;
    Temporal::HistoryState gate;if(!gate.Prepare(in.frame,in.quality).dispatch){p.graph.Abort();return S_FALSE;}
    if(!Temporal::ValidCamera(in.camera)||in.camera.width!=in.frame.width||in.camera.height!=in.frame.height||in.camera.jitter_x!=in.frame.jitter_x||in.camera.jitter_y!=in.frame.jitter_y)return E_INVALIDARG;
    const auto completed=p.fence->GetCompletedValue();if(completed==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
    auto& slot=p.slots[index];if(slot.done>completed)return S_FALSE;
    ComPtr<ID3D12Device> ld;auto hr=list->GetDevice(IID_PPV_ARGS(&ld));if(FAILED(hr))return hr;if(ld.Get()!=p.device.Get())return E_INVALIDARG;
    ID3D12Resource* inputs[]{in.normalized_depth,in.static_confidence};
    for(UINT i=0;i<2;++i){if(!inputs[i]||inputs[i]==in.output)return E_INVALIDARG;ComPtr<ID3D12Device> device;hr=inputs[i]->GetDevice(IID_PPV_ARGS(&device));if(FAILED(hr))return hr;
        const auto d=inputs[i]->GetDesc();if(device.Get()!=p.device.Get()||!PipelineImage(d,in.frame.width,in.frame.height)||(d.Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)
            ||(i==0?d.Format!=DXGI_FORMAT_R32_FLOAT:d.Format!=DXGI_FORMAT_R32_FLOAT&&d.Format!=DXGI_FORMAT_R8_UNORM))return E_INVALIDARG;
    }
    hr=p.Images(slot,in.frame.width,in.frame.height);if(FAILED(hr))return hr;
    slot.source=in.normalized_depth;slot.confidence=in.static_confidence;
    const auto srv=slot.srv->GetCPUDescriptorHandleForHeapStart();const UINT ss=p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    for(UINT i=0;i<2;++i){D3D12_SHADER_RESOURCE_VIEW_DESC d{};d.Format=inputs[i]->GetDesc().Format;d.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;d.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;d.Texture2D.MipLevels=1;
        auto h=srv;h.ptr+=static_cast<SIZE_T>(i)*ss;p.device->CreateShaderResourceView(inputs[i],&d,h);
    }
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,2> targets{};const auto rtv=slot.rtv->GetCPUDescriptorHandleForHeapStart();const UINT rs=p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ID3D12Resource* outputs[]{slot.motion.Get(),slot.depth.Get()};
    for(UINT i=0;i<2;++i){targets[i]=rtv;targets[i].ptr+=static_cast<SIZE_T>(i)*rs;p.device->CreateRenderTargetView(outputs[i],nullptr,targets[i]);PipelineBarrier(list,outputs[i],slot.state,D3D12_RESOURCE_STATE_RENDER_TARGET);}
    hr=p.camera.Record(list,slot.srv.Get(),targets,in.camera);if(FAILED(hr)){p.graph.Abort();return hr;}
    for(auto* resource:outputs)PipelineBarrier(list,resource,D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    TemporalGraphInput graph;graph.color=in.color;graph.motion=slot.motion.Get();graph.depth=slot.depth.Get();graph.reactive=in.reactive;graph.output=in.output;graph.frame=in.frame;graph.quality=in.quality;
    hr=p.graph.Record(list,index,graph);if(hr!=S_OK){p.graph.Abort();return hr;}
    p.pending_slot=index;p.pending=true;return S_OK;
}
bool TemporalPipeline::Commit(const Temporal::Frame& frame,UINT64 signal){
    auto& p=*impl_;if(!p.pending||GetCurrentThreadId()!=p.owner||!p.graph.Commit(frame,signal))return false;
    auto& slot=p.slots[p.pending_slot];slot.done=signal;slot.state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;p.pending=false;return true;
}
void TemporalPipeline::Abort(){auto& p=*impl_;if(p.device&&GetCurrentThreadId()!=p.owner)return;p.graph.Abort();p.pending=false;}
HRESULT TemporalPipeline::Shutdown(){
    auto& p=*impl_;if(!p.device)return S_OK;if(GetCurrentThreadId()!=p.owner)return RPC_E_WRONG_THREAD;if(p.pending)return E_PENDING;
    const auto completed=p.fence->GetCompletedValue();for(const auto& slot:p.slots)if(slot.done>completed)return DXGI_ERROR_WAS_STILL_DRAWING;
    const auto hr=p.graph.Shutdown();if(FAILED(hr))return hr;auto empty=std::make_unique<Impl>();impl_.swap(empty);return S_OK;
}
}
