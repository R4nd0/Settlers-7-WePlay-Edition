#pragma once
#include "on12_temporal.h"
#include <cstddef>

namespace On12Graphics::Temporal {
// Four raw float4 registers: matrix columns as used by mul(matrix, position).
using CameraMatrix=std::array<float,16>;
struct CameraParameters {
    // Row-major map from (current NDC.x*clipW, NDC.y*clipW, clipW, 1)
    // directly to previous clip coordinates. Avoids large world intermediates.
    std::array<float,16> previous_clip_from_xyw{};
    std::uint32_t width=0,height=0;
    float jitter_x=0,jitter_y=0;
    float normalized_depth_scale=0,raster_origin_x=0,raster_origin_y=0,reserved=0;
};
static_assert(sizeof(CameraParameters)==96&&offsetof(CameraParameters,width)==64,"Camera constants ABI");
// S7's R32F depth is clipW / c17.w, not device NDC Z. D3D9 pixel centers
// have origin 0; native D3D12 producers may explicitly select origin 0.5.
bool BuildCamera(const CameraMatrix& current,const CameraMatrix& previous,float depth_scale,
                 std::uint32_t width,std::uint32_t height,float jitter_x,float jitter_y,
                 float raster_origin,CameraParameters&);
bool ValidCamera(const CameraParameters&);
bool CameraMotion(const CameraParameters&,std::uint32_t x,std::uint32_t y,float normalized_depth,
                  float static_confidence,Motion&,float& linear_depth);
bool JitterCamera(const CameraMatrix&,std::uint32_t width,std::uint32_t height,float x,float y,CameraMatrix&);
}
