#pragma once
#include "on12_temporal_graph.h"
#include "on12_temporal_camera_pass.h"

namespace On12Graphics {
struct TemporalPipelineInput {
    // Caller-owned PS_RESOURCE inputs and RENDER_TARGET output, same device.
    ID3D12Resource* color=nullptr;ID3D12Resource* normalized_depth=nullptr;
    // Confidence must be zero on every moving or unclassified surface. Camera
    // motion alone cannot describe animated geometry. Reactive marks unstable
    // shading/transparency separately; this class never manufactures proofs.
    ID3D12Resource* static_confidence=nullptr;ID3D12Resource* reactive=nullptr;
    // Optional qualified per-pixel geometry motion: unjittered previous-current
    // render pixels, previous-view clipW and confidence. It replaces camera-only
    // motion, including on animated surfaces; all frame proofs remain required.
    ID3D12Resource* geometry_motion=nullptr;
    std::uint64_t geometry_motion_frame=0;
    ID3D12Resource* output=nullptr;
    Temporal::CameraParameters camera;Temporal::Frame frame;Temporal::Parameters quality;
};
// Camera motion/depth generation and temporal resolve on one host command list.
// Failure after Record begins requires discarding that unsubmitted list. Commit
// follows execution and a successful signal on the supplied host fence.
class TemporalPipeline {
public:
    TemporalPipeline();~TemporalPipeline();
    TemporalPipeline(const TemporalPipeline&)=delete;TemporalPipeline& operator=(const TemporalPipeline&)=delete;
    HRESULT Initialize(ID3D12Device*,ID3D12Fence*,DXGI_FORMAT,
        const void* temporal_vs,size_t temporal_vs_bytes,const void* temporal_ps,size_t temporal_ps_bytes,
        const void* camera_vs,size_t camera_vs_bytes,const void* camera_ps,size_t camera_ps_bytes);
    HRESULT Record(ID3D12GraphicsCommandList*,UINT slot,const TemporalPipelineInput&);
    bool Commit(const Temporal::Frame&,UINT64 signal);
    void Abort();HRESULT Shutdown();TemporalGraphStats Stats()const;
private:
    struct Impl;std::unique_ptr<Impl> impl_;
};
}
