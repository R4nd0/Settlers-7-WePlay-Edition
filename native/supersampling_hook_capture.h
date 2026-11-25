#pragma once
#include "supersampling_hook.h"
#include "graphics_observation.h"
#include "material_shader_observer.h"
#include "material_comparison.h"
#include "water_shader_effects.h"
#include "shadow_filter_effects.h"
#include <array>
#include <string>

namespace NativeGraphicsCapture {
struct Request {DWORD size,version,sequence;};
static_assert(sizeof(Request)==12,"Capture request ABI changed.");
struct Result {
    DWORD sequence=0,status=0,source_width=0,source_height=0,output_width=0,output_height=0,format=0;
    HRESULT readback_hresult=S_OK;
    LARGE_INTEGER started{},finished{},frequency{};
    SupersamplingStats graphics{};
    GraphicsObservation::Snapshot observation{};
    MaterialShaderObserver::FrameSnapshot scene_uniforms{};
    bool scene_uniforms_available=false;
    MaterialComparison::State comparison{};
    bool comparison_state_available=false;
    WaterShaderEffects::Stats water{};
    bool water_state_available=false;
    ShadowFilterEffects::Stats shadow{};
    bool shadow_state_available=false;
    std::array<char,65> source_sha256{},output_sha256{},state_sha256{};
    std::wstring receipt_path;
};
bool TargetAvailable(const wchar_t* base_report,DWORD sequence) noexcept;
// Owning render thread/TLS only. Source/output are actual surfaces from the same
// frame, after filtering and before Present. Never captures a desktop/window.
bool Read(IDirect3DDevice9* device,IDirect3DSurface9* source,IDirect3DSurface9* output,
          const wchar_t* base_report,DWORD sequence,const SupersamplingStats& graphics,Result& result) noexcept;
// Writes the final scalar receipt after the corresponding real Present result.
void Finish(Result& result,HRESULT attempted_path_result,HRESULT final_caller_result,
            bool fallback_used,bool filtered_path) noexcept;
}
