#include "on12_frame_context.h"
#include "on12_spatial_pass.h"
#include "on12_temporal_pipeline.h"
#include <d3d9on12.h>
#include <array>
#include <vector>
#include <cmath>
#include <cstring>
#include <intrin.h>
#include <float.h>

namespace On12Graphics {
using Microsoft::WRL::ComPtr;
namespace {
struct ControlState {
    alignas(16) unsigned char saved[512]; DWORD error = GetLastError();
    ControlState() { _fxsave(saved); unsigned ignored = 0; _controlfp_s(&ignored, _MCW_EM | _RC_NEAR, _MCW_EM | _MCW_RC); }
    ~ControlState() { _fxrstor(saved); SetLastError(error); }
};
bool Format(DXGI_FORMAT f) { return f == DXGI_FORMAT_R8G8B8A8_UNORM || f == DXGI_FORMAT_B8G8R8A8_UNORM || f == DXGI_FORMAT_B8G8R8X8_UNORM; }
void* InterfaceModule(IUnknown* object, size_t method) {
    if (!object) return nullptr;
    MEMORY_BASIC_INFORMATION memory{};
    if (VirtualQuery(object, &memory, sizeof(memory)) != sizeof(memory) || memory.State != MEM_COMMIT
        || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return nullptr;
    const auto table = *reinterpret_cast<void***>(object);
    if (VirtualQuery(table, &memory, sizeof(memory)) != sizeof(memory)
        || memory.State != MEM_COMMIT || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return nullptr;
    if (reinterpret_cast<uintptr_t>(table) + (method + 1) * sizeof(void*) > reinterpret_cast<uintptr_t>(memory.BaseAddress) + memory.RegionSize) return nullptr;
    if (VirtualQuery(table[method], &memory, sizeof(memory)) != sizeof(memory) || memory.Type != MEM_IMAGE
        || memory.State != MEM_COMMIT || !(memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
        || (memory.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return nullptr;
    return memory.AllocationBase;
}
bool Texture(const D3D12_RESOURCE_DESC& d) {
    return d.Dimension == D3D12_RESOURCE_DIMENSION_TEXTURE2D && d.Width && d.Width <= 16384 && d.Height && d.Height <= 16384
        && d.DepthOrArraySize == 1 && d.MipLevels == 1 && d.SampleDesc.Count == 1 && Format(d.Format);
}
void Transition(ID3D12GraphicsCommandList* list, ID3D12Resource* resource, D3D12_RESOURCE_STATES before, D3D12_RESOURCE_STATES after) {
    D3D12_RESOURCE_BARRIER barrier{}; barrier.Type = D3D12_RESOURCE_BARRIER_TYPE_TRANSITION;
    barrier.Transition = {resource, D3D12_RESOURCE_BARRIER_ALL_SUBRESOURCES, before, after}; list->ResourceBarrier(1, &barrier);
}
}
struct FrameContext::Impl {
    struct Frame {
        struct Extra {ComPtr<IDirect3DSurface9> surface;ComPtr<ID3D12Resource> resource;bool out=false;};
        ComPtr<ID3D12CommandAllocator> allocator;
        ComPtr<ID3D12GraphicsCommandList> list;
        ComPtr<ID3D12Resource> input, antialiased, source12, target12;
        ComPtr<IDirect3DSurface9> source9, target9;
        ComPtr<ID3D12DescriptorHeap> srv, rtv, aa_srv, aa_rtv;
        UINT64 done = 0; bool source_out = false, target_out = false, submitted = false, signaled = false;
        std::array<Extra,4> extra;
    };
    DWORD owner = 0; UINT next = 0; UINT64 serial = 0; bool poisoned = false;
    ComPtr<IDirect3DDevice9> original;
    ComPtr<IDirect3DDevice9> native;
    void* native_module = nullptr;
    ComPtr<IDirect3DDevice9On12> bridge;
    ComPtr<ID3D12Device> device;
    ComPtr<ID3D12CommandQueue> queue;
    ComPtr<ID3D12Fence> fence;
    std::array<Frame, 3> frames;
    std::array<SpatialPass, 3> passes;
    std::array<bool, 3> pass_ready{};
    std::vector<unsigned char> vertex, pixel;
    std::array<std::vector<unsigned char>,4> temporal_shaders;
    std::array<std::unique_ptr<TemporalPipeline>,3> temporal;
    FrameStats stats;
    bool Safe() const { for (const auto& f : frames) {if (f.source_out || f.target_out) return false;for(const auto& e:f.extra)if(e.out)return false;} return true; }
    HRESULT Return(Frame& f) {
        HRESULT first = S_OK;
        UINT count = f.submitted && f.signaled ? 1u : 0u;
        ID3D12Fence* signal = fence.Get();
        if (f.submitted && !f.signaled && SUCCEEDED(device->GetDeviceRemovedReason())) return E_UNEXPECTED;
        auto give_back=[&](IDirect3DSurface9* surface,bool& out){
            if (!out) return;
            HRESULT hr = bridge->ReturnUnderlyingResource(surface, count, count ? &f.done : nullptr, count ? &signal : nullptr);
            if (SUCCEEDED(hr)) { out = false; ++stats.returns; }
            else { ++stats.failed_returns; if (SUCCEEDED(first)) first = hr; }
        };
        give_back(f.target9.Get(),f.target_out);give_back(f.source9.Get(),f.source_out);
        for(auto& e:f.extra)give_back(e.surface.Get(),e.out);
        return first;
    }
};

FrameContext::FrameContext() : impl_(new Impl) {}
FrameContext::~FrameContext() {
    // A failed checkout must never silently release live GPU resources. The
    // host observes SafeForD3D9 and handles device failure before presenting.
    if (impl_ && FAILED(Drain(5000))) (void)impl_.release();
}
bool FrameContext::SafeForD3D9() const { return impl_->Safe(); }
FrameStats FrameContext::Stats() const { return impl_->stats; }
IDirect3DDevice9* FrameContext::NativeDevice() const { return impl_->owner == GetCurrentThreadId() ? impl_->native.Get() : nullptr; }

HRESULT FrameContext::Initialize(IDirect3DDevice9* original, const void* vs, size_t vb, const void* ps, size_t pb) {
    ControlState control;
    if (!original || !vs || !ps || !vb || !pb || vb > 1024 * 1024 || pb > 1024 * 1024 || impl_->original) return E_INVALIDARG;
    auto prepared = std::unique_ptr<Impl>(new (std::nothrow) Impl); if (!prepared) return E_OUTOFMEMORY;
    HRESULT hr = original->QueryInterface(IID_PPV_ARGS(&prepared->bridge)); if (FAILED(hr)) return hr;
    // Query back through the On12 identity: the host may have supplied a
    // dxwrapper device, but interop accepts Microsoft's native resource objects.
    hr = prepared->bridge->QueryInterface(IID_PPV_ARGS(&prepared->native)); if (FAILED(hr)) return hr;
    ComPtr<IUnknown> bridge_identity, native_identity;
    hr = prepared->bridge.As(&bridge_identity); if (FAILED(hr)) return hr;
    hr = prepared->native.As(&native_identity); if (FAILED(hr)) return hr;
    if (bridge_identity.Get() != native_identity.Get()) return E_NOINTERFACE;
    prepared->native_module = InterfaceModule(prepared->native.Get(), 9); if (!prepared->native_module) return E_NOINTERFACE;
    hr = prepared->bridge->GetD3D12Device(IID_PPV_ARGS(&prepared->device)); if (FAILED(hr)) return hr;
    D3D12_COMMAND_QUEUE_DESC queue{}; queue.Type = D3D12_COMMAND_LIST_TYPE_DIRECT;
    hr = prepared->device->CreateCommandQueue(&queue, IID_PPV_ARGS(&prepared->queue)); if (FAILED(hr)) return hr;
    hr = prepared->device->CreateFence(0, D3D12_FENCE_FLAG_NONE, IID_PPV_ARGS(&prepared->fence)); if (FAILED(hr)) return hr;
    for (auto& f : prepared->frames) {
        hr = prepared->device->CreateCommandAllocator(D3D12_COMMAND_LIST_TYPE_DIRECT, IID_PPV_ARGS(&f.allocator)); if (FAILED(hr)) return hr;
        hr = prepared->device->CreateCommandList(0, D3D12_COMMAND_LIST_TYPE_DIRECT, f.allocator.Get(), nullptr, IID_PPV_ARGS(&f.list)); if (FAILED(hr)) return hr;
        hr = f.list->Close(); if (FAILED(hr)) return hr;
        D3D12_DESCRIPTOR_HEAP_DESC heap{}; heap.NumDescriptors = 1; heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = prepared->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&f.srv)); if (FAILED(hr)) return hr;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_RTV; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_NONE;
        hr = prepared->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&f.rtv)); if (FAILED(hr)) return hr;
        hr = prepared->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&f.aa_rtv)); if (FAILED(hr)) return hr;
        heap.Type = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV; heap.Flags = D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE;
        hr = prepared->device->CreateDescriptorHeap(&heap, IID_PPV_ARGS(&f.aa_srv)); if (FAILED(hr)) return hr;
    }
    try {
        prepared->vertex.assign(static_cast<const unsigned char*>(vs), static_cast<const unsigned char*>(vs) + vb);
        prepared->pixel.assign(static_cast<const unsigned char*>(ps), static_cast<const unsigned char*>(ps) + pb);
    } catch (...) { return E_OUTOFMEMORY; }
    prepared->original = original; prepared->owner = GetCurrentThreadId(); impl_.swap(prepared); return S_OK;
}

HRESULT FrameContext::PrepareTemporal(const void* vs,size_t vb,const void* ps,size_t pb,const void* cv,size_t cb,const void* cp,size_t cpb){
    ControlState control;auto& p=*impl_;if(!p.original||p.poisoned||!p.Safe()||!p.temporal_shaders[0].empty())return E_UNEXPECTED;
    if(GetCurrentThreadId()!=p.owner)return RPC_E_WRONG_THREAD;
    const void* shaders[]{vs,ps,cv,cp};const size_t sizes[]{vb,pb,cb,cpb};std::array<std::vector<unsigned char>,4> next;
    for(UINT i=0;i<4;++i){if(!shaders[i]||!sizes[i]||sizes[i]>1024*1024)return E_INVALIDARG;
        try{const auto* data=static_cast<const unsigned char*>(shaders[i]);next[i].assign(data,data+sizes[i]);}catch(...){return E_OUTOFMEMORY;}}
    p.temporal_shaders=std::move(next);return S_OK;
}
HRESULT FrameContext::Render(IDirect3DSurface9* source,IDirect3DSurface9* target,const SpatialSettings& config){return RenderScene(source,target,config,nullptr);}
HRESULT FrameContext::RenderTemporal(IDirect3DSurface9* source,IDirect3DSurface9* target,const SpatialSettings& config,const TemporalSceneSettings& temporal){return RenderScene(source,target,config,&temporal);}
HRESULT FrameContext::RenderScene(IDirect3DSurface9* source, IDirect3DSurface9* target, const SpatialSettings& config,const TemporalSceneSettings* temporal) {
    ControlState control; auto& p = *impl_;
    if (config.size != sizeof(config) || config.version != 1 || config.enabled > 1 || !std::isfinite(config.antialiasing)
        || config.antialiasing < 0 || config.antialiasing > 1) return E_INVALIDARG;
    if (p.owner && GetCurrentThreadId() != p.owner) return RPC_E_WRONG_THREAD;
    if (!config.enabled) { ++p.stats.disabled; return S_FALSE; }
    if (!p.original || !source || !target || p.poisoned || !p.Safe()) return E_UNEXPECTED;
    if(temporal){
        Temporal::HistoryState gate;
        if(!gate.Prepare(temporal->frame,temporal->quality).dispatch){for(auto& pipeline:p.temporal)if(pipeline)pipeline->Abort();++p.stats.temporal_rejected;return S_FALSE;}
        if(p.temporal_shaders[0].empty()||!Temporal::ValidCamera(temporal->camera)||temporal->camera.width!=temporal->frame.width||temporal->camera.height!=temporal->frame.height
            ||temporal->camera.jitter_x!=temporal->frame.jitter_x||temporal->camera.jitter_y!=temporal->frame.jitter_y)return E_INVALIDARG;
        if(temporal->geometry_motion&&temporal->geometry_motion_frame!=temporal->frame.sequence)return E_INVALIDARG;
    }
    if (InterfaceModule(source, 3) != p.native_module || InterfaceModule(target, 3) != p.native_module) return E_INVALIDARG;
    ComPtr<IDirect3DDevice9> sd, td; HRESULT hr = source->GetDevice(&sd); if (FAILED(hr)) return hr;
    hr = target->GetDevice(&td); if (FAILED(hr)) return hr;
    if (sd.Get() != p.native.Get() || td.Get() != p.native.Get()) return E_INVALIDARG;
    D3DSURFACE_DESC source_desc{}, target_desc{};
    hr = source->GetDesc(&source_desc); if (FAILED(hr)) return hr;
    hr = target->GetDesc(&target_desc); if (FAILED(hr)) return hr;
    auto supported9 = [](const D3DSURFACE_DESC& d) {
        return d.Pool == D3DPOOL_DEFAULT && d.MultiSampleType == D3DMULTISAMPLE_NONE
            && d.Width && d.Width <= 16384 && d.Height && d.Height <= 16384
            && (d.Format == D3DFMT_A8R8G8B8 || d.Format == D3DFMT_X8R8G8B8 || d.Format == D3DFMT_A8B8G8R8);
    };
    if (!supported9(source_desc) || !supported9(target_desc) || !(target_desc.Usage & D3DUSAGE_RENDERTARGET)) {
        ++p.stats.unsupported; return DXGI_ERROR_UNSUPPORTED;
    }
    std::array<IDirect3DSurface9*,4> extra{};
    if(temporal){
        if(temporal->frame.width!=source_desc.Width||temporal->frame.height!=source_desc.Height)return E_INVALIDARG;
        extra={temporal->normalized_depth,temporal->static_confidence,temporal->reactive,temporal->geometry_motion};
        for(UINT i=0;i<4;++i){auto* surface=extra[i];if(i==3&&!surface)continue;if(!surface||surface==source||surface==target||InterfaceModule(surface,3)!=p.native_module)return E_INVALIDARG;
            for(UINT j=0;j<i;++j)if(surface==extra[j])return E_INVALIDARG;
            D3DSURFACE_DESC d{};ComPtr<IDirect3DDevice9> owner;
            hr=surface->GetDevice(&owner);if(FAILED(hr))return hr;if(owner.Get()!=p.native.Get())return E_INVALIDARG;
            hr=surface->GetDesc(&d);if(FAILED(hr))return hr;
            if(d.Pool!=D3DPOOL_DEFAULT||d.MultiSampleType!=D3DMULTISAMPLE_NONE||d.Width!=source_desc.Width||d.Height!=source_desc.Height
                ||(i==3?d.Format!=D3DFMT_A32B32G32R32F:i==0?d.Format!=D3DFMT_R32F:d.Format!=D3DFMT_R32F&&d.Format!=D3DFMT_L8))return E_INVALIDARG;
        }
    }
    ComPtr<IUnknown> si, ti; hr = source->QueryInterface(IID_PPV_ARGS(&si)); if (FAILED(hr)) return hr;
    hr = target->QueryInterface(IID_PPV_ARGS(&ti)); if (FAILED(hr)) return hr;
    const bool same = si.Get() == ti.Get();
    si.Reset(); ti.Reset(); sd.Reset(); td.Reset();
    auto& f = p.frames[p.next]; const UINT64 completed = p.fence->GetCompletedValue();
    if (completed == UINT64_MAX) return DXGI_ERROR_DEVICE_REMOVED;
    if (f.done > completed) { ++p.stats.busy; return S_FALSE; }
    f.source12.Reset(); f.target12.Reset(); f.source9 = source; f.target9 = target;
    f.source_out = f.target_out = f.submitted = f.signaled = false;
    for(UINT i=0;i<4;++i){f.extra[i].resource.Reset();f.extra[i].surface=extra[i];f.extra[i].out=false;}
    hr = f.allocator->Reset(); if (FAILED(hr)) return hr;
    hr = f.list->Reset(f.allocator.Get(), nullptr); if (FAILED(hr)) return hr;
    hr = p.bridge->UnwrapUnderlyingResource(source, p.queue.Get(), IID_PPV_ARGS(&f.source12));
    if (FAILED(hr)) { f.list->Close(); return hr; }
    f.source_out = true; ++p.stats.checkouts;
    TemporalPipeline* processor=nullptr;
    auto finish_error = [&](HRESULT error) {
        if(processor)processor->Abort();
        f.list->Close(); HRESULT returned = p.Return(f); if (FAILED(returned)) { p.poisoned = true; return returned; } return error;
    };
    if (same) f.target12 = f.source12;
    else {
        hr = p.bridge->UnwrapUnderlyingResource(target, p.queue.Get(), IID_PPV_ARGS(&f.target12)); if (FAILED(hr)) return finish_error(hr);
        f.target_out = true; ++p.stats.checkouts;
        if (f.target12.Get() == f.source12.Get()) return finish_error(E_INVALIDARG);
    }
    if(temporal)for(auto& e:f.extra){
        if(!e.surface)continue;
        hr=p.bridge->UnwrapUnderlyingResource(e.surface.Get(),p.queue.Get(),IID_PPV_ARGS(&e.resource));if(FAILED(hr))return finish_error(hr);
        e.out=true;++p.stats.checkouts;
        if(e.resource.Get()==f.source12.Get()||e.resource.Get()==f.target12.Get())return finish_error(E_INVALIDARG);
        for(const auto& previous:f.extra){if(&previous==&e)break;if(previous.resource.Get()==e.resource.Get())return finish_error(E_INVALIDARG);}
    }
    const auto input_desc = f.source12->GetDesc(), output_desc = f.target12->GetDesc();
    if (!Texture(input_desc) || !Texture(output_desc) || !(output_desc.Flags & D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET)) {
        ++p.stats.unsupported; return finish_error(DXGI_ERROR_UNSUPPORTED);
    }
    ComPtr<ID3D12Device> source_device, target_device;
    hr = f.source12->GetDevice(IID_PPV_ARGS(&source_device)); if (FAILED(hr)) return finish_error(hr);
    hr = f.target12->GetDevice(IID_PPV_ARGS(&target_device)); if (FAILED(hr)) return finish_error(hr);
    if (source_device.Get() != p.device.Get() || target_device.Get() != p.device.Get()) return finish_error(E_INVALIDARG);
    const auto index = [](DXGI_FORMAT format) { return format == DXGI_FORMAT_R8G8B8A8_UNORM ? 0u : format == DXGI_FORMAT_B8G8R8A8_UNORM ? 1u : 2u; };
    const unsigned format_index = index(output_desc.Format), source_index = index(input_desc.Format);
    const bool separate_aa = temporal || (config.antialiasing > 0 && (input_desc.Width != output_desc.Width || input_desc.Height != output_desc.Height));
    auto prepare_pass = [&](DXGI_FORMAT format) {
        const auto i = index(format); if (p.pass_ready[i]) return S_OK;
        const HRESULT result = p.passes[i].Initialize(p.device.Get(), format, p.vertex.data(), p.vertex.size(), p.pixel.data(), p.pixel.size());
        if (SUCCEEDED(result)) p.pass_ready[i] = true; return result;
    };
    hr = prepare_pass(output_desc.Format); if (FAILED(hr)) return finish_error(hr);
    if (separate_aa) { hr = prepare_pass(input_desc.Format); if (FAILED(hr)) return finish_error(hr); }
    if(temporal){
        if(!p.temporal[source_index]){
            auto pipeline=std::make_unique<TemporalPipeline>();const auto& s=p.temporal_shaders;
            hr=pipeline->Initialize(p.device.Get(),p.fence.Get(),input_desc.Format,s[0].data(),s[0].size(),s[1].data(),s[1].size(),s[2].data(),s[2].size(),s[3].data(),s[3].size());
            if(FAILED(hr))return finish_error(hr);p.temporal[source_index]=std::move(pipeline);
        }processor=p.temporal[source_index].get();
    }
    auto scratch = input_desc; scratch.Alignment = 0; scratch.Flags = D3D12_RESOURCE_FLAG_NONE; scratch.Layout = D3D12_TEXTURE_LAYOUT_UNKNOWN;
    bool create = !f.input;
    if (f.input) { auto old = f.input->GetDesc(); create = old.Width != scratch.Width || old.Height != scratch.Height || old.Format != scratch.Format; }
    if (create) {
        f.input.Reset(); D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
        hr = p.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &scratch, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&f.input));
        if (FAILED(hr)) return finish_error(hr);
    }
    if (separate_aa) {
        auto aa_desc = scratch; aa_desc.Flags = D3D12_RESOURCE_FLAG_ALLOW_RENDER_TARGET;
        bool create_aa = !f.antialiased;
        if (f.antialiased) { auto old = f.antialiased->GetDesc(); create_aa = old.Width != aa_desc.Width || old.Height != aa_desc.Height || old.Format != aa_desc.Format; }
        if (create_aa) {
            f.antialiased.Reset(); D3D12_HEAP_PROPERTIES heap{}; heap.Type = D3D12_HEAP_TYPE_DEFAULT; heap.CreationNodeMask = heap.VisibleNodeMask = 1;
            hr = p.device->CreateCommittedResource(&heap, D3D12_HEAP_FLAG_NONE, &aa_desc, D3D12_RESOURCE_STATE_COMMON, nullptr, IID_PPV_ARGS(&f.antialiased));
            if (FAILED(hr)) return finish_error(hr);
        }
    }
    D3D12_SHADER_RESOURCE_VIEW_DESC srv{}; srv.Format = input_desc.Format; srv.ViewDimension = D3D12_SRV_DIMENSION_TEXTURE2D;
    srv.Shader4ComponentMapping = D3D12_DEFAULT_SHADER_4_COMPONENT_MAPPING; srv.Texture2D.MipLevels = 1;
    p.device->CreateShaderResourceView(f.input.Get(), &srv, f.srv->GetCPUDescriptorHandleForHeapStart());
    p.device->CreateRenderTargetView(f.target12.Get(), nullptr, f.rtv->GetCPUDescriptorHandleForHeapStart());
    if (separate_aa) {
        p.device->CreateShaderResourceView(f.antialiased.Get(), &srv, f.aa_srv->GetCPUDescriptorHandleForHeapStart());
        p.device->CreateRenderTargetView(f.antialiased.Get(), nullptr, f.aa_rtv->GetCPUDescriptorHandleForHeapStart());
    }
    Transition(f.list.Get(), f.source12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_SOURCE);
    Transition(f.list.Get(), f.input.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_COPY_DEST);
    f.list->CopyResource(f.input.Get(), f.source12.Get());
    Transition(f.list.Get(), f.input.Get(), D3D12_RESOURCE_STATE_COPY_DEST, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    if (separate_aa) {
        // AA directions and contrast tests must use source texels. Mixing a
        // cubic center with bilinear AA taps at output resolution damages text.
        Transition(f.list.Get(), f.antialiased.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
        if(temporal){
            for(auto& e:f.extra)if(e.resource)Transition(f.list.Get(),e.resource.Get(),D3D12_RESOURCE_STATE_COMMON,D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
            TemporalPipelineInput in;in.color=f.input.Get();in.normalized_depth=f.extra[0].resource.Get();in.static_confidence=f.extra[1].resource.Get();in.reactive=f.extra[2].resource.Get();
            in.geometry_motion=f.extra[3].resource.Get();in.geometry_motion_frame=temporal->geometry_motion_frame;
            in.output=f.antialiased.Get();in.camera=temporal->camera;in.frame=temporal->frame;in.quality=temporal->quality;
            hr=processor->Record(f.list.Get(),p.next,in);if(hr!=S_OK)return finish_error(hr);
            for(auto& e:f.extra)if(e.resource)Transition(f.list.Get(),e.resource.Get(),D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE,D3D12_RESOURCE_STATE_COMMON);
        }else{
            hr = p.passes[source_index].Record(f.list.Get(), f.srv.Get(), f.srv->GetGPUDescriptorHandleForHeapStart(), f.aa_rtv->GetCPUDescriptorHandleForHeapStart(),
                static_cast<UINT>(input_desc.Width), input_desc.Height, static_cast<UINT>(input_desc.Width), input_desc.Height, config.antialiasing);
            if (FAILED(hr)) return finish_error(hr);
        }
        Transition(f.list.Get(), f.antialiased.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE);
    }
    Transition(f.list.Get(), f.source12.Get(), D3D12_RESOURCE_STATE_COPY_SOURCE, same ? D3D12_RESOURCE_STATE_RENDER_TARGET : D3D12_RESOURCE_STATE_COMMON);
    if (!same) Transition(f.list.Get(), f.target12.Get(), D3D12_RESOURCE_STATE_COMMON, D3D12_RESOURCE_STATE_RENDER_TARGET);
    auto* sample_heap = separate_aa ? f.aa_srv.Get() : f.srv.Get();
    hr = p.passes[format_index].Record(f.list.Get(), sample_heap, sample_heap->GetGPUDescriptorHandleForHeapStart(), f.rtv->GetCPUDescriptorHandleForHeapStart(),
        static_cast<UINT>(input_desc.Width), input_desc.Height, static_cast<UINT>(output_desc.Width), output_desc.Height, separate_aa ? 0.0f : config.antialiasing);
    if (FAILED(hr)) return finish_error(hr);
    Transition(f.list.Get(), f.target12.Get(), D3D12_RESOURCE_STATE_RENDER_TARGET, D3D12_RESOURCE_STATE_COMMON);
    Transition(f.list.Get(), f.input.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    if (separate_aa) Transition(f.list.Get(), f.antialiased.Get(), D3D12_RESOURCE_STATE_PIXEL_SHADER_RESOURCE, D3D12_RESOURCE_STATE_COMMON);
    hr = f.list->Close(); if (FAILED(hr)) return finish_error(hr);
    ID3D12CommandList* lists[]{f.list.Get()}; p.queue->ExecuteCommandLists(1, lists); f.submitted = true; f.done = ++p.serial;
    hr = p.queue->Signal(p.fence.Get(), f.done);
    if (FAILED(hr)) { p.poisoned = true; return hr; }
    f.signaled = true;
    if(processor&&!processor->Commit(temporal->frame,f.done)){p.poisoned=true;return E_UNEXPECTED;}
    hr = p.Return(f); if (FAILED(hr)) { p.poisoned = true; return hr; }
    if(temporal)++p.stats.temporal_applied;
    ++p.stats.applied; p.next = (p.next + 1) % static_cast<UINT>(p.frames.size()); return S_OK;
}

HRESULT FrameContext::Drain(DWORD timeout_ms) {
    ControlState control; auto& p = *impl_; if (!p.original) return S_OK;
    if (GetCurrentThreadId() != p.owner) return RPC_E_WRONG_THREAD;
    if (!p.Safe()) return E_UNEXPECTED;
    UINT64 maximum = 0; for (const auto& f : p.frames) if (f.signaled && f.done > maximum) maximum = f.done;
    if (p.fence->GetCompletedValue() < maximum) {
        HANDLE event = CreateEventW(nullptr, FALSE, FALSE, nullptr); if (!event) return HRESULT_FROM_WIN32(GetLastError());
        HRESULT hr = p.fence->SetEventOnCompletion(maximum, event);
        DWORD waited = SUCCEEDED(hr) ? WaitForSingleObject(event, timeout_ms) : WAIT_FAILED; CloseHandle(event);
        if (FAILED(hr)) return hr;
        if (waited != WAIT_OBJECT_0) return waited == WAIT_TIMEOUT ? DXGI_ERROR_WAS_STILL_DRAWING : E_FAIL;
    }
    for(auto& pipeline:p.temporal)if(pipeline){const auto hr=pipeline->Shutdown();if(FAILED(hr))return hr;pipeline.reset();}
    for (auto& f : p.frames) { f.source12.Reset(); f.target12.Reset(); f.source9.Reset(); f.target9.Reset(); f.input.Reset(); f.antialiased.Reset(); f.done = 0;for(auto& e:f.extra){e.resource.Reset();e.surface.Reset();} }
    return S_OK;
}
HRESULT FrameContext::Shutdown(DWORD timeout_ms) {
    ControlState control;
    auto empty = std::unique_ptr<Impl>(new (std::nothrow) Impl); if (!empty) return E_OUTOFMEMORY;
    HRESULT hr = Drain(timeout_ms); if (FAILED(hr)) return hr;
    impl_.swap(empty); return S_OK;
}
}
