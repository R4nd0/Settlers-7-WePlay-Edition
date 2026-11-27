#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include "on12_temporal_camera.h"

namespace On12Graphics {
// Two SRVs: normalized R32 depth and explicit static-surface confidence.
// Two RTVs: float4 motion (xy, expected previous clipW, confidence), linear Z.
// Resource transitions, lifetime and queue/fence ownership belong to the host.
class CameraPass {
public:
    HRESULT Initialize(ID3D12Device*,const void* vs,size_t vs_bytes,const void* ps,size_t ps_bytes);
    HRESULT Record(ID3D12GraphicsCommandList*,ID3D12DescriptorHeap*,const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,2>&,const Temporal::CameraParameters&);
    static HRESULT SerializeSignature(ID3DBlob**);
private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    DWORD owner_=0;
};
}
