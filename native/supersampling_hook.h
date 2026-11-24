#pragma once
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>

// Versioned laboratory ABI. No pointer into game-owned structures is accepted.
struct SupersamplingConfig {
    DWORD size, version, render_width, render_height, output_width, output_height;
    wchar_t report_path[512];
};

struct SupersamplingStats {
    DWORD size, version, attached, present_calls, output_present_calls, fallback_calls;
    DWORD reset_calls, reset_successes, release_calls, swapchain_creations;
    DWORD source_width, source_height, output_width, output_height, client_width, client_height;
    DWORD viewport_width, viewport_height, target_width, target_height;
    DWORD high_resolution_draws, other_resolution_draws, indexed_draws;
    DWORD last_reset_requested_width, last_reset_requested_height;
    DWORD last_hresult, last_failure, detached, zero_release_returns;
    DWORD resize_queued, resize_applied, resize_failure, before_client_width, before_client_height;
    DWORD after_client_width, after_client_height;
    DWORD logical_size_messages, logical_rect_calls, logical_point_calls, logical_mouse_messages;
    DWORD logical_api_slots, logical_context_rejections;
    DWORD edition, cached_release_calls, output_cache_drops, window_shutdowns, terminal_cleanup;
    DWORD capture_requests, capture_completed, capture_failures, capture_frame;
    DWORD effects_requested, effects_configured, effects_active, effects_pending, effects_revision;
    DWORD effects_config_errors, effects_unsafe, effects_blocked_draws;
    DWORD effects_drawn_revision, effects_drawn_active, effects_frame_has_geometry;
    DWORD effects_module_faulted, effects_module_shader_failures, effects_module_set_failures;
    DWORD effects_module_restore_failures, effects_module_exceptions;
    DWORD effects_faulted, effects_disabled_reason, effects_fallback_draws;
    DWORD engine_deinit_calls, engine_deinit_rejected, engine_deinit_cleanup, engine_deinit_returned, shutdown_stage;
};

extern "C" DWORD WINAPI WePlaySupersamplingInitialize(void* config);
extern "C" DWORD WINAPI WePlaySupersamplingReadStats(void* output);
extern "C" DWORD WINAPI WePlayNativeGraphicsRequestCapture(void* request);
extern "C" DWORD WINAPI WePlayNativeGraphicsConfigureObservation(void* config);
extern "C" DWORD WINAPI WePlayNativeGraphicsConfigureEffects(void* config);
#ifdef S7_SUPERSAMPLING_FIXTURE
extern "C" DWORD WINAPI WePlaySupersamplingAttach(IDirect3DDevice9* device);
extern "C" DWORD WINAPI WePlaySupersamplingDetach(IDirect3DDevice9* device);
extern "C" BOOL WINAPI WePlaySupersamplingFixtureRect(HWND window,RECT* rectangle,const void* caller);
extern "C" BOOL WINAPI WePlaySupersamplingFixturePoint(HWND window,POINT* point,const void* caller,BOOL to_screen);
extern "C" DWORD WINAPI WePlaySupersamplingFixtureFailPresent();
#endif
#ifdef S7_MATERIAL_EFFECTS_FIXTURE
extern "C" DWORD WINAPI WePlaySupersamplingFixtureFailDraw();
extern "C" DWORD WINAPI WePlaySupersamplingFixtureModuleFault();
#endif
#ifdef S7_SUPERSAMPLING_FIXTURE
extern "C" DWORD WINAPI WePlaySupersamplingFixturePrepareDeinit(const void* caller,DWORD thread);
#endif
