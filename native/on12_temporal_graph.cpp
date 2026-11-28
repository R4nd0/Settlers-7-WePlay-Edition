#include "on12_temporal_graph.h"
#include <vector>

namespace On12Graphics {
using Microsoft::WRL::ComPtr;
namespace {
void Transition(ID3D12GraphicsCommandList* list,ID3D12Resource* resource,D3D12_RESOURCE_STATES before,D3D12_RESOURCE_STATES after){
    if(before==after)return;D3D12_RESOURCE_BARRIER b{};b.Type=D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    b.Transition={resource,D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES,before,after};list->ResourceBarrier(1,&b);
}
bool Image(const D3D12_RESOURCE_DESC& d,UINT w,UINT h){return d.Dimension==D3D12_RESOURCE_DIMENSION_TEXTURE2D&&d.Width==w&&d.Height==h&&d.DepthOrArraySize==1&&d.MipLevels==1&&d.SampleDesc.Count==1;}
bool Scalar(DXGI_FORMAT f){return f==DXGI_FORMAT_R32_FLOAT||f==DXGI_FORMAT_R8_UNORM;}
}
struct TemporalGraph::Impl {
    struct History {ComPtr<ID3D12Resource> color,depth;D3D12_RESOURCE_STATES state=D3D12_RESOURCE_STATE_COMMON;};
    struct Slot {
        ComPtr<ID3D12DescriptorHeap> srv,rtv;
        std::array<ComPtr<ID3D12Resource>,9> retained{};
        UINT64 done=0;
    };
    ComPtr<ID3D12Device> device;ComPtr<ID3D12Fence> fence;TemporalPass pass;
    std::array<History,2> histories{};std::array<Slot,3> slots{};
    Temporal::HistoryState state;TemporalGraphStats stats;
    DXGI_FORMAT format=DXGI_FORMAT_UNKNOWN;DWORD owner=0;UINT width=0,height=0,read_index=0,pending_slot=0,pending_write=0;
    UINT64 last_signal=0;bool pending=false,pending_reuse=false;Temporal::Frame pending_frame;
    HRESULT Histories(UINT w,UINT h){
        if(width==w&&height==h&&histories[0].color&&histories[1].color)return S_OK;
        std::array<History,2> next;
        D3D12_HEAP_PROPERTIES heap{};heap.Type=D3D12_HEAP_TYPE_DEFAULT;heap.CreationNodeMask=heap.VisibleNodeMask=1;
        D3D12_RESOURCE_DESC d{};d.Dimension=D3D12_RESOURCE_DIMENSION_TEXTURE2D;d.Width=w;d.Height=h;d.DepthOrArraySize=1;d.MipLevels=1;d.SampleDesc.Count=1;d.Flags=D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        for(auto& item:next){d.Format=DXGI_FORMAT_R16G16B16A16_FLOAT;
            auto hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&item.color));if(FAILED(hr))return hr;
            d.Format=DXGI_FORMAT_R32_FLOAT;hr=device->CreateCommittedResource(&heap,D3D12_HEAP_FLAG_NONE,&d,D3D12_RESOURCE_STATE_COMMON,nullptr,IID_PPV_ARGS(&item.depth));if(FAILED(hr))return hr;
        }
        // Retired resources remain referenced by every in-flight slot that used
        // them. Resizing never frees data that a submitted list can still read.
        histories=std::move(next);width=w;height=h;read_index=0;return S_OK;
    }
};
TemporalGraph::TemporalGraph():impl_(new Impl){}
TemporalGraph::~TemporalGraph(){if(FAILED(Shutdown()))(void)impl_.release();}
TemporalGraphStats TemporalGraph::Stats()const{return impl_->stats;}
HRESULT TemporalGraph::Initialize(ID3D12Device* device,ID3D12Fence* fence,DXGI_FORMAT format,const void* vs,size_t vb,const void* ps,size_t pb){
    if(!device||!fence||impl_->device)return E_INVALIDARG;
    ComPtr<ID3D12Device> fd;HRESULT hr=fence->GetDevice(IID_PPV_ARGS(&fd));if(FAILED(hr))return hr;if(fd.Get()!=device)return E_INVALIDARG;
    auto p=std::make_unique<Impl>();hr=p->pass.Initialize(device,format,vs,vb,ps,pb);if(FAILED(hr))return hr;
    for(auto& slot:p->slots){D3D12_DESCRIPTOR_HEAP_DESC heap{};heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV;heap.NumDescriptors=6;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr=device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&slot.srv));if(FAILED(hr))return hr;
        heap.Type=D3D12_DESCRIPTOR_HEAP_TYPE_RTV;heap.NumDescriptors=3;heap.Flags=D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr=device->CreateDescriptorHeap(&heap,IID_PPV_ARGS(&slot.rtv));if(FAILED(hr))return hr;
    }
    p->device=device;p->fence=fence;p->format=format;p->owner=GetCurrentThreadId();impl_.swap(p);return S_OK;
}
HRESULT TemporalGraph::Record(ID3D12GraphicsCommandList* list,UINT index,const TemporalGraphInput& in){
    auto& p=*impl_;if(!p.device||!list||index>=p.slots.size()||p.pending)return E_INVALIDARG;
    if(p.owner!=GetCurrentThreadId())return RPC_E_WRONG_THREAD;
    const auto complete=p.fence->GetCompletedValue();if(complete==UINT64_MAX)return DXGI_ERROR_DEVICE_REMOVED;
    auto& slot=p.slots[index];if(slot.done>complete){++p.stats.busy;return S_FALSE;}
    const auto decision=p.state.Prepare(in.frame,in.quality);if(!decision.dispatch)return S_FALSE;
    auto fail=[&](HRESULT hr){p.state.Abort();return hr;};
    ComPtr<ID3D12Device> ld;HRESULT hr=list->GetDevice(IID_PPV_ARGS(&ld));if(FAILED(hr))return fail(hr);if(ld.Get()!=p.device.Get())return fail(E_INVALIDARG);
    ID3D12Resource* resources[]{in.color,in.motion,in.depth,in.reactive,in.output};
    std::array<D3D12_RESOURCE_DESC,5> desc{};
    for(UINT i=0;i<5;++i){if(!resources[i])return fail(E_POINTER);ComPtr<ID3D12Device> owner;hr=resources[i]->GetDevice(IID_PPV_ARGS(&owner));if(FAILED(hr))return fail(hr);
        if(owner.Get()!=p.device.Get())return fail(E_INVALIDARG);desc[i]=resources[i]->GetDesc();
        if(!Image(desc[i],in.frame.width,in.frame.height))return fail(E_INVALIDARG);
        if(i<4&&(resources[i]==in.output||(desc[i].Flags&D3D12_RESOURCE_FLAG_DENY_SHADER_RESOURCE)))return fail(E_INVALIDARG);
    }
    if(desc[0].Format!=p.format||desc[4].Format!=p.format||desc[1].Format!=DXGI_FORMAT_R32G32B32A32_FLOAT||desc[2].Format!=DXGI_FORMAT_R32_FLOAT
        ||!Scalar(desc[3].Format)||!(desc[4].Flags&D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET))return fail(E_INVALIDARG);
    hr=p.Histories(in.frame.width,in.frame.height);if(FAILED(hr))return fail(hr);
    const UINT write=1-p.read_index;auto& previous=p.histories[p.read_index];auto& next=p.histories[write];
    slot.retained={in.color,in.motion,in.depth,in.reactive,in.output,previous.color,previous.depth,next.color,next.depth};
    const auto srv_start=slot.srv->GetCPUDescriptorHandleForHeapStart();const UINT srv_stride=p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV);
    ID3D12Resource* samples[]{in.color,previous.color.Get(),in.motion,in.depth,previous.depth.Get(),in.reactive};
    for(UINT i=0;i<6;++i){D3D12_SHADER_RESOURCE_VIEW_DESC view{};view.Format=samples[i]->GetDesc().Format;view.ViewDimension=D3D12_SRV_DIMENSION_TEXTURE2D;view.Shader4ComponentMapping=D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING;view.Texture2D.MipLevels=1;
        auto handle=srv_start;handle.ptr+=static_cast<SIZE_T>(i)*srv_stride;p.device->CreateShaderResourceView(samples[i],&view,handle);}
    std::array<D3D12_CPU_DESCRIPTOR_HANDLE,3> targets{};const auto rt_start=slot.rtv->GetCPUDescriptorHandleForHeapStart();const UINT rt_stride=p.device->GetDescriptorHandleIncrementSize(D3D12_DESCRIPTOR_HEAP_TYPE_RTV);
    ID3D12Resource* outputs[]{in.output,next.color.Get(),next.depth.Get()};
    for(UINT i=0;i<3;++i){targets[i]=rt_start;targets[i].ptr+=static_cast<SIZE_T>(i)*rt_stride;p.device->CreateRenderTargetView(outputs[i],nullptr,targets[i]);}
    Transition(list,previous.color.Get(),previous.state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);Transition(list,previous.depth.Get(),previous.state,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    Transition(list,next.color.Get(),next.state,D3D12_RESOURCE_STATE_RENDER_TARGET);Transition(list,next.depth.Get(),next.state,D3D12_RESOURCE_STATE_RENDER_TARGET);
    hr=p.pass.Record(list,slot.srv.Get(),targets,decision.parameters);if(FAILED(hr))return fail(hr);
    Transition(list,next.color.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);Transition(list,next.depth.Get(),D3D12_RESOURCE_STATE_RENDER_TARGET,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    p.pending=true;p.pending_slot=index;p.pending_write=write;p.pending_frame=in.frame;p.pending_reuse=decision.reuse;return S_OK;
}
bool TemporalGraph::Commit(const Temporal::Frame& frame,UINT64 signal){
    auto& p=*impl_;if(!p.device||GetCurrentThreadId()!=p.owner||!p.pending||!signal||signal<=p.last_signal||!p.state.Commit(frame))return false;
    p.last_signal=signal;p.slots[p.pending_slot].done=signal;p.read_index=p.pending_write;
    for(auto& item:p.histories)item.state=D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE;
    ++p.stats.committed;if(p.pending_reuse)++p.stats.reused;else ++p.stats.resets;p.pending=false;return true;
}
void TemporalGraph::Abort(){auto& p=*impl_;if(p.device&&GetCurrentThreadId()!=p.owner)return;p.state.Abort();if(p.pending){p.pending=false;++p.stats.aborted;}}
HRESULT TemporalGraph::Shutdown(){
    if(!impl_->device)return S_OK;auto& p=*impl_;if(GetCurrentThreadId()!=p.owner)return RPC_E_WRONG_THREAD;
    if(p.pending)return E_PENDING;const auto complete=p.fence->GetCompletedValue();for(const auto& slot:p.slots)if(slot.done>complete)return DXGI_ERROR_WAS_STILL_DRAWING;
    auto empty=std::make_unique<Impl>();impl_.swap(empty);return S_OK;
}
}
