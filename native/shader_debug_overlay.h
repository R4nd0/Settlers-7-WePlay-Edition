// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <cstdint>
#include <array>

namespace ShaderDebugOverlay {
enum class Profile:DWORD { Water=1,Leaf=2,Terrain=3,Shadow=4,Ambient=5,All=6 };
struct Selection { DWORD enabled=0,variant=0;Profile profile=Profile::Water; };
struct Status {
    // Applied means current-epoch successful selected-family proof, or fresh original
    // geometry while disabled. The host must not label a queued request applied.
    DWORD applied_valid=0,applied_enabled=0,applied_variant=0;
    DWORD pending=0,pending_enabled=0,pending_variant=0;
    Profile profile=Profile::Water;
};
struct Preset {
    DWORD size=sizeof(Preset),version=1;
    std::array<Selection,5> values{{{1,0,Profile::Water},{0,0,Profile::Leaf},
        {0,0,Profile::Terrain},{0,0,Profile::Shadow},{0,0,Profile::Ambient}}};
};
static_assert(sizeof(Preset)==68,"Five fixed tagged choices; no shader paths or arbitrary parameters");
struct MultiStatus {
    std::array<Status,5> families{{{0,0,0,0,0,0,Profile::Water},{0,0,0,0,0,0,Profile::Leaf},
        {0,0,0,0,0,0,Profile::Terrain},{0,0,0,0,0,0,Profile::Shadow},{0,0,0,0,0,0,Profile::Ambient}}};
};
struct Stats {
    DWORD size=sizeof(Stats),version=1,unsafe_state=0,pending_restore=0;
    std::uint64_t draws=0,skipped=0,failures=0,restorations=0,restore_failures=0;
    DWORD font_ready=0,retained_original_refs=0;
};
// Physical client coordinates only. QueueClick never performs any D3D work.
// Last click wins; ConsumeSelection is called by the serialized render thread.
bool QueueClick(LONG x,LONG y,DWORD output_width,DWORD output_height,Profile profile=Profile::Water) noexcept;
bool HitTest(LONG x,LONG y,DWORD output_width,DWORD output_height,Profile profile=Profile::Water) noexcept;
bool ConsumeSelection(Selection* output) noexcept;
// Combined UI model. Tabs change only UI, never shader values or pending bits.
bool QueueTab(Profile family) noexcept;
Profile SelectedTab() noexcept;
bool ReadPreset(Preset* output,std::uint64_t* revision=nullptr) noexcept;
// Complete validation precedes any change. Core owns persistence and actual
// rendering; native UI performs no filesystem access or qualification decision.
bool SetPreset(const Preset* preset,bool apply_preview) noexcept;
bool QueueApplyAll() noexcept;
bool ConsumePreset(Preset* output,DWORD* changed_families,std::uint64_t* revision=nullptr) noexcept;
// Present path only, OUTSIDE the game's BeginScene. Draw owns one scene.
// Call after paired image readback, before presenting the physical output.
// false may mean safely skipped; inspect unsafe_state before further game work.
bool Draw(IDirect3DDevice9* device,IDirect3DSurface9* output,DWORD width,DWORD height,const Status& status) noexcept;
bool DrawAll(IDirect3DDevice9* device,IDirect3DSurface9* output,DWORD width,DWORD height,const MultiStatus& status) noexcept;
// Original device state is durable even when the caller's stack has expired.
// Same owner thread/serialized host only; unsafe requires retry or fail-closed.
bool RetryRestore(IDirect3DDevice9* device) noexcept;
// Required BEFORE every external device Release/Reset/attach change. The font
// texture retains its parent device. Refuses cleanup while restoration is unsafe.
bool Reset(IDirect3DDevice9* device) noexcept;
bool Shutdown() noexcept;
bool ReadStats(Stats* output) noexcept;
bool HasUnsafe() noexcept;
}
