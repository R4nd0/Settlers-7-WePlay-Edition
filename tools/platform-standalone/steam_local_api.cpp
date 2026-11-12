// Deliberately unavailable Steam API boundary for the isolated local-facade lab.
// It has no client loading, IPC, socket, account, ticket or ownership machinery.
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <intrin.h>
#include <stdint.h>

extern "C" uint32_t __cdecl WePlayLocalSteamBoundary() { return 0x53374c31; }
extern "C" bool __cdecl SteamAPI_Init() { return false; }
extern "C" int __cdecl SteamAPI_GetHSteamUser() { return 0; }
extern "C" int __cdecl SteamAPI_GetHSteamPipe() { return 0; }
extern "C" void* __cdecl SteamInternal_CreateInterface(const char*) { return nullptr; }
extern "C" void __cdecl SteamAPI_Shutdown() {}
extern "C" void __cdecl SteamAPI_RunCallbacks() {}

__declspec(noreturn) static void Unexpected() {
    TerminateProcess(GetCurrentProcess(), 0xe0537011);
    __fastfail(7);
}
// These paths require SDK-owned objects. The local facade must never enter them.
extern "C" void* __cdecl SteamInternal_ContextInit(void*) { Unexpected(); }
extern "C" void __cdecl SteamAPI_RegisterCallback(void*, int) { Unexpected(); }
extern "C" void __cdecl SteamAPI_UnregisterCallback(void*) { Unexpected(); }
