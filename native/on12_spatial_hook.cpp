// Opt-in laboratory host for native D3D12 spatial AA. No per-draw hooks.
#include "on12_frame_context.h"
#include "water_on12_identity.h"
#include "on12_spatial_shader.generated.h"
#include <d3dcompiler.h>
#include <wrl/client.h>
#include <vector>
#include <sstream>

namespace {
using namespace WaterOn12Identity;
using Microsoft::WRL::ComPtr;
constexpr char GetterName[] = "?GetD3DDevice@VVideo@@SAPAUIDirect3DDevice9@@XZ";
struct Config { DWORD size, version, flags; wchar_t report[512]; };
static_assert(sizeof(Config) == 1036, "Spatial host ABI");
using Getter = IDirect3DDevice9* (__cdecl*)();
using Present = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, const RECT*, const RECT*, HWND, const RGNDATA*);
using Reset = HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*, D3DPRESENT_PARAMETERS*);
struct Slot { void** address; void* original; void* replacement; };
Getter original_getter = nullptr;
Present original_present = nullptr; Reset original_reset = nullptr;
HMODULE wrapper = nullptr, apphelp = nullptr;
IDirect3DDevice9* admitted = nullptr; DWORD owner = 0;
// Process-lifetime host: never run a COM destructor after renderer DLL teardown.
On12Graphics::FrameContext* context = nullptr;
On12Graphics::SpatialSettings settings;
std::vector<Slot> getter_slots, device_slots;
std::wstring report_path, directory;
DWORD failure = 0, sequence = 0; HRESULT last_result = S_OK;
UINT width = 0, height = 0;
std::uint64_t presents = 0, resets = 0, errors = 0, original_calls = 0;
thread_local bool internal = false;
struct Scope { Scope() { internal = true; } ~Scope() { internal = false; } };

bool Patch(const Slot& s, bool forward) {
    DWORD previous = 0, unused = 0;
    if (!VirtualProtect(s.address, sizeof(void*), PAGE_READWRITE, &previous)) return false;
    void* before = forward ? s.original : s.replacement; void* after = forward ? s.replacement : s.original;
    const bool changed = InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(s.address), after, before) == before;
    const bool restored = VirtualProtect(s.address, sizeof(void*), previous, &unused) != FALSE;
    if (changed && !restored) { InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(s.address), before, after); VirtualProtect(s.address, sizeof(void*), previous, &unused); }
    return changed && restored;
}
void Live() noexcept {
    try {
    if (report_path.empty()) return;
    const auto stats = context ? context->Stats() : On12Graphics::FrameStats{};
    std::ostringstream text;
    text << "{\"format\":\"s7-on12-spatial-live-v1\",\"failure\":" << failure
        << ",\"initialized\":" << (context ? "true" : "false") << ",\"enabled\":" << settings.enabled
        << ",\"sequence\":" << sequence << ",\"width\":" << width << ",\"height\":" << height
        << ",\"presents\":" << presents << ",\"original_present_calls\":" << original_calls
        << ",\"resets\":" << resets << ",\"errors\":" << errors << ",\"last_hr\":" << static_cast<long>(last_result)
        << ",\"applied\":" << stats.applied << ",\"busy\":" << stats.busy
        << ",\"checkouts\":" << stats.checkouts << ",\"returns\":" << stats.returns
        << ",\"failed_returns\":" << stats.failed_returns << ",\"safe_for_d3d9\":" << (!context || context->SafeForD3D9() ? "true" : "false") << "}";
    const auto bytes = text.str(); const auto temporary = report_path + L".tmp";
    HANDLE file = CreateFileW(temporary.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_ALWAYS, FILE_ATTRIBUTE_NORMAL, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    DWORD count = 0; const bool okay = WriteFile(file, bytes.data(), static_cast<DWORD>(bytes.size()), &count, nullptr) && count == bytes.size(); CloseHandle(file);
    if (okay) MoveFileExW(temporary.c_str(), report_path.c_str(), MOVEFILE_REPLACE_EXISTING);
    } catch (...) { /* Optional telemetry must never escape into the game. */ }
}
void Command() {
    struct Value { DWORD size, version, sequence, enabled; } value{};
    HANDLE file = CreateFileW((directory + L"\\on12-spatial.private.command").c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return;
    LARGE_INTEGER size{}; BY_HANDLE_FILE_INFORMATION info{}; DWORD count = 0;
    const bool okay = GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        && GetFileSizeEx(file, &size) && size.QuadPart == sizeof(value) && ReadFile(file, &value, sizeof(value), &count, nullptr) && count == sizeof(value);
    CloseHandle(file);
    if (!okay || value.size != sizeof(value) || value.version != 1 || !value.sequence || value.enabled > 1) { ++errors; return; }
    if (value.sequence > sequence) { sequence = value.sequence; settings.enabled = value.enabled; }
}
HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* d, const RECT* a, const RECT* b, HWND window, const RGNDATA* region) {
    if (!internal && d == admitted && GetCurrentThreadId() == owner) {
        {
            Guard guard; Scope scope; ++presents;
            try {
                if (presents == 1 || presents % 60 == 0) Command();
                if (context && settings.enabled && !failure) {
                    ComPtr<IDirect3DSurface9> backbuffer;
                    last_result = context->NativeDevice()->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer);
                    if (SUCCEEDED(last_result)) {
                        D3DSURFACE_DESC desc{}; last_result = backbuffer->GetDesc(&desc);
                        if (SUCCEEDED(last_result)) { width = desc.Width; height = desc.Height; last_result = context->Render(backbuffer.Get(), backbuffer.Get(), settings); }
                    }
                    if (FAILED(last_result)) { ++errors; failure = 301; }
                }
            } catch (...) { ++errors; failure = 302; }
            // Never let D3D9 touch a resource still checked out after a device
            // or fence failure. A safe optional-pass failure retains Present.
            if (context && !context->SafeForD3D9()) { Live(); return D3DERR_DEVICELOST; }
            ++original_calls;
            if (presents == 1 || presents % 60 == 0 || failure) Live();
        }
    }
    return original_present(d, a, b, window, region);
}
HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* d, D3DPRESENT_PARAMETERS* parameters) {
    if (!internal && d == admitted) {
        Guard guard; Scope scope;
        if (GetCurrentThreadId() != owner) return D3DERR_INVALIDCALL;
        if (context) { const HRESULT drained = context->Drain(5000); if (FAILED(drained)) { ++errors; last_result = drained; Live(); return D3DERR_DEVICELOST; } }
        ++resets;
    }
    return original_reset(d, parameters);
}
HRESULT Attach(IDirect3DDevice9* device) {
    if (!device || admitted || failure || !Range(device, sizeof(void*))) return E_UNEXPECTED;
    auto** table = *reinterpret_cast<void***>(device);
    if (!Range(table, 119 * sizeof(void*)) || !Belongs(table, wrapper)) return E_NOINTERFACE;
    // The water snapshot must have detached first. No chaining an unknown
    // hook, foreign renderer, modified table, or unqualified apphelp shim.
    for (size_t i = 0; i < 119; ++i) {
        MEMORY_BASIC_INFORMATION memory{};
        if (!table[i] || VirtualQuery(table[i], &memory, sizeof(memory)) != sizeof(memory) || memory.Type != MEM_IMAGE || memory.State != MEM_COMMIT
            || (memory.Protect & (PAGE_NOACCESS | PAGE_GUARD)) || !(memory.Protect & (PAGE_EXECUTE | PAGE_EXECUTE_READ | PAGE_EXECUTE_READWRITE | PAGE_EXECUTE_WRITECOPY))
            || (memory.AllocationBase != wrapper && !(apphelp && memory.AllocationBase == apphelp && (i == 0 || i == 2 || i == 13 || i == 16)))) return E_NOINTERFACE;
    }
    if (!Module(GetModuleHandleW(L"d3d9on12.dll"), "24aceac4aa280d09f1ec2c4c9a2b3d784bf158fe1f0f23292ab1072db9187364")
        && !Module(GetModuleHandleW(L"d3d9on12.dll"), "8d79e366f97ea0236e57b3d597626c7072b67adc182a9780548e9e3926aaff9a")) return E_NOINTERFACE;
    ComPtr<ID3DBlob> vs, ps, compiler_error;
    constexpr UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    HRESULT hr = D3DCompile(On12SpatialShader, sizeof(On12SpatialShader) - 1, "on12_spatial_pass.hlsl", nullptr, nullptr, "VS", "vs_5_0", flags, 0, &vs, &compiler_error);
    if (FAILED(hr)) return hr;
    compiler_error.Reset(); hr = D3DCompile(On12SpatialShader, sizeof(On12SpatialShader) - 1, "on12_spatial_pass.hlsl", nullptr, nullptr, "PS", "ps_5_0", flags, 0, &ps, &compiler_error);
    if (FAILED(hr)) return hr;
    auto candidate = std::unique_ptr<On12Graphics::FrameContext>(new (std::nothrow) On12Graphics::FrameContext); if (!candidate) return E_OUTOFMEMORY;
    hr = candidate->Initialize(device, vs->GetBufferPointer(), vs->GetBufferSize(), ps->GetBufferPointer(), ps->GetBufferSize()); if (FAILED(hr)) return hr;
    device_slots = {{&table[17], table[17], reinterpret_cast<void*>(HookPresent)}, {&table[16], table[16], reinterpret_cast<void*>(HookReset)}};
    original_present = reinterpret_cast<Present>(table[17]); original_reset = reinterpret_cast<Reset>(table[16]);
    size_t patched = 0;
    for (const auto& slot : device_slots) { if (!Patch(slot, true)) { while (patched) Patch(device_slots[--patched], false); device_slots.clear(); return E_FAIL; } ++patched; }
    admitted = device; owner = GetCurrentThreadId(); context = candidate.release(); settings.enabled = 1;
    for (auto it = getter_slots.rbegin(); it != getter_slots.rend(); ++it) if (!Patch(*it, false)) { failure = 203; return E_FAIL; }
    Live(); return S_OK;
}
IDirect3DDevice9* __cdecl HookGetter() {
    IDirect3DDevice9* device = original_getter();
    if (!internal && !admitted && !failure) {
        Guard guard; Scope scope;
        try { last_result = Attach(device); if (FAILED(last_result)) failure = 201; Live(); }
        catch (...) { failure = 202; Live(); }
    }
    return device;
}
bool Import(HMODULE module, std::vector<Slot>& slots) {
    auto* image = reinterpret_cast<unsigned char*>(module); const auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if (!Range(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 || dos->e_lfanew > 4096) return false;
    const auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (!Range(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    const DWORD size = nt->OptionalHeader.SizeOfImage;
    auto valid = [&](DWORD rva, size_t bytes) { return rva < size && bytes <= size - rva && Range(image + rva, bytes); };
    const auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; if (!valid(imports.VirtualAddress, imports.Size)) return false;
    auto* entries = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + imports.VirtualAddress);
    for (size_t n = 0; n < imports.Size / sizeof(*entries) && entries[n].Name; ++n) {
        const auto& e = entries[n]; if (!valid(e.Name, 12)) return false;
        if (_stricmp(reinterpret_cast<char*>(image + e.Name), "vbase90.dll")) continue;
        if (!e.OriginalFirstThunk) return false;
        for (DWORD i = 0; ; ++i) {
            const DWORD offset = i * sizeof(IMAGE_THUNK_DATA32);
            if (!valid(e.OriginalFirstThunk + offset, 4) || !valid(e.FirstThunk + offset, 4)) return false;
            const DWORD thunk = reinterpret_cast<IMAGE_THUNK_DATA32*>(image + e.OriginalFirstThunk + offset)->u1.AddressOfData;
            if (!thunk) break; if (IMAGE_SNAP_BY_ORDINAL32(thunk)) continue;
            if (!valid(thunk, sizeof(WORD) + sizeof(GetterName))) return false;
            const auto* name = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image + thunk);
            if (std::memcmp(name->Name, GetterName, sizeof(GetterName))) continue;
            auto** slot = reinterpret_cast<void**>(image + e.FirstThunk + offset);
            if (*slot != reinterpret_cast<void*>(original_getter)) return false;
            slots.push_back({slot, *slot, reinterpret_cast<void*>(HookGetter)});
        }
    }
    return true;
}
}

extern "C" DWORD WINAPI WePlayOn12SpatialInitialize(void* opaque) {
    Guard guard;
    try {
        Config c{}; SIZE_T read = 0;
        if (!opaque || !ReadProcessMemory(GetCurrentProcess(), opaque, &c, sizeof(c), &read) || read != sizeof(c)
            || c.size != sizeof(c) || c.version != 1 || c.flags != 1) return 0;
        const size_t length = wcsnlen_s(c.report, 512); constexpr wchar_t suffix[] = L"\\on12-spatial-live.private.json";
        if (length < 40 || length >= 512 || c.report[1] != L':' || c.report[2] != L'\\'
            || std::wstring(c.report).find(L"\\..") != std::wstring::npos || std::wstring(c.report).find(L"\\.\\") != std::wstring::npos
            || length < wcslen(suffix) || wcscmp(c.report + length - wcslen(suffix), suffix) || !Parents(c.report)
            || GetFileAttributesW(c.report) != INVALID_FILE_ATTRIBUTES) return 101;
        if (original_getter || !Module(GetModuleHandleW(nullptr), Game) || !Module(GetModuleHandleW(L"vbase90.dll"), Base)
            || !Module(GetModuleHandleW(L"vision90.dll"), Vision)) return 102;
        wchar_t image[512]{}; const DWORD count = GetModuleFileNameW(nullptr, image, 512); if (!count || count >= 512) return 103;
        const auto split = std::wstring(image).find_last_of(L'\\'); if (split == std::wstring::npos) return 103;
        const auto bin = std::wstring(image).substr(0, split);
        wrapper = GetModuleHandleW((bin + L"\\dxwrapper.dll").c_str());
        if (!Module(wrapper, Wrapper) || !Module(GetModuleHandleW((bin + L"\\d3d9.dll").c_str()), Stub)) return 104;
        apphelp = GetModuleHandleW(L"apphelp.dll");
        if (apphelp && !Module(apphelp, "34655b97cbcaa5b93733da1d9118e0663735c442eb0016671a98389f3cde863e")) return 105;
        original_getter = reinterpret_cast<Getter>(GetProcAddress(GetModuleHandleW(L"vbase90.dll"), GetterName)); if (!original_getter) return 106;
        std::vector<Slot> slots; if (!Import(GetModuleHandleW(nullptr), slots) || !Import(GetModuleHandleW(L"vision90.dll"), slots) || slots.empty()) return 107;
        HMODULE self = nullptr;
        if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_PIN, reinterpret_cast<const wchar_t*>(WePlayOn12SpatialInitialize), &self)) return 108;
        report_path = c.report; directory = report_path.substr(0, report_path.find_last_of(L'\\'));
        getter_slots = slots;
        size_t patched = 0;
        for (const auto& slot : slots) { if (!Patch(slot, true)) { while (patched) Patch(slots[--patched], false); return 109; } ++patched; }
        Live(); return 1;
    } catch (...) { return 110; }
}

#ifdef S7_ON12_SPATIAL_GPU_FIXTURE
extern "C" HRESULT On12SpatialFixtureAttach(IDirect3DDevice9* device) {
    Guard guard; wrapper = GetModuleHandleW(L"dxwrapper.dll");
    if (!Module(wrapper, Wrapper)) return E_NOINTERFACE;
    apphelp = GetModuleHandleW(L"apphelp.dll"); return Attach(device);
}
extern "C" bool On12SpatialFixtureClose() {
    Guard guard; if (!context || presents != 1 || original_calls != 1 || resets != 1 || failure || errors || context->Stats().applied != 1) return false;
    if (FAILED(context->Shutdown(5000))) return false;
    for (auto it = device_slots.rbegin(); it != device_slots.rend(); ++it) if (!Patch(*it, false)) return false;
    delete context; context = nullptr; return true;
}
#endif
