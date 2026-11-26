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

namespace WaterShaderEffects {
struct ConfigPalette;
struct Config {
    DWORD size=sizeof(Config),version=1,render_width=0,render_height=0,enabled=0;
    wchar_t pixel_path[512]{};
    unsigned char pixel_sha256[32]{};
};
static_assert(sizeof(Config)==1076,"Fixed WaterProgram transport changed");
struct DrawTransaction {
    IDirect3DDevice9* device=nullptr;
    IDirect3DPixelShader9* original_pixel=nullptr; // borrowed; pending ownership lives in module State
    bool touched=false,active=false,unsafe_state=false;
    DWORD owner_thread=0,selected_variant=MAXDWORD;
};
struct Stats {
    DWORD size=sizeof(Stats),version=2,initialized=0,configured=0,requested=0,enabled=0;
    DWORD faulted=0,unsafe_state=0,active_transactions=0,cached_refs=0,retained_programs=0;
    DWORD last_failure=0,pending_original_refs=0,variant=0;
    std::uint64_t draws=0,matched=0,context_rejections=0,query_failures=0,cache_overflows=0;
    std::uint64_t shader_creations=0,shader_failures=0,set_failures=0,restore_failures=0;
    std::uint64_t applications=0,restorations=0,successful_draws=0,failed_draws=0,exceptions=0;
    std::uint64_t frames=0,resets=0,configuration_epoch=0,config_rejections=0;
    std::uint64_t current_applications=0,current_restorations=0;
    std::uint64_t completed_frame=0,completed_applications=0,completed_restorations=0;
    std::uint64_t last_applied_frame=0,last_restored_frame=0;
    std::uint64_t recovery_attempts=0,recoveries=0;
    DWORD config_version=1,selected_variant=MAXDWORD,completed_drawn_variant=MAXDWORD;
    std::uint64_t completed_successful_draws=0,completed_failed_draws=0;
    std::array<std::uint64_t,3> variant_applications{},variant_restorations{},variant_successful_draws{},variant_failed_draws{},variant_creations{};
    std::array<std::array<unsigned char,32>,3> variant_sha256{};
};

// The host retains its exact EXE/DXVK/build guards and serial/TLS scope. trusted
// report must be the host's admitted private report or recipient LocalData path.
// Only canonical non-reparse descendants of that directory are accepted, with
// the exact recipe/hash filename and pinned recipient-generated program.
bool Initialize(const wchar_t* trusted_report,DWORD render_width,DWORD render_height) noexcept;
// Queue outside this module; call Configure only at the owning render thread's
// safe frame boundary. Enable latches that render owner; a remote worker must
// enqueue a copied config rather than call this directly. Disable is explicit.
// Do not overlap legacy River VS68/69
// mutation; terrain/leaf V2 flags6 are independent.
bool Configure(const Config* config) noexcept;
// Separate3096/version2. First request (including OFF) preloads all3 immutable
// CPU buffers/leases. Later requests change only enabled/index at safe boundary.
bool ConfigurePalette(const ConfigPalette* config) noexcept;
// Call only for nonzero geometry. EndDraw's successful-frame counters assume
// the host retains its original successful/nonzero draw contract.
void BeginDraw(IDirect3DDevice9* device,DrawTransaction* transaction) noexcept;
void EndDraw(DrawTransaction* transaction,HRESULT original_result) noexcept;
// Always inspect unsafe_state after Begin and End. Retry in the SAME serial
// callback. If still unsafe, no original draw, observer or fallback is safe.
bool RetryRestore(DrawTransaction* transaction) noexcept;
// Explicit durable recovery AFTER a failed callback has returned. The host
// must hold its owning render-thread serial/TLS scope. No caller transaction is
// dereferenced; active healthy transactions and foreign threads are refused.
// A successful recovery verifies the original binding and releases the pending
// module-owned ref, but does not retroactively qualify the failed frame.
bool RetryPendingRestore(IDirect3DDevice9* device) noexcept;
void EndFrame(IDirect3DDevice9* device) noexcept;
// Before every external device Release/Reset, inside the host TLS scope. Drops
// originals and the single generated PS; never infers device death from Release.
// Refuses cleanup across an active/unrestored transaction.
bool Reset(IDirect3DDevice9* device) noexcept;
// Actual Reset/device replacement only: clears frame proof and rebinds the
// render owner to this trusted host callback. Never call for ordinary Release.
void InvalidateFrame() noexcept;
bool Shutdown() noexcept;
// Shutdown keeps scalar historical counters readable, with all live flags/refs
// cleared. A later Initialize starts a new statistics session.
bool ReadStats(Stats* output) noexcept;

#ifdef S7_WATER_SHADER_FIXTURE
// Authored CPU fake-COM fixture only. Not declared/compiled in production.
bool FixtureIdentities(const unsigned char vertex[32],const unsigned char pixel[32],
                       const unsigned char replacement[32]) noexcept;
bool FixturePaletteIdentity(DWORD index,const unsigned char replacement[32]) noexcept;
#endif
}
