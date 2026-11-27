#include "on12_temporal_pass.h"
#include <climits>

namespace On12Graphics {
using Microsoft::WRL::ComPtr;
void TemporalPass::Reset(){pipeline_.Reset();signature_.Reset();device_.Reset();owner_=0;}
HRESULT TemporalPass::SerializeSignature(ID3DBlob** result){
    if(!result)return E_POINTER;*result=nullptr;
    D3D12_DESCRIPTOR_RANGE range{};range.RangeType=D3D12_DESCRIPTOR_RANGE_TYPE_SRV;range.NumDescriptors=6;
    D3D12_ROOT_PARAMETER roots[2]{};roots[0].ParameterType=D3D12_ROOT_PARAMETER_TYPE_DESCRIPTOR_TABLE;roots[0].DescriptorTable={1,&range};roots[0].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    roots[1].ParameterType=D3D12_ROOT_PARAMETER_TYPE_32BIT_CONSTANTS;roots[1].Constants={0,0,16};roots[1].ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_STATIC_SAMPLER_DESC sampler{};sampler.Filter=D3D12_FILTER_MIN_MAG_MIP_LINEAR;sampler.AddressU=sampler.AddressV=sampler.AddressW=D3D12_TEXTURE_ADDRESS_MODE_CLAMP;
    sampler.MaxAnisotropy=1;sampler.ComparisonFunc=D3D12_COMPARISON_FUNC_ALWAYS;sampler.MaxLOD=D3D12_FLOAT32_MAX;sampler.ShaderVisibility=D3D12_SHADER_VISIBILITY_PIXEL;
    D3D12_ROOT_SIGNATURE_DESC desc{};desc.NumParameters=2;desc.pParameters=roots;desc.NumStaticSamplers=1;desc.pStaticSamplers=&sampler;
    desc.Flags=D3D12_ROOT_SIGNATURE_FLAG_DENY_HULL_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_DOMAIN_SHADER_ROOT_ACCESS|D3D12_ROOT_SIGNATURE_FLAG_DENY_GEOMETRY_SHADER_ROOT_ACCESS;
    ComPtr<ID3DBlob> error;return D3D12SerializeRootSignature(&desc,D3D_ROOT_SIGNATURE_VERSION_1,result,&error);
}
HRESULT TemporalPass::Initialize(ID3D12Device* device,DXGI_FORMAT format,const void* vs,size_t vb,const void* ps,size_t pb){
    if(!device||!vs||!ps||!vb||!pb||vb>1024*1024||pb>1024*1024||device_)return E_INVALIDARG;
    if(format!=DXGI_FORMAT_R8G8B8A8_UNORM&&format!=DXGI_FORMAT_B8G8R8A8_UNORM&&format!=DXGI_FORMAT_B8G8R8X8_UNORM)return E_INVALIDARG;
    for(auto f:{format,DXGI_FORMAT_R16G16B16A16_FLOAT,DXGI_FORMAT_R32_FLOAT}){
        D3D12_FEATURE_DATA_FORMAT_SUPPORT support{f,D3D12_FORMAT_SUPPORT1_NONE,D3D12_FORMAT_SUPPORT2_NONE};HRESULT hr=device->CheckFeatureSupport(D3D12_FEATURE_FORMAT_SUPPORT,&support,sizeof(support));if(FAILED(hr))return hr;
        constexpr auto required=D3D12_FORMAT_SUPPORT1_RENDER_TARGET|D3D12_FORMAT_SUPPORT1_SHADER_SAMPLE;
        if((support.Support1&required)!=required)return DXGI_ERROR_UNSUPPORTED;
    }
    ComPtr<ID3DBlob> bytes;HRESULT hr=SerializeSignature(&bytes);if(FAILED(hr))return hr;
    ComPtr<ID3D12RootSignature> signature;hr=device->CreateRootSignature(0,bytes->GetBufferPointer(),bytes->GetBufferSize(),IID_PPV_ARGS(&signature));if(FAILED(hr))return hr;
    D3D12_GRAPHICS_PIPELINE_STATE_DESC state{};state.pRootSignature=signature.Get();state.VS={vs,vb};state.PS={ps,pb};state.SampleMask=UINT_MAX;
    for(auto& blend:state.BlendState.RenderTarget){blend.SrcBlend=blend.SrcBlendAlpha=D3D12_BLEND_ONE;blend.DestBlend=blend.DestBlendAlpha=D3D12_BLEND_ZERO;blend.BlendOp=blend.BlendOpAlpha=D3D12_BLEND_OP_ADD;blend.LogicOp=D3D12_LOGIC_OP_NOOP;blend.RenderTargetWriteMask=D3D12_COLOR_WRITE_ENABLE_ALL;}
    state.RasterizerState.FillMode=D3D12_FILL_MODE_SOLID;state.RasterizerState.CullMode=D3D12_CULL_MODE_NONE;state.RasterizerState.DepthClipEnable=TRUE;
    state.DepthStencilState.DepthWriteMask=D3D12_DEPTH_WRITE_MASK_ZERO;state.DepthStencilState.DepthFunc=D3D12_COMPARISON_FUNC_ALWAYS;
    state.DepthStencilState.StencilReadMask=state.DepthStencilState.StencilWriteMask=255;
    state.DepthStencilState.FrontFace={D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_STENCIL_OP_KEEP,D3D12_COMPARISON_FUNC_ALWAYS};state.DepthStencilState.BackFace=state.DepthStencilState.FrontFace;
    state.PrimitiveTopologyType=D3D12_PRIMITIVE_TOPOLOGY_TYPE_TRIANGLE;state.NumRenderTargets=3;state.RTVFormats[0]=format;state.RTVFormats[1]=DXGI_FORMAT_R16G16B16A16_FLOAT;state.RTVFormats[2]=DXGI_FORMAT_R32_FLOAT;state.SampleDesc.Count=1;
    ComPtr<ID3D12PipelineState> pipeline;hr=device->CreateGraphicsPipelineState(&state,IID_PPV_ARGS(&pipeline));if(FAILED(hr))return hr;
    device_=device;signature_=signature;pipeline_=pipeline;owner_=GetCurrentThreadId();return S_OK;
}
HRESULT TemporalPass::Record(ID3D12GraphicsCommandList* list,ID3D12DescriptorHeap* heap,const std::array<D3D12_CPU_DESCRIPTOR_HANDLE,3>& targets,const Temporal::Parameters& p){
    if(!device_||!list||!heap||!Temporal::Valid(p)||!targets[0].ptr||!targets[1].ptr||!targets[2].ptr)return E_INVALIDARG;
    if(GetCurrentThreadId()!=owner_)return RPC_E_WRONG_THREAD;
    const auto desc=heap->GetDesc();if(desc.Type!=D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV||desc.NumDescriptors<6||!(desc.Flags&D3D12_DESCRIPTOR_HEAP_FLAG_SHADER_VISIBLE))return E_INVALIDARG;
    ComPtr<ID3D12Device> ld,hd;HRESULT hr=list->GetDevice(IID_PPV_ARGS(&ld));if(FAILED(hr))return hr;hr=heap->GetDevice(IID_PPV_ARGS(&hd));if(FAILED(hr))return hr;if(ld.Get()!=device_.Get()||hd.Get()!=device_.Get())return E_INVALIDARG;
    D3D12_VIEWPORT vp{0,0,static_cast<float>(p.width),static_cast<float>(p.height),0,1};D3D12_RECT sc{0,0,static_cast<LONG>(p.width),static_cast<LONG>(p.height)};
    list->SetPipelineState(pipeline_.Get());list->SetGraphicsRootSignature(signature_.Get());list->SetDescriptorHeaps(1,&heap);list->SetGraphicsRootDescriptorTable(0,heap->GetGPUDescriptorHandleForHeapStart());
    list->SetGraphicsRoot32BitConstants(1,16,&p,0);list->RSSetViewports(1,&vp);list->RSSetScissorRects(1,&sc);list->OMSetRenderTargets(3,targets.data(),FALSE,nullptr);
    list->IASetPrimitiveTopology(D3D_PRIMITIVE_TOPOLOGY_TRIANGLELIST);list->DrawInstanced(3,1,0,0);return S_OK;
}
}
