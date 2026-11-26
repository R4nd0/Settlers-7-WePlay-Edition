// SPDX-License-Identifier: MIT
#pragma once
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <d3d9.h>
#include <cstdint>

namespace ShaderDebugOverlay {
struct Selection { DWORD enabled=0,variant=0; };
struct Status {
    // Applied means current-epoch successful River proof, or fresh original
    // geometry while disabled. The host must not label a queued request applied.
    DWORD applied_valid=0,applied_enabled=0,applied_variant=0;
    DWORD pending=0,pending_enabled=0,pending_variant=0;
};
struct Stats {
    DWORD size=sizeof(Stats),version=1,unsafe_state=0,pending_restore=0;
    std::uint64_t draws=0,skipped=0,failures=0,restorations=0,restore_failures=0;
    DWORD font_ready=0,retained_original_refs=0;
};
// Physical client coordinates only. QueueClick never performs any D3D work.
// Last click wins; ConsumeSelection is called by the serialized render thread.
bool QueueClick(LONG x,LONG y,DWORD output_width,DWORD output_height) noexcept;
bool HitTest(LONG x,LONG y,DWORD output_width,DWORD output_height) noexcept;
bool ConsumeSelection(Selection* output) noexcept;
// Present path only, OUTSIDE the game's BeginScene. Draw owns one scene.
// Call after paired image readback, before presenting the physical output.
// false may mean safely skipped; inspect unsafe_state before further game work.
bool Draw(IDirect3DDevice9* device,IDirect3DSurface9* output,DWORD width,DWORD height,const Status& status) noexcept;
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
