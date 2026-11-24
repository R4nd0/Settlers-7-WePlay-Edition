// SPDX-License-Identifier: MIT
// Read-only observations from original, hash-bound engine exports.
#pragma once
#include <cstdint>

namespace GraphicsObservation {
enum Available : uint32_t {
    CameraReady=1u, ContextSizeReady=2u, LodReady=4u, SunAzimuthReady=8u,
    // Elevation, exposure and native simulation turns are intentionally not
    // represented as available until their independent readers are mapped.
};
enum Status : uint32_t { Ready=0, NotInitialized=1, MissingContext=2,
    InvalidOutput=3, GetterFault=4, FloatingPointUnavailable=5 };

// Fixed POD for an owning-render-thread capture. All fields require their
// availability bit; unavailable zero fields are NOT measured zero values.
struct Snapshot {
    uint32_t size, version, available, status, edition, context_width, context_height;
    float camera_world[16], fov_native[2], clip_native[2];
    float lod_scale, sun_azimuth, sun_clock_ms;
    uint32_t sun_status;
};
static_assert(sizeof(Snapshot)==124,"Graphics observation ABI changed");

// Initialize before capture, outside the present lock. Original Ubisoft/Steam/
// Gold executable identities and the common original Vision/vBase files only.
bool Initialize();
// Call from the existing owning render-thread/TLS scope. Returns camera/context
// readiness, not full scene qualification. Never writes engine/profile state.
bool Read(Snapshot& output);

#ifdef GRAPHICS_OBSERVATION_FIXTURE
using MainContext=void* (__cdecl*)();
using Camera=void* (__thiscall*)(void*);
using Matrix=void (__thiscall*)(void*,void*);
using Floats=void (__thiscall*)(void*,float*,float*);
using Size=void (__thiscall*)(void*,int*,int*);
struct FixtureGetters {MainContext main;Camera camera;Matrix world;Floats fov,clip;Size size;};
void InitializeFixture(const FixtureGetters& getters);
#endif
}
