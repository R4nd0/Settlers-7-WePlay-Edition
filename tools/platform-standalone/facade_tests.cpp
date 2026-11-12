#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <cstring>
#include <cstdio>
#include <cstdlib>
enum Error { NativeFacadeMismatch, RollbackFailure };
__declspec(noreturn) static void Stop(Error) { ExitProcess(0xe0537012); }
static void ReportLocalStatus(const char*) {}
#include "steam_local_facade.h"
using SteamLocalFacade::InitBridge;
using SteamLocalFacade::LanguageBridge;

__declspec(naked) static void ReturnFromBridge() { __asm { ret } }
static int RunInit(unsigned char* facade) {
    int valid = 0;
    __asm {
        push ebx
        push esi
        mov esi, facade
        mov ebx, 0x12345600
        call InitBridge
        cmp ebx, 0x12345601
        sete al
        movzx eax, al
        mov valid, eax
        pop esi
        pop ebx
    }
    return valid;
}
static const char* RunLanguage() {
    const char* language = nullptr;
    __asm {
        call LanguageBridge
        mov language, eax
    }
    return language;
}
int main(int argc, char** argv) {
    using namespace SteamLocalFacade;
    unsigned char facade[0x90] = {};
    local_base = 0x400000;
    local_owner = GetCurrentThreadId();
    after_init = reinterpret_cast<uintptr_t>(&ReturnFromBridge);
    after_language = after_init;
    *reinterpret_cast<uint32_t*>(facade) = 0x112a3a4;
    facade[0x80] = 1;
    const int mode = argc == 2 ? atoi(argv[1]) : 0;
    if (mode == 1) facade[0x80] = 0;
    if (mode == 2) facade[0] ^= 1;
    if (mode == 3) facade[0x8c] = 1;
    if (mode == 4) ++local_owner;
    if (mode >= 1 && mode <= 4) { LocalReady(facade); return 100; }
    if (mode == 5) { ++local_owner; LocalLanguage(); return 100; }
    if (mode == 6) { LocalReady(nullptr); return 100; }
    if (mode == 7) { LocalReady(reinterpret_cast<const unsigned char*>(1)); return 100; }
    unsigned char fixture[] = {0x55,0x8b,0xec,0x68,0x00,0x10,0x40,0x00,0xc3};
    uint64_t hash = 14695981039346656037ull;
    for (auto byte : fixture) hash = (hash ^ byte) * 1099511628211ull;
    const unsigned short relocation[] = {4};
    Guard guard{0x400000, sizeof(fixture), hash, relocation, 1};
    const uintptr_t base = reinterpret_cast<uintptr_t>(fixture);
    uint32_t relocated = 0;
    memcpy(&relocated, fixture + 4, 4);
    relocated += static_cast<uint32_t>(base - 0x400000);
    memcpy(fixture + 4, &relocated, 4);
    if (!GuardMatches(base, guard)) return 1;
    fixture[2] ^= 1;
    if (GuardMatches(base, guard)) return 2;
    fixture[2] ^= 1;
    Guard invalid{0x400000,5,hash,relocation,1};
    if (GuardMatches(base, invalid)) return 3;
    SetLastError(12345);
    if (!LocalReady(facade) || GetLastError() != 12345 || !RunInit(facade) || strcmp(RunLanguage(), "english")) return 4;
    // This deliberately cannot install into a fixture image with invented code.
    if (Install(base, local_owner)) return 5;
    puts("local facade: relocated guard, corruption, range, owner, object and native bridges passed");
    return 0;
}
