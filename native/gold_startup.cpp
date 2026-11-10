#define WIN32_LEAN_AND_MEAN
#include "network_guard.h"
#include <windows.h>
#include <bcrypt.h>
#include <intrin.h>
#include <cstring>
#include <vector>
#include <array>
#include <atomic>
#include "gold_startup.h"

#if !defined(_M_IX86)
#error Gold startup requires the independently mapped x86 ABI.
#endif

namespace {
constexpr char ExpectedHash[] = "76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab";
constexpr UINT Failure = 0xe05370a1;
uintptr_t image = 0, ascii_append = 0, wide_assign = 0;
GoldStartup::OrbitRecord* orbit = nullptr;
std::atomic<DWORD> owner_thread{0};
std::atomic<LONG> initialized{0};
std::atomic<LONG> identity_active{0};

__declspec(noreturn) void Stop() {
    TerminateProcess(GetCurrentProcess(), Failure);
    __fastfail(7);
}
bool ValidRange(const void* pointer, size_t size, bool writable = false) {
    if (!pointer || !size) return false;
    MEMORY_BASIC_INFORMATION info{};
    if (!VirtualQuery(pointer, &info, sizeof(info)) || info.State != MEM_COMMIT
        || (info.Protect & (PAGE_GUARD | PAGE_NOACCESS))) return false;
    const uintptr_t begin = reinterpret_cast<uintptr_t>(pointer);
    if (begin + size < begin || begin + size > reinterpret_cast<uintptr_t>(info.BaseAddress) + info.RegionSize) return false;
    const DWORD protection = info.Protect & 0xff;
    return writable ? protection == PAGE_READWRITE || protection == PAGE_WRITECOPY
        || protection == PAGE_EXECUTE_READWRITE || protection == PAGE_EXECUTE_WRITECOPY
        : protection != 0 && protection != PAGE_EXECUTE;
}
void CheckOrbit() {
    if (owner_thread.load(std::memory_order_acquire) != GetCurrentThreadId()
        || !ValidRange(orbit, sizeof(*orbit)) || !GoldStartup::Empty(orbit, 1)
        || *reinterpret_cast<GoldStartup::OrbitRecord**>(image + 0xd0b58c) != orbit) Stop();
}
void __cdecl InitOrbit(GoldStartup::OrbitRecord* record) {
    if (owner_thread.load() || !ValidRange(record, sizeof(*record), true) || !GoldStartup::Empty(record, 0)) Stop();
    orbit = record;
    record->unavailable = 1;
    owner_thread.store(GetCurrentThreadId(), std::memory_order_release);
}
__declspec(naked) void InitOrbitBridge() {
    __asm {
        push edi
        call InitOrbit
        add esp, 4
        ret
    }
}
void* __cdecl OrbitGetter() {
    const auto getter = reinterpret_cast<void* (__cdecl*)()>(image + 0x10440);
    void* value = getter();
    CheckOrbit();
    if (value != orbit) Stop();
    return value;
}
void CheckFacade(void* facade) {
    CheckOrbit();
    if (!ValidRange(facade, 0x11c) || *static_cast<uintptr_t*>(facade) != image + 0xb25ec0
        || *reinterpret_cast<void**>(static_cast<unsigned char*>(facade) + 0x1c)) Stop();
}
void __stdcall UnavailableCloud(void* facade) {
    const uintptr_t caller = reinterpret_cast<uintptr_t>(_ReturnAddress()) - image;
    if (caller != 0x1824 && caller != 0x40f46) Stop();
    CheckFacade(facade);
    // There is no legacy session, cloud result or cloud callback to wait for.
}
void __cdecl LocalIdentity(void* outer, GoldStartup::AsciiString* ticket,
    GoldStartup::AsciiString* name, GoldStartup::WideString* display, uintptr_t caller) {
    CheckOrbit();
    if (caller != image + 0xe57c || identity_active.exchange(1) || !ValidRange(outer, 16)
        || *static_cast<uintptr_t*>(outer) != image + 0xb247dc) Stop();
    CheckFacade(*reinterpret_cast<void**>(static_cast<unsigned char*>(outer) + 12));
    if (!ValidRange(name, sizeof(*name), true) || !ValidRange(display, sizeof(*display), true)
        || !ValidRange(ticket, sizeof(*ticket), true) || name == ticket
        || !GoldStartup::EmptyOutputs(name, display, ticket)) Stop();
    // These are local labels. The legacy ticket stays empty and no Ubisoft
    // account, session, ownership result or credential is manufactured.
    const char* local_name = "WePlayGoldLocal";
    const wchar_t* local_display = L"WePlay Gold Local";
    __asm {
        mov eax, name
        push local_name
        call dword ptr [ascii_append]
        mov ecx, 17
        push local_display
        push display
        call dword ptr [wide_assign]
    }
    if (ticket->length || name->length != 15 || display->length != 17) Stop();
    identity_active.store(0);
}
__declspec(naked) void LocalIdentityBridge() {
    __asm {
        push ebp
        mov ebp, esp
        push dword ptr [ebp + 4]
        push dword ptr [ebp + 12]
        push dword ptr [ebp + 8]
        push ecx
        push edx
        call LocalIdentity
        add esp, 20
        pop ebp
        ret 8
    }
}

bool DiskIdentity() {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    if (!length || length >= 32768) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    bool valid = GetFileInformationByHandle(file, &info) && !info.nFileSizeHigh && info.nFileSizeLow == 14141768
        && !(info.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    valid = valid && BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0
        && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0;
    unsigned char buffer[65536]{}, digest[32]{}; DWORD read = 0;
    while (valid) {
        if (!ReadFile(file, buffer, sizeof(buffer), &read, nullptr)) { valid = false; break; }
        if (!read) break;
        valid = BCryptHashData(hash, buffer, read, 0) >= 0;
    }
    valid = valid && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    CloseHandle(file);
    char actual[65]{}; constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) { actual[i * 2] = hex[digest[i] >> 4]; actual[i * 2 + 1] = hex[digest[i] & 15]; }
    if (!valid) return false;
    if (!strcmp(actual, ExpectedHash)) return true;
#ifdef S7_GOLD_LAB_SHA256
    return !strcmp(actual, S7_GOLD_LAB_SHA256);
#else
    return false;
#endif
}
struct Patch { unsigned char* at; std::vector<unsigned char> before, after; };
bool Add(std::vector<Patch>& patches, uintptr_t rva, std::initializer_list<unsigned char> before,
    const void* replacement) {
    auto* at = reinterpret_cast<unsigned char*>(image + rva);
    std::vector<unsigned char> original(before);
    if (original.size() != 5 || original[0] != 0xe8 || memcmp(at, original.data(), original.size())) return false;
    std::vector<unsigned char> after = original;
    const uint32_t relative = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(replacement) - reinterpret_cast<uintptr_t>(at) - 5);
    memcpy(after.data() + 1, &relative, 4);
    patches.push_back({at, original, after});
    return true;
}
bool Write(const Patch& patch, bool install) {
    const auto& before = install ? patch.before : patch.after;
    const auto& after = install ? patch.after : patch.before;
    if (memcmp(patch.at, before.data(), before.size())) return false;
    DWORD old = 0, ignored = 0;
    if (!VirtualProtect(patch.at, after.size(), PAGE_EXECUTE_READWRITE, &old)) return false;
    memcpy(patch.at, after.data(), after.size());
    if (!FlushInstructionCache(GetCurrentProcess(), patch.at, after.size())
        || !VirtualProtect(patch.at, after.size(), old, &ignored)) Stop();
    if (memcmp(patch.at, after.data(), after.size())) Stop();
    return true;
}
}

// Called explicitly before the suspended game's entry point, never in DllMain.
extern "C" __declspec(dllexport) DWORD WINAPI GoldInitialize(void*) {
    if (initialized.exchange(1)) return 0;
    image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!image || !DiskIdentity()) return 0;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 || dos->e_lfanew > 4096) return 0;
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (nt->Signature != IMAGE_NT_SIGNATURE || nt->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
        || nt->OptionalHeader.SizeOfImage != 0xdc4000 || nt->FileHeader.TimeDateStamp != 0x4d8744d4) return 0;
    std::vector<Patch> patches;
    const unsigned char append_guard[] = {0x55,0x8b,0xec,0x53,0x8b,0x5d,0x08,0x56,0x57,0x8b,0xf0,0x85,0xdb,0x74,0x42};
    const unsigned char wide_guard[] = {0x55,0x8b,0xec,0x6a,0xff,0x68};
    if (memcmp(reinterpret_cast<void*>(image+0x16200),append_guard,sizeof(append_guard))
        || memcmp(reinterpret_cast<void*>(image+0x17420),wide_guard,sizeof(wide_guard))
        || *reinterpret_cast<uintptr_t*>(image+0x17426) != image+0x9d5e69) return 0;
    if (!Add(patches, 0x1042c, {0xe8,0x6f,0xfd,0xff,0xff}, InitOrbitBridge)
        || !Add(patches, 0x12b9, {0xe8,0x82,0xf1,0x00,0x00}, OrbitGetter)
        || !Add(patches, 0x181f, {0xe8,0x3c,0x81,0x04,0x00}, UnavailableCloud)
        || !Add(patches, 0x40f41, {0xe8,0x1a,0x8a,0x00,0x00}, UnavailableCloud)
        || !Add(patches, 0xe577, {0xe8,0x54,0x35,0xff,0xff}, LocalIdentityBridge)) return 0;
    auto* branch = reinterpret_cast<unsigned char*>(image + 0x12c5);
    if (branch[0] != 0x74 || branch[1] != 0x25) return 0;
    patches.push_back({branch, {0x74,0x25}, {0x75,0x25}});
    ascii_append = image + 0x16200; wide_assign = image + 0x17420;
    if (!InstallOfflineNetworkGuard()) return 0;
    for (size_t i = 0; i < patches.size(); ++i) if (!Write(patches[i], true)) {
        for (size_t undo = i; undo; --undo) if (!Write(patches[undo - 1], false)) Stop();
        return 0;
    }
    return 1;
}
