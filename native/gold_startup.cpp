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
#include "gold_profile.h"
#include "gold_graphics.h"
#include <shlobj.h>
#include <string>

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
GoldProfile::Names profile_names{};
HANDLE renderer_file = INVALID_HANDLE_VALUE;

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

bool FileDigest(const wchar_t* path, char (&actual)[65], DWORD expected_size = 0, HANDLE* retained = nullptr) {
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{};
    bool valid = GetFileInformationByHandle(file, &info) && !info.nFileSizeHigh
        && (!expected_size || info.nFileSizeLow == expected_size)
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
    if (valid && retained) *retained = file;
    else CloseHandle(file);
    constexpr char hex[] = "0123456789abcdef";
    for (size_t i = 0; i < 32; ++i) { actual[i * 2] = hex[digest[i] >> 4]; actual[i * 2 + 1] = hex[digest[i] & 15]; }
    actual[64] = 0;
    return valid;
}
bool DiskIdentity() {
    wchar_t path[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, path, 32768);
    char actual[65]{};
    if (!length || length >= 32768 || !FileDigest(path, actual, 14141768)) return false;
    if (!strcmp(actual, ExpectedHash)) return true;
#ifdef S7_GOLD_LAB_SHA256
    return !strcmp(actual, S7_GOLD_LAB_SHA256);
#else
    return false;
#endif
}
bool ModernRenderer() {
    wchar_t executable[32768]{};
    const DWORD length = GetModuleFileNameW(nullptr, executable, 32768);
    if (!length || length >= 32768) return false;
    const auto* slash = wcsrchr(executable, L'\\');
    if (!slash) return false;
    const std::wstring path = std::wstring(executable, static_cast<size_t>(slash - executable)) + L"\\d3d9.dll";
    char actual[65]{};
    HANDLE pinned = INVALID_HANDLE_VALUE;
    if (!FileDigest(path.c_str(), actual, 0, &pinned)) return false;
    if (strcmp(actual, GoldGraphics::RendererHash)) { CloseHandle(pinned); return false; }
    // Resolve the pinned payload before the engine can select system D3D9.
    // A conflicting preloaded D3D9 module is rejected, never used as fallback.
    HMODULE module = GetModuleHandleW(L"d3d9.dll");
    if (!module) module = LoadLibraryExW(path.c_str(), nullptr,
        LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    wchar_t loaded[32768]{};
    const DWORD count = module ? GetModuleFileNameW(module, loaded, 32768) : 0;
    bool valid = count && count < 32768 && !_wcsicmp(path.c_str(), loaded)
        && FileDigest(loaded, actual) && !strcmp(actual, GoldGraphics::RendererHash);
    HMODULE permanent = nullptr;
    valid = valid && GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN, loaded, &permanent) && permanent == module;
    if (valid) renderer_file = pinned; // Deny file replacement for this process lifetime.
    else CloseHandle(pinned);
    return valid;
}
struct Patch { unsigned char* at; std::vector<unsigned char> before, after; };
bool AddBytes(std::vector<Patch>& patches, uintptr_t rva, const void* before, const void* after, size_t size) {
    auto* at = reinterpret_cast<unsigned char*>(image + rva);
    if (!size || !ValidRange(at, size) || memcmp(at, before, size)) return false;
    const auto* original = static_cast<const unsigned char*>(before);
    const auto* replacement = static_cast<const unsigned char*>(after);
    patches.push_back({at, {original, original + size}, {replacement, replacement + size}});
    return true;
}
bool ProfilePatches(std::vector<Patch>& patches) {
    // Three independently mapped Gold path constructors append this suffix to
    // Documents. Keep their original constructors and all save semantics.
    const uintptr_t original = image + 0xb2ecf0;
    const uintptr_t replacement = reinterpret_cast<uintptr_t>(profile_names.suffix.data());
    for (uintptr_t rva : {0x133fdeu, 0x1352fbu, 0x135600u}) {
        const auto* instruction = reinterpret_cast<const unsigned char*>(image + rva);
        if (*instruction != 0x68 || !AddBytes(patches, rva + 1, &original, &replacement, 4)) return false;
    }
    constexpr char old_mutex[] = "Global\\Settlers7";
    return AddBytes(patches, 0xb24fc4, old_mutex, profile_names.mutex.data(), sizeof(old_mutex));
}
bool ProfileNames() {
    wchar_t path[32768]{};
    const DWORD module_length = GetModuleFileNameW(nullptr, path, 32768);
    if (!module_length || module_length >= 32768) return false;
    HANDLE file = CreateFileW(path, GENERIC_READ, FILE_SHARE_READ, nullptr, OPEN_EXISTING, 0, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    wchar_t final_path[32768]{};
    const DWORD length = GetFinalPathNameByHandleW(file, final_path, 32768, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    CloseHandle(file);
    if (!length || length >= 32768) return false;
    wchar_t normalized[32768]{};
    const int count = LCMapStringEx(LOCALE_NAME_INVARIANT, LCMAP_UPPERCASE, final_path,
        static_cast<int>(length), normalized, 32768, nullptr, nullptr, 0);
    if (count <= 0) return false;
    unsigned char digest[32]{};
    BCRYPT_ALG_HANDLE algorithm = nullptr; BCRYPT_HASH_HANDLE hash = nullptr;
    bool valid = BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0
        && BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0) >= 0
        && BCryptHashData(hash, reinterpret_cast<PUCHAR>(normalized), static_cast<ULONG>(count * sizeof(wchar_t)), 0) >= 0
        && BCryptFinishHash(hash, digest, sizeof(digest), 0) >= 0;
    if (hash) BCryptDestroyHash(hash);
    if (algorithm) BCryptCloseAlgorithmProvider(algorithm, 0);
    if (valid) profile_names = GoldProfile::FromDigest(digest);
    return valid;
}
bool ProfileDirectory() {
    wchar_t documents[MAX_PATH]{};
    if (SHGetFolderPathW(nullptr, CSIDL_PERSONAL, nullptr, SHGFP_TYPE_CURRENT, documents) != S_OK) return false;
    std::wstring path(documents);
    const std::string suffix(profile_names.suffix.data());
    if (path.size() + suffix.size() + wcslen(L"\\Options.ini") >= MAX_PATH) return false;
    for (size_t i = 0; i < suffix.size(); ++i) {
        path += static_cast<wchar_t>(suffix[i]);
        if (i + 1 != suffix.size() && suffix[i + 1] != '\\') continue;
        DWORD attributes = GetFileAttributesW(path.c_str());
        if (attributes == INVALID_FILE_ATTRIBUTES) {
            if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) return false;
            if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
            attributes = GetFileAttributesW(path.c_str());
        }
        if (attributes == INVALID_FILE_ATTRIBUTES || !(attributes & FILE_ATTRIBUTE_DIRECTORY)
            || (attributes & FILE_ATTRIBUTE_REPARSE_POINT)) return false;
    }
    path += L"\\Options.ini";
    HANDLE options = CreateFileW(path.c_str(), GENERIC_WRITE, FILE_SHARE_READ, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (options == INVALID_HANDLE_VALUE) {
        if (GetLastError() != ERROR_FILE_EXISTS) return false;
        const DWORD attributes = GetFileAttributesW(path.c_str());
        return attributes != INVALID_FILE_ATTRIBUTES
            && !(attributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT));
    }
    DWORD written = 0;
    const DWORD size = sizeof(GoldGraphics::Defaults) - 1;
    const bool valid = WriteFile(options, GoldGraphics::Defaults, size, &written, nullptr)
        && written == size && FlushFileBuffers(options);
    CloseHandle(options);
    return valid;
}
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
    if (!ProfileNames() || !ProfilePatches(patches)) return 0;
    ascii_append = image + 0x16200; wide_assign = image + 0x17420;
    if (!ModernRenderer() || !ProfileDirectory() || !InstallOfflineNetworkGuard()) return 0;
    for (size_t i = 0; i < patches.size(); ++i) if (!Write(patches[i], true)) {
        for (size_t undo = i; undo; --undo) if (!Write(patches[undo - 1], false)) Stop();
        return 0;
    }
    return 1;
}
