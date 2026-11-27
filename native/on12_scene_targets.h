#pragma once
#include "on12_frame_context.h"
#include <array>

namespace On12Graphics {
struct SceneStats {
    UINT resources = 0, ui_width = 0, ui_height = 0;
    std::uint64_t frames = 0, upscaled = 0, ui_draws = 0, mapped_targets = 0, mapped_textures = 0;
    HRESULT failure = S_OK;
};
// Virtual D3D9 state keeps the engine's native dimensions and resource identity.
// Physical world targets are smaller; UI is drawn into the native backbuffer.
class SceneTargets {
public:
    SceneTargets();
    ~SceneTargets();
    HRESULT Initialize(IDirect3DDevice9* wrapped, FrameContext*, const std::array<void*,119>&,
                       UINT scene_width, UINT scene_height, UINT native_width, UINT native_height);
    HRESULT BeginFrame();
    HRESULT BeginUi(const SpatialSettings&);
    HRESULT BeforeCapture();
    HRESULT AfterCapture();
    HRESULT AfterApply();
    HRESULT ReleaseForReset();
    void ResetSucceeded();
    HRESULT Shutdown();
    bool Active() const;
    bool Ui() const;
    bool Suspended() const;
    bool Safe() const;
    bool MapsTexture(IDirect3DBaseTexture9*) const;
    SceneStats Stats() const;
    HRESULT SetTarget(DWORD, IDirect3DSurface9*);
    HRESULT GetTarget(DWORD, IDirect3DSurface9**);
    HRESULT SetDepth(IDirect3DSurface9*);
    HRESULT GetDepth(IDirect3DSurface9**);
    HRESULT SetViewport(const D3DVIEWPORT9*);
    HRESULT GetViewport(D3DVIEWPORT9*);
    HRESULT SetScissor(const RECT*);
    HRESULT GetScissor(RECT*);
    HRESULT SetTexture(DWORD, IDirect3DBaseTexture9*);
    HRESULT GetTexture(DWORD, IDirect3DBaseTexture9**);
    HRESULT Stretch(IDirect3DSurface9*, const RECT*, IDirect3DSurface9*, const RECT*, D3DTEXTUREFILTERTYPE);
    HRESULT Fill(IDirect3DSurface9*, const RECT*, D3DCOLOR);
    HRESULT Clear(DWORD, const D3DRECT*, DWORD, D3DCOLOR, float, DWORD);
private:
    struct Impl;
    std::unique_ptr<Impl> impl_;
};
}
