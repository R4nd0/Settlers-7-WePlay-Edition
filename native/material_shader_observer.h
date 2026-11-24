#pragma once
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <array>
#include <cstdint>

// Optional read-only component. The integrating hook must first establish its
// executable/renderer/device guards. Call only inside that hook's internal/TLS
// scope: releasing a cached shader can release its parent D3D9 device.
namespace MaterialShaderObserver {
constexpr unsigned Draw=1, Indexed=2;
constexpr size_t MaxCatalog=512, MaxUnknown=64, MaxPairs=64;
constexpr size_t MaxCaptureTargets=8,MaxCaptureConstants=8,MaxCaptureSamplers=16,MaxCaptureSamples=64;

struct CatalogEntry {
    std::uint32_t id, family, stage, bytes; // stage: 1 vertex, 2 pixel
    std::array<unsigned char,32> sha256;
};
static_assert(sizeof(CatalogEntry)==48,"Catalog ABI");
struct RecordCount { std::uint32_t id,family,stage,bytes; std::uint64_t draws; };
struct UnknownCount {
    std::uint32_t stage,bytes;
    std::array<unsigned char,32> sha256;
    std::uint64_t draws;
};
struct PairCount {
    std::uint32_t vertex_id,pixel_id,vertex_aliases,pixel_aliases;
    std::uint64_t draws;
};
struct ConstantRequest { std::uint32_t stage,index,component_mask; };
struct CaptureTarget {
    std::uint32_t vertex_id,pixel_id,sample_limit,constant_count,sampler_mask;
    std::array<ConstantRequest,MaxCaptureConstants> constants{};
};
struct CaptureConfig {
    std::uint32_t size=sizeof(CaptureConfig),version=1,target_count=0,frame_stride=120;
    std::uint64_t first_frame=0;
    std::array<CaptureTarget,MaxCaptureTargets> targets{};
};
struct ConstantSample {
    std::uint32_t stage,index,component_mask,valid;
    std::array<float,4> value{};
};
struct SamplerSample {
    std::uint32_t index,valid,type,format,width,height,depth,levels;
};
struct CaptureSample {
    std::uint64_t frame,successful_draw;
    std::uint32_t target,vertex_id,pixel_id,vertex_aliases,pixel_aliases,constant_count,sampler_count;
    std::uint32_t context_valid,target_width,target_height,target_format,viewport_width,viewport_height;
    std::uint32_t alpha_blend,alpha_test,srgb_write;
    std::array<ConstantSample,MaxCaptureConstants> constants{};
    std::array<SamplerSample,MaxCaptureSamplers> samplers{};
};
// Raw shader uniforms from one matched original River geometry draw. This is
// not the generic Vision context camera and contains no inferred elevation or
// exposure. One successful full-scene snapshot is retained per observer frame.
struct FrameSnapshot {
    DWORD size=sizeof(FrameSnapshot),version=1,available=0,context_valid=0;
    std::uint64_t frame=0,successful_draw=0;
    DWORD vertex_id=0,pixel_id=0,vertex_aliases=0,pixel_aliases=0;
    DWORD target_width=0,target_height=0,target_format=0,viewport_width=0,viewport_height=0;
    DWORD alpha_blend=0,alpha_test=0,srgb_write=0;
    std::array<unsigned char,32> catalog_sha256{},vertex_sha256{},pixel_sha256{};
    std::array<float,16> vs_constants_12_15{};
    std::array<float,3> vs16_eye{},ps5_sun_direction{},ps7_sun_specular{};
};
struct Stats {
    std::uint32_t size=sizeof(Stats),version=1,enabled=0,catalog_count=0;
    std::uint32_t unknown_count=0,pair_count=0,cached_refs=0,peak_cached_refs=0;
    std::uint64_t successful_draws=0,failed_draws=0,frames=0,resets=0;
    std::uint64_t shader_queries=0,shader_reads=0,cache_hits=0;
    std::uint64_t query_failures=0,oversize_programs=0,cache_overflows=0;
    std::uint64_t unknown_overflows=0,pair_overflows=0,context_rejections=0,exceptions=0;
    std::uint64_t report_failures=0,known_vertex_draws=0,known_pixel_draws=0,known_pair_draws=0;
    std::uint32_t capture_target_count=0,capture_sample_count=0;
    std::uint64_t capture_failures=0;
    std::array<RecordCount,MaxCatalog> records{};
    std::array<UnknownCount,MaxUnknown> unknown{};
    std::array<PairCount,MaxPairs> pairs{};
    std::array<CaptureSample,MaxCaptureSamples> captures{};
};

// Scalar catalog only (S7MSCAT1); no shader bytes/source are accepted from disk.
bool Initialize(const wchar_t* catalog_path,const unsigned char expected_sha256[32],
                const wchar_t* report_path) noexcept;
// Optional read-only input capture. Catalog IDs resolve to exact original
// VS+PS hashes/sizes; aliases do not imply a unique source material/vendor.
// Requests are bounded single float4 registers; only selected finite components
// are retained. Sampler capture contains dimensions/formats only, never pixels.
bool ConfigureCapture(const CaptureConfig* config) noexcept;
bool ConfigureFrameSnapshot(DWORD render_width,DWORD render_height) noexcept;
bool CopyLatestFrameSnapshot(std::uint64_t expected_present_frame,FrameSnapshot* output) noexcept;
void InvalidateFrameSnapshot() noexcept;
// Invoke AFTER the original nonzero-geometry Draw call, retaining its HRESULT.
void ObserveDraw(IDirect3DDevice9* device,unsigned kind,HRESULT draw_result) noexcept;
// Invoke at Present entry, even when that Present will fail or fall back.
void EndFrame(IDirect3DDevice9* device) noexcept;
// Invoke BEFORE EVERY external device Release, Reset, detach and teardown,
// inside the integrating hook's internal/TLS scope. This prevents retained
// shader->device references from keeping a device alive across an external Release.
void Reset(IDirect3DDevice9* device) noexcept;
bool ReadStats(Stats* output) noexcept;
void Flush() noexcept;
void Shutdown() noexcept;
#ifdef S7_MATERIAL_OBSERVER_FIXTURE
bool FixtureFrameIdentity(const unsigned char vertex[32],UINT vertex_bytes,
                          const unsigned char pixel[32],UINT pixel_bytes) noexcept;
#endif
}
