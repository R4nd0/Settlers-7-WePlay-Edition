#pragma once
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <cstddef>
#include <memory>
#include "on12_temporal_camera.h"

namespace On12Graphics {
struct SpatialSettings {
    DWORD size = sizeof(SpatialSettings), version = 1, enabled = 0;
    float antialiasing = 1.0f;
};
static_assert(sizeof(SpatialSettings) == 16, "On12 spatial settings ABI");
struct FrameStats {
    std::uint64_t applied = 0, disabled = 0, busy = 0, unsupported = 0;
    std::uint64_t checkouts = 0, returns = 0, failed_returns = 0;
    std::uint64_t temporal_applied=0,temporal_rejected=0;
};
struct TemporalSceneSettings {
    IDirect3DSurface9* normalized_depth=nullptr;
    IDirect3DSurface9* static_confidence=nullptr;
    IDirect3DSurface9* reactive=nullptr;
    Temporal::Frame frame;Temporal::CameraParameters camera;Temporal::Parameters quality;
};
// One owner thread. All D3D9 state queries happen before checkout; native work
// uses its own D3D12 command lists. The host must Drain before D3D9 Reset.
class FrameContext {
public:
    FrameContext();
    ~FrameContext();
    FrameContext(const FrameContext&) = delete;
    FrameContext& operator=(const FrameContext&) = delete;
    HRESULT Initialize(IDirect3DDevice9*, const void* vs, size_t vs_bytes, const void* ps, size_t ps_bytes);
    HRESULT Render(IDirect3DSurface9* source, IDirect3DSurface9* target, const SpatialSettings&);
    HRESULT PrepareTemporal(const void* vs,size_t vs_bytes,const void* ps,size_t ps_bytes,
        const void* camera_vs,size_t camera_vs_bytes,const void* camera_ps,size_t camera_ps_bytes);
    HRESULT RenderTemporal(IDirect3DSurface9* source,IDirect3DSurface9* target,const SpatialSettings&,const TemporalSceneSettings&);
    HRESULT Drain(DWORD timeout_ms);
    HRESULT Shutdown(DWORD timeout_ms);
    // Borrowed native D3D9 interface from the On12 COM identity. Obtain render
    // targets through this interface, never pass wrapper surfaces to interop.
    IDirect3DDevice9* NativeDevice() const;
    bool SafeForD3D9() const;
    FrameStats Stats() const;
private:
    HRESULT RenderScene(IDirect3DSurface9*,IDirect3DSurface9*,const SpatialSettings&,const TemporalSceneSettings*);
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
