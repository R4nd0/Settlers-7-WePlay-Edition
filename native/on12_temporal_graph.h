#pragma once
#include "on12_temporal_pass.h"
#include <memory>

namespace On12Graphics {
struct TemporalGraphInput {
    // Inputs must be in PIXEL_SHADER_RESOURCE; output in RENDER_TARGET.
    // Motion is unjittered current->previous pixels, previous-view Z, confidence.
    ID3D12Resource* color=nullptr;ID3D12Resource* motion=nullptr;
    ID3D12Resource* depth=nullptr;ID3D12Resource* reactive=nullptr;ID3D12Resource* output=nullptr;
    Temporal::Frame frame;Temporal::Parameters quality;
};
struct TemporalGraphStats {std::uint64_t committed=0,reused=0,resets=0,aborted=0,busy=0;};
// Records onto the host's list/queue. Commit only after successful execution and
// signal on the supplied fence. Abort is for an unsubmitted list only.
class TemporalGraph {
public:
    TemporalGraph();~TemporalGraph();
    TemporalGraph(const TemporalGraph&)=delete;TemporalGraph& operator=(const TemporalGraph&)=delete;
    HRESULT Initialize(ID3D12Device*,ID3D12Fence*,DXGI_FORMAT,const void* vs,size_t vs_bytes,const void* ps,size_t ps_bytes);
    HRESULT Record(ID3D12GraphicsCommandList*,UINT slot,const TemporalGraphInput&);
    bool Commit(const Temporal::Frame&,UINT64 signaled_value);
    void Abort();
    HRESULT Shutdown(); // Nonblocking; requires retired slots and no pending list.
    TemporalGraphStats Stats()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
