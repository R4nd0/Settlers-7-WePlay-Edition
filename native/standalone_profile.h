#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>

namespace StandaloneProfile {
enum class Edition { History, Gold };
// Called only after the entry initializer has verified the complete game image.
bool Prepare(const wchar_t* executable, Edition edition, HMODULE initializer);
const wchar_t* DocumentsBase();
const wchar_t* ProfileDirectory();
}
