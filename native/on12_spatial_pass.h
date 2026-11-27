#pragma once
#include <windows.h>
#include <d3d12.h>
#include <wrl/client.h>
#include <cstddef>

namespace On12Graphics {
// The host owns this command list. Input must be PIXEL_SHADER_RESOURCE and
// output RENDER_TARGET. Recording never touches a game's D3D9 pipeline state.
class SpatialPass {
public:
    HRESULT Initialize(ID3D12Device* device, DXGI_FORMAT format,
                       const void* vertex, size_t vertex_bytes,
                       const void* pixel, size_t pixel_bytes);
    HRESULT Record(ID3D12GraphicsCommandList* list, ID3D12DescriptorHeap* input_heap,
                   D3D12_GPU_DESCRIPTOR_HANDLE input, D3D12_CPU_DESCRIPTOR_HANDLE output,
                   UINT source_width, UINT source_height, UINT output_width, UINT output_height,
                   float antialiasing);
    void Reset();
private:
    Microsoft::WRL::ComPtr<ID3D12Device> device_;
    Microsoft::WRL::ComPtr<ID3D12RootSignature> signature_;
    Microsoft::WRL::ComPtr<ID3D12PipelineState> pipeline_;
    DWORD owner_ = 0;
};
}
