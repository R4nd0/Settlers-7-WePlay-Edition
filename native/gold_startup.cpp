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
#ifdef S7_GOLD_LOCAL_SIMULATION
// Experimental, single-player-only ownership of the local message handler.
// No legacy session, cloud callback, packet acknowledgement or clock is created.
std::atomic<LONG> local_step_active{0};
uintptr_t local_core = 0;
using NativeReady = bool (__thiscall*)(void*, unsigned int, unsigned char*, unsigned int, bool);
using NativeAdvance = bool (__stdcall*)(void*);
NativeReady local_ready = nullptr;
NativeAdvance local_advance = nullptr;
using NativeTaskChoice = void* (__stdcall*)(void*, unsigned int, void*);
NativeTaskChoice local_choice_transport = nullptr, local_choice_alternative = nullptr, local_choice_fallback = nullptr;
using ExistingTaskChoice = void (__stdcall*)(void*, void*);
void __stdcall ExistingGoldTaskChoice(void* task, void* output) {
    const uintptr_t target = image + 0x548940;
    __asm {
        push esi
        mov esi, output
        push task
        call target
        pop esi
    }
}
ExistingTaskChoice local_choice_existing = ExistingGoldTaskChoice;
uintptr_t LocalCore(void* outer) {
    CheckOrbit();
    const uintptr_t owner = reinterpret_cast<uintptr_t>(outer);
    if (!ValidRange(outer, 0x1c) || *reinterpret_cast<uintptr_t*>(owner) != image + 0xb247dc
        || *reinterpret_cast<uintptr_t*>(owner + 0x10)) Stop();
    CheckFacade(*reinterpret_cast<void**>(owner + 0xc));
    const uintptr_t game = *reinterpret_cast<uintptr_t*>(image + 0xcf8310);
    if (!ValidRange(reinterpret_cast<void*>(game), 0x190)
        || *reinterpret_cast<uintptr_t*>(game) != image + 0xb32870
        || *reinterpret_cast<uintptr_t*>(game + 0xcc) != owner
        || *reinterpret_cast<unsigned int*>(game + 0x184) != 2) Stop();
    const auto network_mode = reinterpret_cast<bool (__cdecl*)()>(image + 0x58c910);
    if (network_mode()) Stop();
    const uintptr_t backend = *reinterpret_cast<uintptr_t*>(owner + 8);
    if (!ValidRange(reinterpret_cast<void*>(backend), 12)
        || *reinterpret_cast<uintptr_t*>(backend) != image + 0xb28b74) Stop();
    const uintptr_t core = *reinterpret_cast<uintptr_t*>(backend + 8);
    if (!ValidRange(reinterpret_cast<void*>(core), 0x200, true)
        || *reinterpret_cast<uintptr_t*>(core) != image + 0xb29058
        || *reinterpret_cast<uintptr_t*>(core + 4) != image + 0xb29064) Stop();
    return core;
}
__declspec(noinline) bool __fastcall LocalReady(void* outer, void*, unsigned int turn,
    unsigned char* ready, unsigned int delay, bool network) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != image + 0x1a6a98
        || network || delay != 18 || !ValidRange(ready, 1, true) || local_step_active.exchange(1)) Stop();
    const uintptr_t core = LocalCore(outer);
    // The same local preparation performed by History's message-handler entry.
    // Native Gold still computes the result, processes its queues and reports
    // readiness; the wrapper never writes the output or increments a turn.
    *reinterpret_cast<unsigned char*>(core + 0x16) = 0;
    *reinterpret_cast<unsigned char*>(core + 0x3a) = 0;
    *reinterpret_cast<unsigned char*>(core + 0x58) = 1;
    local_core = core;
    const uintptr_t backend = *reinterpret_cast<uintptr_t*>(static_cast<unsigned char*>(outer) + 8);
    const bool result = local_ready(reinterpret_cast<void*>(backend), turn, ready, delay, network);
    local_step_active.store(0);
    return result;
}
__declspec(noinline) bool __stdcall LocalAdvance(void* core_pointer) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != image + 0x40ed9 || local_step_active.exchange(1)) Stop();
    const uintptr_t game = *reinterpret_cast<uintptr_t*>(image + 0xcf8310);
    if (!ValidRange(reinterpret_cast<void*>(game), 0xd0)) Stop();
    const uintptr_t core = LocalCore(*reinterpret_cast<void**>(game + 0xcc));
    if (core != reinterpret_cast<uintptr_t>(core_pointer) || core != local_core) Stop();
    *reinterpret_cast<unsigned char*>(core + 0x16) = 0;
    *reinterpret_cast<unsigned char*>(core + 0x58) = 1;
    *reinterpret_cast<unsigned char*>(core + 0x1f0) = 1;
    const bool result = local_advance(core_pointer);
    local_step_active.store(0);
    return result;
}
// Gold already contains the actual transport searches. History calls these
// locally instead of suspending CRequestTask until an old controller answers.
// Enter only after Gold's owner lookup and preferred-parent-result checks.
__declspec(noinline) void __stdcall LocalTaskChoice(void* task, void* output, unsigned int owner, uintptr_t caller) {
    if (caller != image + 0x549444 || local_step_active.exchange(1)) Stop();
    const uintptr_t game = *reinterpret_cast<uintptr_t*>(image + 0xcf8310);
    if (!ValidRange(reinterpret_cast<void*>(game), 0xd0)) Stop();
    LocalCore(*reinterpret_cast<void**>(game + 0xcc));
    const auto address = reinterpret_cast<uintptr_t>(task);
    if (!ValidRange(task, 0xe8, true) || !ValidRange(output, 44, true)
        || *reinterpret_cast<uintptr_t*>(address) != image + 0xb4c684
        || *reinterpret_cast<unsigned int*>(address + 0xb4) != 0) Stop();
    // Native Gold has already searched the parent case. History also searches
    // existing standalone transport before allocating a new transport.
    if (!*reinterpret_cast<uintptr_t*>(address + 0x38)) local_choice_existing(task, output);
    auto* result = static_cast<unsigned int*>(output);
    if (!result[2]) {
        local_choice_transport(task, owner, output);
        std::array<unsigned int, 11> candidate{};
        candidate[3] = candidate[4] = 0x7f7fffff;
        local_choice_alternative(task, owner, candidate.data());
        float current_cost = 0, candidate_cost = 0;
        memcpy(&current_cost, result + 3, sizeof(float));
        memcpy(&candidate_cost, candidate.data() + 3, sizeof(float));
        if (candidate_cost < current_cost) memcpy(output, candidate.data(), 44);
        if (!result[2]) local_choice_fallback(task, owner, output);
    }
    local_step_active.store(0);
}
__declspec(naked) void LocalTaskBridge() {
    __asm {
        mov eax, [esp + 0x20]
        push dword ptr [esp]
        push eax
        push ebx
        push esi
        call LocalTaskChoice
        ret
    }
}
bool LocalSimulationPatches(std::vector<Patch>& patches) {
    // Guard each independently mapped native search entry as well as the
    // caller. A correct disk hash alone cannot detect another in-memory hook.
    const unsigned char existing_entry[] = {0x55,0x8b,0xec,0x83,0xec,0x0c};
    const unsigned char transport_entry[] = {0x55,0x8b,0xec,0x83,0xe4,0xf8,0x6a,0xff};
    const unsigned char fallback_entry[] = {0x55,0x8b,0xec,0x81,0xec,0xd0,0x00,0x00,0x00};
    if (!ValidRange(reinterpret_cast<void*>(image + 0x54969a), 12)
        || memcmp(reinterpret_cast<void*>(image + 0x548940), existing_entry, sizeof(existing_entry))
        || memcmp(reinterpret_cast<void*>(image + 0x548a50), transport_entry, sizeof(transport_entry))
        || memcmp(reinterpret_cast<void*>(image + 0x548d60), transport_entry, sizeof(transport_entry))
        || memcmp(reinterpret_cast<void*>(image + 0x548250), fallback_entry, sizeof(fallback_entry))) return false;
    const uintptr_t original = image + 0x1470;
    const uintptr_t replacement = reinterpret_cast<uintptr_t>(LocalReady);
    if (*reinterpret_cast<uintptr_t*>(image + 0xb28b78) != image + 0x40ec0
        || *reinterpret_cast<uintptr_t*>(image + 0xb28b7c) != image + 0x40ed0
        || !AddBytes(patches, 0xb247dc, &original, &replacement, 4)) return false;
    local_ready = reinterpret_cast<NativeReady>(image + 0x40ec0);
    local_advance = reinterpret_cast<NativeAdvance>(image + 0x42b30);
    const unsigned char task_before[] = {0xb8,0x01,0x00,0x00,0x00,0x39,0x45,0x10,0x0f,0x84,0x4d,0x02,0x00,0x00};
    unsigned char task_after[sizeof(task_before)]{};
    memset(task_after, 0x90, sizeof(task_after));
    task_after[0] = 0xe8;
    const uint32_t bridge_offset = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(LocalTaskBridge) - (image + 0x549444));
    memcpy(task_after + 1, &bridge_offset, 4);
    task_after[5] = 0xe9;
    const uint32_t return_offset = 0x54969a - 0x549449;
    memcpy(task_after + 6, &return_offset, 4);
    if (!AddBytes(patches, 0x54943f, task_before, task_after, sizeof(task_before))) return false;
    local_choice_transport = reinterpret_cast<NativeTaskChoice>(image + 0x548a50);
    local_choice_alternative = reinterpret_cast<NativeTaskChoice>(image + 0x548d60);
    local_choice_fallback = reinterpret_cast<NativeTaskChoice>(image + 0x548250);
    return true;
}
#endif
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
#ifdef S7_GOLD_LOCAL_SIMULATION
    if (!LocalSimulationPatches(patches)
        || !Add(patches, 0x40ed4, {0xe8,0x57,0x1c,0x00,0x00}, LocalAdvance)) return 0;
#endif
    ascii_append = image + 0x16200; wide_assign = image + 0x17420;
    if (!ModernRenderer() || !ProfileDirectory() || !InstallOfflineNetworkGuard()) return 0;
    for (size_t i = 0; i < patches.size(); ++i) if (!Write(patches[i], true)) {
        for (size_t undo = i; undo; --undo) if (!Write(patches[undo - 1], false)) Stop();
        return 0;
    }
    return 1;
}
