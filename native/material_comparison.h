// SPDX-License-Identifier: MIT
// Diagnostic-only render-uniform isolation. Never expose in production choices.
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
#include <type_traits>

namespace MaterialComparison {
struct Config { DWORD size=sizeof(Config),version=1,enabled=0; };
static_assert(sizeof(Config)==12,"comparison diagnostic ABI");
struct Transaction {
    IDirect3DDevice9* device=nullptr;
    std::array<float,4> original{},applied{};
    bool touched=false,active=false,unsafe_state=false,pin_candidate=false;
};
struct State {
    DWORD size=sizeof(State),version=1,initialized=0,requested=0,active=0,pinned=0;
    DWORD faulted=0,unsafe_state=0,cached_refs=0,active_transactions=0;
    DWORD render_width=0,render_height=0;
    std::array<float,4> pinned_ps80{};
    std::uint64_t epoch=0,draws=0,matched=0,applications=0,restorations=0;
    std::uint64_t failed_draws=0,context_rejections=0,input_rejections=0,query_failures=0;
    // cache_overflows counts positive matches processed without caching at the
    // 128-reference limit. Such matches still apply; negatives are never cached.
    std::uint64_t set_failures=0,restore_failures=0,exceptions=0,cache_overflows=0;
    std::uint64_t frames=0,resets=0;
    // Local Present/EndFrame sequence. The host must bind frames to its native
    // capture frame; a retained global pin alone proves no captured-frame draw.
    std::uint64_t pin_committed_frame=0,last_applied_frame=0,last_restored_frame=0;
    std::uint64_t frame_applications=0,frame_restorations=0;
    std::uint64_t completed_frame_applications=0,completed_frame_restorations=0;
};
static_assert(std::is_standard_layout<State>::value&&std::is_trivially_copyable<State>::value,"capture state POD");

// Trusted diagnostic host only: retain original executable/renderer/source
// guards and OBSERVE authorization, then serialize under its device lock/TLS.
// Invoke Begin/End only for nonzero geometry (including indexed vertex count).
// There is no game-clock/RNG/simulation/asset/image or file operation here.
bool Initialize(DWORD render_width,DWORD render_height,bool enabled=false) noexcept;
// Apply at a render-safe boundary. Every accepted reconfiguration clears the
// pin, including enabled->enabled. Refuses active or unrestored transactions.
bool Configure(const Config* config) noexcept;
// Begin before material effects, End after their restoration and before the
// original-program observer. PS80 is set on EVERY matching control/candidate
// draw independently of the material effect mask. The first finite value pins
// only after a successful original draw and successful immediate restoration.
void BeginDraw(IDirect3DDevice9* device,Transaction* output) noexcept;
void EndDraw(Transaction* transaction,HRESULT original_draw_result) noexcept;
// Inspect unsafe_state after Begin/End. Retry immediately in the SAME callback;
// if unsuccessful, block rendering/observer/original-fallback claims. The caller
// must still End after a failed/throwing original draw. Never copy a transaction
// or retain it across lifecycle/configuration boundaries.
bool RetryRestore(Transaction* transaction) noexcept;
// EndFrame drops original shader identity refs. Reset must run before EVERY
// external device Release/Reset/detach, including a foreign-thread Release under
// the same host lock. It drops refs/device/thread but RETAINS the pinned float4.
// This permits comparing reset/recreated device frames under one explicit pin.
// Reconfigure/disable or Shutdown clears the pin. Lifecycle while a transaction
// is active/unrestored is refused and faults the diagnostic; restore first.
// Routine external Release cleanup retains already-recorded frame evidence.
// Only an actual device Reset/attachment context change invalidates that evidence.
void EndFrame(IDirect3DDevice9* device) noexcept;
void Reset(IDirect3DDevice9* device) noexcept;
void InvalidateFrame() noexcept;
void Shutdown() noexcept;
bool ReadState(State* output) noexcept;
#ifdef S7_MATERIAL_COMPARISON_FIXTURE
// CPU fixtures use synthetic shader programs, never redistributed game bytes.
bool FixtureIdentity(const unsigned char vertex[32],DWORD vertex_bytes,
                     const unsigned char pixel[32],DWORD pixel_bytes) noexcept;
#endif
}
