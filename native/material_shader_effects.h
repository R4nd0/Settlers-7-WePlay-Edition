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
#include <cstddef>

namespace MaterialShaderConfigV2 { struct Config; }
namespace MaterialShaderConfigV3 { struct Config; }
namespace MaterialShaderConfigV4 { struct Config; }
namespace MaterialShaderEffects {
constexpr DWORD River=1,Terrain=2,Leaf=4,AllowedFlags=River|Terrain|Leaf;
struct Config {
    DWORD size=sizeof(Config),version=1,flags=0,render_width=0,render_height=0;
    float river_reflection_scale=1.15f,river_density_scale=.94f,terrain_detail_scale=1.20f,terrain_relief_scale=1.10f;
    wchar_t leaf_vertex_path[512]{},bump_pixel_path[512]{},parallax_pixel_path[512]{},report_path[512]{};
};
struct ConstantBackup {DWORD stage=0,index=0;std::array<float,4> original{},applied{};};
struct DrawTransaction {
    IDirect3DDevice9* device=nullptr;
    IDirect3DVertexShader9* original_vertex=nullptr;
    IDirect3DPixelShader9* original_pixel=nullptr;
    DWORD role=0,constant_count=0,touched_constants=0,touched_vertex=0,touched_pixel=0;
    DWORD selected_variant=MAXDWORD;
    bool active=false,unsafe_state=false;
    std::array<ConstantBackup,2> constants{};
};
struct Sample {
    DWORD role=0,constant_count=0,restored=0,shader_changed=0;
    DWORD vertex_changed=0,pixel_changed=0;
    std::uint64_t epoch=0;
    std::array<ConstantBackup,2> constants{};
};
struct Stats {
    DWORD size=sizeof(Stats),version=2,enabled_flags=0,faulted=0,sample_count=0,cached_refs=0;
    DWORD initialized_flags=0,active_transactions=0,periodic_reporting=0;
    // Bounded device-program cache (one Leaf VS/PS selected by explicit recipe,
    // plus historical terrain stages), separate
    // from cached_refs, which counts borrowed original identities this frame.
    DWORD retained_programs=0,peak_retained_programs=0,config_version=0,active_original_refs=0;
    DWORD selected_variant=MAXDWORD; // V4 configured selection, including OFF; old modes MAXDWORD.
    std::uint64_t draws=0,matched=0,context_rejections=0,input_rejections=0,query_failures=0;
    std::uint64_t shader_creations=0,shader_failures=0,set_failures=0,restored_draws=0,restore_failures=0;
    std::uint64_t failed_draws=0,frames=0,resets=0,cache_overflows=0,exceptions=0,report_failures=0;
    std::uint64_t enable_changes=0,last_change_frame=0;
    std::array<std::uint64_t,4> applied{};
    std::array<std::uint64_t,5> program_creations{}; // slot0 Leaf VS or explicit V3 PS
    std::array<Sample,32> samples{};
    std::array<std::uint64_t,3> variant_applications{},variant_restorations{},variant_successful_draws{},variant_failed_draws{},variant_creations{};
    std::array<std::array<unsigned char,32>,3> variant_sha256{}; // Actual validated loaded code, including authored fixtures.
};
static_assert(offsetof(Stats,selected_variant)==52&&offsetof(Stats,draws)==56,"Old scalar offsets preserved; selected uses old padding");

// Caller must retain the integrating hook's exact executable/renderer/content
// guards, serial device lock and internal/TLS scope across Begin/Draw/End.
// Config paths must be resolved and confined by that trusted host to its owned
// private case/recipient LocalData (including ancestor/reparse checks). This
// component additionally requires absolute paths and a .private.json report.
// No feature is enabled without an explicit supported configuration.
bool Initialize(const Config* config) noexcept;
// Separate exact 6180-byte/version2 transport. The trusted host must dispatch
// only after its full readable-buffer, source/ABI and worker-budget guards.
// V2 terrain binds BOTH VS and PS; V1 allocation and historical V2 recipe pins
// stay intact. An explicitly staged macro v3 recipe is accepted only as a full
// hash/filename-bound alternative set with its runtime input guards. Wire stays2.
// Leaf hemisphere recipe3 is a separate exact path/hash choice with Leaf-only
// intent. It never repins historical Leaf/Macro modes or changes shader inputs
// for other original VS/PS pairs.
bool InitializeV2(const MaterialShaderConfigV2::Config* config) noexcept;
// Explicit7204-byte/version3 isolated Leaf PS recipe. Original VS stays bound;
// old VS/Terrain path fields must be empty. No Hemi/other-material stacking.
bool InitializeV3(const MaterialShaderConfigV3::Config* config) noexcept;
bool InitializeV4(const MaterialShaderConfigV4::Config* config) noexcept;
// V4-only fixed preloaded program selection. Quiescent render-owner boundary,
// valid while OFF, no file/DLL replacement, no shader bind or cache destruction.
bool SetVariant(DWORD index) noexcept;
// Diagnostic A/B switch at the host's queued render-thread safe boundary.
// Zero/subset of initialized resources only; pinned CPU programs stay loaded.
// Counters survive; bounded per-role samples start a new labeled epoch.
bool SetEnabled(DWORD flags) noexcept;
// Off by default. The host enables periodic diagnostics only for OBSERVE.
// State changes, faults and explicit Flush remain event reports.
void SetDiagnostics(bool periodic) noexcept;
void BeginDraw(IDirect3DDevice9* device,DrawTransaction* output) noexcept;
// Call even if the original draw failed. Restore BEFORE the original-program
// observer runs, so it never mistakes a temporary replacement for the original.
void EndDraw(DrawTransaction* state,HRESULT original_result) noexcept;
// Inspect unsafe_state after Begin/End. Retry immediately inside the same serial
// draw callback; if still false, stop the diagnostic renderer rather than claim
// an original fallback or observe potentially modified inputs. Do not retain a
// transaction across Present/Release/Reset (its pointers borrow the module's
// explicitly retained active-original refs, independent of frame-cache capacity).
bool RetryRestore(DrawTransaction* state) noexcept;
void EndFrame(IDirect3DDevice9* device) noexcept;
// EndFrame drops original identity refs only. Generated programs survive warm
// frames, bounded at three for V1, five for V2 and one for isolated V3. Before EVERY external device Release/Reset/detach,
// call Reset inside hook internal/TLS: it drops BOTH reference classes before
// the external call, preventing a generated shader -> device lifetime cycle.
void Reset(IDirect3DDevice9* device) noexcept;
void Flush() noexcept;
void Shutdown() noexcept;
bool ReadStats(Stats* output) noexcept;

// Pure, finite/range-checked candidate policies. Four river depth classes are
// independent components; no averaging, x-only reinterpretation or CPU game write.
bool RiverPolicy(const float fog[4],const float reflection[4],float fog_out[4],float reflection_out[4],float fog_scale,float reflection_scale) noexcept;
bool TerrainPolicy(const float original[4],float output[4],bool parallax,float detail_scale,float relief_scale) noexcept;
#ifdef S7_MATERIAL_EFFECTS_FIXTURE
bool FixtureCompatibility(unsigned role,const unsigned char vertex[32],const unsigned char pixel[32],DWORD vertex_bytes,DWORD pixel_bytes,
                          const unsigned char replacement[32]) noexcept;
bool FixtureProgramCompatibility(unsigned version,unsigned slot,const unsigned char replacement[32],DWORD bytes) noexcept;
#endif
}
