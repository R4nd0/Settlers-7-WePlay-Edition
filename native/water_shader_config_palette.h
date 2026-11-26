// SPDX-License-Identifier: MIT
#pragma once
#include <windows.h>
#include <cstddef>
#include <type_traits>
namespace WaterShaderEffects {
struct ConfigPalette {
    DWORD size=3096,version=2,render_width=0,render_height=0,enabled=0,selected_variant=0;
    wchar_t program_paths[3][512]{};
};
static_assert(sizeof(ConfigPalette)==3096&&sizeof(wchar_t)==2&&std::is_trivially_copyable<ConfigPalette>::value,"Independent fixed WaterPalette wire");
static_assert(offsetof(ConfigPalette,enabled)==16&&offsetof(ConfigPalette,selected_variant)==20&&offsetof(ConfigPalette,program_paths)==24,"Exact WaterPalette offsets");
}
