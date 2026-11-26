#pragma once
#include "ambient_occlusion_policy.h"
#include "shadow_filter_effects.h"
#include <windows.h>
#include <cstddef>
#include <type_traits>

namespace AmbientOcclusionEffects {
struct Config {DWORD size=sizeof(Config),version=1,enabled=0;};
static_assert(sizeof(Config)==12,"Fixed single-profile SSAO toggle");
static_assert(offsetof(Config,enabled)==8&&std::is_trivially_copyable<Config>::value,"Fixed wire layout");
using DrawTransaction=ShadowFilterEffects::DrawTransaction;
static_assert(std::is_same<DrawTransaction,ShadowFilterEffects::DrawTransaction>::value,"Reuse fourth draw scope; AO and shadow are mutually exclusive");
struct Stats {
    DWORD size=sizeof(Stats),version=2,initialized=0,authorized=0,requested=0,enabled=0,faulted=0,unsafe_state=0;
    DWORD active_transactions=0,cached_refs=0,last_failure=0;
    std::uint64_t frames=0,epoch=0,draws=0,matched=0,query_failures=0,context_rejections=0,policy_rejections=0,cache_bypasses=0;
    std::uint64_t applications=0,restorations=0,successful_draws=0,failed_draws=0,set_failures=0,restore_failures=0,exceptions=0,rejections=0;
    std::uint64_t current_applications=0,current_restorations=0,completed_frame=0,completed_applications=0,completed_restorations=0;
    std::uint64_t last_applied_frame=0,last_restored_frame=0,recovery_attempts=0,recoveries=0;
    DWORD last_policy_decision=0;
    DWORD sampler_available=0,sampler_min=0,sampler_mag=0,sampler_mip=0,sampler_address_u=0,sampler_address_v=0;
    std::array<DWORD,4> last_original_bits{},last_applied_bits{};
    DWORD reserved_legacy_tail=0; // Preserve original312-byte prefix including tail padding.
    DWORD profile_version=1; // Config1=A originalcap1; Config2=B boundedcap1.25.
};
static_assert(sizeof(Stats)==320&&std::is_trivially_copyable<Stats>::value,"Fixed SSAO observation layout");
static_assert(offsetof(Stats,profile_version)==312,"Profile appended; old field offsets remain stable");
// Host must independently admit EXE/DXVK/DLL/source, OBSERVE + explicit owned
// lab intent, source dimensions and a serial render-thread TLS scope. This is
// a fixed diagnostic profile; production loader must never expose its toggle.
bool Initialize(DWORD render_width,DWORD render_height,bool observe_and_explicit_lab_authorized) noexcept;
// Host queues copied Config; call at render-owner safe post-Present boundary.
bool Configure(const Config* config) noexcept;
// Only nonzero original geometry. One outstanding transaction, one owner.
void BeginDraw(IDirect3DDevice9* device,DrawTransaction* transaction) noexcept;
void EndDraw(DrawTransaction* transaction,HRESULT original_result) noexcept;
bool RetryRestore(DrawTransaction* transaction) noexcept;
// State owns backup float4 independently of caller lifetime. This explicit
// owner-thread recovery never dereferences an expired transaction token.
bool RetryPendingRestore(IDirect3DDevice9* device) noexcept;
void EndFrame(IDirect3DDevice9* device) noexcept;
// A false cleanup result means state may be unrestored: host MUST block
// original observer/fallback/new modifications/device Release/Reset/unload.
bool Reset(IDirect3DDevice9* device) noexcept;
bool InvalidateFrame() noexcept;
bool Shutdown() noexcept;
bool ReadStats(Stats* output) noexcept;
#ifdef S7_AMBIENT_OCCLUSION_FIXTURE
bool FixtureIdentities(const unsigned char vertex[32],const unsigned char pixel[32]) noexcept;
#endif
}
