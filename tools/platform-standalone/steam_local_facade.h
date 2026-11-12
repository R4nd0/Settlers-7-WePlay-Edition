#pragma once
// Included inside offline_bootstrap's private namespace only in a lab build.
// The existing complete executable digest / PE / application guards run first.
// This supplements them with complete native function guards, owner and object
// checks. No SteamAPI result, account identity or entitlement is manufactured.
namespace SteamLocalFacade {
static uintptr_t local_base = 0;
static DWORD local_owner = 0;
static uintptr_t after_init = 0;
static uintptr_t after_language = 0;
static char local_language[32] = "english";

struct Guard {
    uintptr_t address;
    size_t size;
    uint64_t hash;
    const unsigned short* relocations;
    size_t count;
};
static const unsigned short init_reloc[] = {6,31,50,86,109,126,147,173,190,198,290,295,303,331};
static const unsigned short overlay_reloc[] = {6,56,177,191,224,238,266,271,277,288,293,311,317,328,333,348,354,365,370,385,391,402,407,422,428};
static const unsigned short language_reloc[] = {5,11,70,90,107,125,142,156,172,190,207,223,240,259};
static const Guard guards[] = {
    {0x4c8370,368,0x1c2954374d2f1123ull,init_reloc,_countof(init_reloc)},
    {0x4c89e0,483,0xadc9357670ec84d7ull,overlay_reloc,_countof(overlay_reloc)},
    {0x4c8bf0,275,0x5c4785c821d31219ull,language_reloc,_countof(language_reloc)},
};
static bool GuardMatches(uintptr_t base, const Guard& guard) {
    const auto* code = reinterpret_cast<const unsigned char*>(base + guard.address - 0x400000);
    const uint32_t delta = static_cast<uint32_t>(base - 0x400000);
    uint64_t hash = 14695981039346656037ull;
    size_t relocation = 0;
    for (size_t i = 0; i < guard.size;) {
        if (relocation < guard.count && i == guard.relocations[relocation]) {
            if (i + 4 > guard.size) return false;
            uint32_t word = 0;
            memcpy(&word, code + i, 4);
            word -= delta;
            for (unsigned int byte = 0; byte < 4; ++byte) {
                hash = (hash ^ static_cast<unsigned char>(word >> (byte * 8))) * 1099511628211ull;
            }
            i += 4;
            ++relocation;
        } else {
            hash = (hash ^ code[i++]) * 1099511628211ull;
        }
    }
    return relocation == guard.count && hash == guard.hash;
}
static bool Owner() { return local_owner && GetCurrentThreadId() == local_owner; }
static bool ReadableFacade(const unsigned char* facade) {
    MEMORY_BASIC_INFORMATION region{};
    if (!facade || VirtualQuery(facade, &region, sizeof(region)) != sizeof(region) ||
        region.State != MEM_COMMIT || (region.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const uintptr_t offset = reinterpret_cast<uintptr_t>(facade) - reinterpret_cast<uintptr_t>(region.BaseAddress);
    return offset <= region.RegionSize && region.RegionSize - offset >= 0x90;
}
static bool __fastcall LocalReady(const unsigned char* facade) {
    const DWORD saved = GetLastError();
    if (!Owner() || !ReadableFacade(facade) ||
        *reinterpret_cast<const uintptr_t*>(facade) != local_base + 0x112a3a4 - 0x400000 ||
        facade[0x80] != 1 || *reinterpret_cast<const uint32_t*>(facade + 0x8c) != 0) {
        Stop(NativeFacadeMismatch);
    }
    // UPLAY_Start has already completed the existing local bootstrap. Its
    // explicit offline result is present, and no Steam callback object exists.
    ReportLocalStatus("local-steam-facade-ready-without-sdk");
    SetLastError(saved);
    return true;
}
static const char* __cdecl LocalLanguage() {
    if (!Owner()) Stop(NativeFacadeMismatch);
    return local_language;
}
__declspec(naked) static void InitBridge() {
    __asm {
        mov ecx, esi
        call LocalReady
        mov bl, al
        jmp dword ptr [after_init]
    }
}
__declspec(naked) static void LanguageBridge() {
    __asm {
        call LocalLanguage
        jmp dword ptr [after_language]
    }
}
struct Write {
    uintptr_t address;
    unsigned char original[6];
    unsigned char replacement[6];
    size_t size;
};
static bool Put(const Write& write, bool restore) {
    auto* target = reinterpret_cast<void*>(write.address);
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(target, write.size, PAGE_EXECUTE_READWRITE, &protection)) return false;
    memcpy(target, restore ? write.original : write.replacement, write.size);
    const bool flushed = FlushInstructionCache(GetCurrentProcess(), target, write.size) != FALSE;
    if (!VirtualProtect(target, write.size, protection, &ignored)) Stop(RollbackFailure);
    return flushed;
}
static bool Install(uintptr_t base, DWORD owner) {
    if (local_base || owner != GetCurrentThreadId()) return false;
    HMODULE boundary = GetModuleHandleW(L"steam_api.dll");
    using Marker = uint32_t(__cdecl*)();
    const auto marker = boundary ? reinterpret_cast<Marker>(GetProcAddress(boundary, "WePlayLocalSteamBoundary")) : nullptr;
    if (!marker || marker() != 0x53374c31 || GetModuleHandleW(L"steamclient.dll") ||
        GetModuleHandleW(L"steamclient64.dll")) return false;
    for (const auto& guard : guards) if (!GuardMatches(base, guard)) return false;
    // This optional value is local game configuration, not platform state.
    char language[32] = {};
    const DWORD length = GetEnvironmentVariableA("WEPLAY_STANDALONE_LANGUAGE", language, sizeof(language));
    if (length >= sizeof(language)) return false;
    if (length) {
        for (DWORD i = 0; i < length; ++i)
            if (language[i] < 'a' || language[i] > 'z') return false;
        memcpy(local_language, language, length + 1);
    }
    local_base = base;
    local_owner = owner;
    after_init = base + 0x4c847d - 0x400000;
    after_language = base + 0x4c8c0a - 0x400000;
    Write writes[] = {
        {base + 0x4c8434 - 0x400000, {}, {}, 6},
        {base + 0x4c8ade - 0x400000, {}, {}, 5},
        {base + 0x4c8bf4 - 0x400000, {}, {}, 5},
    };
    const uintptr_t destinations[] = {reinterpret_cast<uintptr_t>(&InitBridge),
        base + 0x4c8b9e - 0x400000, reinterpret_cast<uintptr_t>(&LanguageBridge)};
    for (size_t i = 0; i < _countof(writes); ++i) {
        auto& write = writes[i];
        memcpy(write.original, reinterpret_cast<const void*>(write.address), write.size);
        memset(write.replacement, 0x90, write.size);
        write.replacement[0] = 0xe9;
        const uint32_t relative = static_cast<uint32_t>(destinations[i] - write.address - 5);
        memcpy(write.replacement + 1, &relative, 4);
    }
    for (size_t i = 0; i < _countof(writes); ++i) {
        if (!Put(writes[i], false)) {
            for (size_t j = 0; j <= i; ++j) if (!Put(writes[j], true)) Stop(RollbackFailure);
            local_base = 0;
            local_owner = 0;
            return false;
        }
    }
    ReportLocalStatus("local-steam-facade-installed");
    return true;
}
}
