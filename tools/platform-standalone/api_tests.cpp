#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <cstdlib>
#include <cstdio>
extern "C" {
uint32_t __cdecl WePlayLocalSteamBoundary();
bool __cdecl SteamAPI_Init();
int __cdecl SteamAPI_GetHSteamUser();
int __cdecl SteamAPI_GetHSteamPipe();
void* __cdecl SteamInternal_CreateInterface(const char*);
void __cdecl SteamAPI_Shutdown();
void __cdecl SteamAPI_RunCallbacks();
void* __cdecl SteamInternal_ContextInit(void*);
void __cdecl SteamAPI_RegisterCallback(void*, int);
void __cdecl SteamAPI_UnregisterCallback(void*);
}
int main(int argc, char** argv) {
    const int mode = argc == 2 ? atoi(argv[1]) : 0;
    if (mode == 1) SteamInternal_ContextInit(nullptr);
    if (mode == 2) SteamAPI_RegisterCallback(nullptr, 0);
    if (mode == 3) SteamAPI_UnregisterCallback(nullptr);
    if (mode) return 100;
    if (WePlayLocalSteamBoundary() != 0x53374c31 || SteamAPI_Init() ||
        SteamAPI_GetHSteamUser() || SteamAPI_GetHSteamPipe() ||
        SteamInternal_CreateInterface("invalid")) return 1;
    SteamAPI_RunCallbacks(); SteamAPI_Shutdown(); SteamAPI_Shutdown();
    if (GetModuleHandleW(L"steamclient.dll") || GetModuleHandleW(L"steamclient64.dll")) return 2;
    puts("local API boundary: unavailable results, lifecycle, no client module passed");
    return 0;
}
