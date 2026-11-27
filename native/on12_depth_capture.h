#pragma once
#include <d3d9.h>
#include <wrl/client.h>
#include <vector>
#include <cstring>
#include <cmath>
#include <algorithm>

namespace On12Graphics {
struct R32DepthCapture {
    UINT width=0,height=0,finite=0,interior=0;
    float minimum=0,maximum=0;
    std::vector<float> pixels;
};
// Explicit one-shot diagnostic only. This synchronizes the copy for CPU
// readback; it must never run in a performance comparison or every frame.
inline HRESULT ReadR32Depth(IDirect3DDevice9* native,IDirect3DTexture9* texture,R32DepthCapture& output){
    using Microsoft::WRL::ComPtr;output={};if(!native||!texture)return E_POINTER;
    D3DSURFACE_DESC d{};HRESULT hr=texture->GetLevelDesc(0,&d);if(FAILED(hr))return hr;
    if(d.Format!=D3DFMT_R32F||d.Pool!=D3DPOOL_DEFAULT||!(d.Usage&D3DUSAGE_RENDERTARGET)||d.MultiSampleType!=D3DMULTISAMPLE_NONE
        ||!d.Width||!d.Height||d.Width>4096||d.Height>4096||texture->GetLevelCount()!=1)return D3DERR_INVALIDCALL;
    ComPtr<IDirect3DDevice9> owner;hr=texture->GetDevice(&owner);if(FAILED(hr))return hr;if(owner.Get()!=native)return E_INVALIDARG;
    ComPtr<IDirect3DSurface9> source,copy;hr=texture->GetSurfaceLevel(0,&source);if(FAILED(hr))return hr;
    hr=native->CreateOffscreenPlainSurface(d.Width,d.Height,D3DFMT_R32F,D3DPOOL_SYSTEMMEM,&copy,nullptr);if(FAILED(hr))return hr;
    R32DepthCapture result;result.width=d.Width;result.height=d.Height;
    try{result.pixels.resize(static_cast<size_t>(d.Width)*d.Height);}catch(...){return E_OUTOFMEMORY;}
    hr=native->GetRenderTargetData(source.Get(),copy.Get());if(FAILED(hr))return hr;
    D3DLOCKED_RECT locked{};hr=copy->LockRect(&locked,nullptr,D3DLOCK_READONLY);if(FAILED(hr))return hr;
    if(!locked.pBits||locked.Pitch<static_cast<INT>(d.Width*sizeof(float))){copy->UnlockRect();return E_UNEXPECTED;}
    for(UINT y=0;y<d.Height;++y)std::memcpy(result.pixels.data()+static_cast<size_t>(y)*d.Width,
        static_cast<const unsigned char*>(locked.pBits)+static_cast<size_t>(y)*locked.Pitch,static_cast<size_t>(d.Width)*sizeof(float));
    hr=copy->UnlockRect();if(FAILED(hr))return hr;
    for(float z:result.pixels)if(std::isfinite(z)){
        if(!result.finite)result.minimum=result.maximum=z;else{result.minimum=(std::min)(result.minimum,z);result.maximum=(std::max)(result.maximum,z);}
        ++result.finite;if(z>0&&z<1)++result.interior;
    }
    output=std::move(result);return S_OK;
}
}
