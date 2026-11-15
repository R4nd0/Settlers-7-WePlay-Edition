// The built-in depth add-on registers its settings UI unconditionally in 6.8.
// No-GUI builds retain those C API entry points without retaining callbacks.
// The upstream runtime_api.cpp already refuses effect-runtime open_overlay().
#include "reshade.hpp"
#ifdef RESHADE_GUI
#error The no-overlay runtime must be compiled without RESHADE_GUI.
#endif
void ReShadeRegisterOverlay(const char*, void(*)(reshade::api::effect_runtime*)) {}
void ReShadeRegisterOverlayForAddon(void*, const char*, void(*)(reshade::api::effect_runtime*)) {}
void ReShadeUnregisterOverlay(const char*, void(*)(reshade::api::effect_runtime*)) {}
void ReShadeUnregisterOverlayForAddon(void*, const char*, void(*)(reshade::api::effect_runtime*)) {}
