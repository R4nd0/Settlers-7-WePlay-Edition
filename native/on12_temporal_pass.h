#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include "on12_temporal.h"

namespace On12Graphics {
// External list, same device, six consecutive shader-visible SRVs:
// current/history/motion/depth/history-depth/reactive. Targets are resolved
// LDR color, linear FP16 history, R32 linear depth. The host owns their fences.
class TemporalPass {
public:
    HRESULT Initialize(ID3D12Device*,DXGI_FORMAT color_format,const void* vs,size_t vs_bytes,const void* ps,size_t ps_bytes);
    HRESULT Record(ID3D12GraphicsCommandList*,ID3D12DescriptorHeap*,const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,3>& targets,const Temporal::Parameters&);
    void Reset();
    // CPU-only root-signature description shared by compilation/reflection tests.
    static HRESULT SerializeSignature(ID3DBlob**);
private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    DWORD owner_=0;
};
}
