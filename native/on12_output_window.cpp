#include "on12_output_window.h"
#include "water_on12_identity.h"
#include <vector>
#include <atomic>
#include <limits>
#include <cstdio>

namespace On12Graphics {
namespace {
using WaterOn12Identity::Range;
using WaterOn12Identity::Belongs;
struct Slot { void** address; void* before; void* after; };
struct State {
    HWND window = nullptr; DWORD owner = 0, controller = 0; WNDPROC original = nullptr;
    UINT sw = 0, sh = 0, ow = 0, oh = 0, message = 0;
    bool (*busy)() = nullptr;
    std::vector<HMODULE> engine;
    std::vector<Slot> slots;
    std::atomic_bool active{false}, pending_resize{true}; bool resizing = false;
    decltype(&::GetClientRect) rectangle = nullptr;
    decltype(&::ScreenToClient) to_client = nullptr;
    decltype(&::ClientToScreen) to_screen = nullptr;
};
// Published once, retained after shutdown: an engine call already entering a
// restored import must still have a valid trampoline and immutable metadata.
State* state = nullptr;
constexpr UINT_PTR Timer = 0x37533132;
constexpr LPARAM Token = 0x37533132;
bool Owned(const State& s) {
    DWORD pid = 0; return IsWindow(s.window) && GetWindowThreadProcessId(s.window, &pid) == s.owner && pid == GetCurrentProcessId();
}
bool Ready(const State& s) {
    RECT r{}; return s.active.load(std::memory_order_acquire) && Owned(s) && !IsIconic(s.window)
        && s.rectangle(s.window, &r) && r.left == 0 && r.top == 0 && r.right == static_cast<LONG>(s.ow) && r.bottom == static_cast<LONG>(s.oh);
}
bool Caller(const State& s, const void* caller) {
    for (auto module : s.engine) if (Belongs(caller, module)) return true;
    return false;
}
bool Patch(const Slot& slot, bool forward) {
    DWORD old = 0, unused = 0;
    if (!VirtualProtect(slot.address, sizeof(void*), PAGE_READWRITE, &old)) return false;
    void* before = forward ? slot.before : slot.after; void* after = forward ? slot.after : slot.before;
    bool okay = InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(slot.address), after, before) == before;
    if (!VirtualProtect(slot.address, sizeof(void*), old, &unused)) {
        if (okay) InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(slot.address), before, after);
        VirtualProtect(slot.address, sizeof(void*), old, &unused); okay = false;
    }
    return okay;
}
BOOL WINAPI Rectangle(HWND window, RECT* result) {
    auto& s = *state; const BOOL okay = s.rectangle(window, result); const DWORD error = GetLastError();
    if (okay && window == s.window && s.active.load(std::memory_order_acquire) && Caller(s, _ReturnAddress()) && Owned(s)
        && (s.pending_resize.load(std::memory_order_acquire) || Ready(s)))
        *result = {0, 0, static_cast<LONG>(s.sw), static_cast<LONG>(s.sh)};
    SetLastError(error); return okay;
}
BOOL WINAPI ToClient(HWND window, POINT* point) {
    auto& s = *state; const BOOL okay = s.to_client(window, point); const DWORD error = GetLastError();
    if (okay && window == s.window && Caller(s, _ReturnAddress()) && Ready(s)) {
        POINT value = *point; if (!ScalePoint(value, s.ow, s.oh, s.sw, s.sh)) { SetLastError(ERROR_ARITHMETIC_OVERFLOW); return FALSE; } *point = value;
    }
    SetLastError(error); return okay;
}
BOOL WINAPI ToScreen(HWND window, POINT* point) {
    auto& s = *state;
    if (window != s.window || !Caller(s, _ReturnAddress()) || !Ready(s) || !Range(point, sizeof(*point))) return s.to_screen(window, point);
    POINT value = *point; if (!ScalePoint(value, s.sw, s.sh, s.ow, s.oh)) { SetLastError(ERROR_ARITHMETIC_OVERFLOW); return FALSE; }
    const BOOL okay = s.to_screen(window, &value); if (okay) *point = value; return okay;
}
bool Mouse(UINT message) {
    return message == WM_MOUSEMOVE || (message >= WM_LBUTTONDOWN && message <= WM_MBUTTONDBLCLK)
        || message == WM_XBUTTONDOWN || message == WM_XBUTTONUP || message == WM_XBUTTONDBLCLK;
}
LRESULT CALLBACK Procedure(HWND window, UINT message, WPARAM wparam, LPARAM lparam) {
    auto& s = *state;
    if (window == s.window && s.active.load(std::memory_order_acquire)) {
        if ((message == s.message && wparam == GetCurrentProcessId() && lparam == Token)
            || (message == WM_TIMER && wparam == Timer)) {
            if (s.busy && s.busy()) { SetTimer(window, Timer, 50, nullptr); return 0; }
            KillTimer(window, Timer);
            RECT r{0, 0, static_cast<LONG>(s.ow), static_cast<LONG>(s.oh)};
            if (Owned(s) && AdjustWindowRectEx(&r, static_cast<DWORD>(GetWindowLongPtrW(window, GWL_STYLE)), GetMenu(window) != nullptr,
                                              static_cast<DWORD>(GetWindowLongPtrW(window, GWL_EXSTYLE)))) {
                s.resizing = true;
                SetWindowPos(window, nullptr, 0, 0, r.right - r.left, r.bottom - r.top, SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER);
                s.resizing = false;
            }
            s.pending_resize.store(false, std::memory_order_release);
            return 0;
        }
        if (message == WM_SIZE && wparam != SIZE_MINIMIZED && (s.resizing || Ready(s))) lparam = MAKELPARAM(s.sw, s.sh);
        else if (Mouse(message) && Ready(s)) {
            POINT p{static_cast<short>(LOWORD(lparam)), static_cast<short>(HIWORD(lparam))};
            if (ScalePoint(p, s.ow, s.oh, s.sw, s.sh) && p.x >= -32768 && p.x <= 32767 && p.y >= -32768 && p.y <= 32767)
                lparam = MAKELPARAM(static_cast<WORD>(p.x), static_cast<WORD>(p.y));
        }
        if (message == WM_DESTROY || message == WM_NCDESTROY) s.active.store(false, std::memory_order_release);
    }
    return CallWindowProcW(s.original, window, message, wparam, lparam);
}
bool Imports(HMODULE module, State& s) {
    auto* image = reinterpret_cast<BYTE*>(module); auto* dos = reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if (!Range(dos, sizeof(*dos)) || dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 || dos->e_lfanew > 4096) return false;
    auto* nt = reinterpret_cast<IMAGE_NT_HEADERS32*>(image + dos->e_lfanew);
    if (!Range(nt, sizeof(*nt)) || nt->Signature != IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    const DWORD size = nt->OptionalHeader.SizeOfImage;
    auto valid = [&](DWORD rva, size_t count) { return rva < size && count <= size - rva && Range(image + rva, count); };
    auto imports = nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT]; if (!valid(imports.VirtualAddress, imports.Size)) return false;
    auto* entries = reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image + imports.VirtualAddress);
    for (size_t n = 0; n < imports.Size / sizeof(*entries) && entries[n].Name; ++n) {
        const auto& e = entries[n]; if (!valid(e.Name, 11)) return false;
        if (_stricmp(reinterpret_cast<char*>(image + e.Name), "USER32.dll")) continue;
        if (!e.OriginalFirstThunk) return false;
        for (DWORD i = 0; ; ++i) {
            const DWORD offset = i * sizeof(IMAGE_THUNK_DATA32);
            if (!valid(e.OriginalFirstThunk + offset, 4) || !valid(e.FirstThunk + offset, 4)) return false;
            const DWORD thunk = reinterpret_cast<IMAGE_THUNK_DATA32*>(image + e.OriginalFirstThunk + offset)->u1.AddressOfData;
            if (!thunk) break; if (IMAGE_SNAP_BY_ORDINAL32(thunk)) continue;
            if (!valid(thunk, 18)) return false;
            const auto* entry = reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image + thunk);
            // Bound the name before comparison, including a terminating NUL.
            size_t length = 0; for (; length < 64 && valid(thunk + 2 + static_cast<DWORD>(length), 1) && entry->Name[length]; ++length) {}
            if (length == 64 || !valid(thunk + 2 + static_cast<DWORD>(length), 1)) return false;
            void* before = nullptr; void* after = nullptr;
            if (!strcmp(entry->Name, "GetClientRect")) { before = reinterpret_cast<void*>(s.rectangle); after = reinterpret_cast<void*>(Rectangle); }
            if (!strcmp(entry->Name, "ScreenToClient")) { before = reinterpret_cast<void*>(s.to_client); after = reinterpret_cast<void*>(ToClient); }
            if (!strcmp(entry->Name, "ClientToScreen")) { before = reinterpret_cast<void*>(s.to_screen); after = reinterpret_cast<void*>(ToScreen); }
            if (!before) continue;
            auto** slot = reinterpret_cast<void**>(image + e.FirstThunk + offset);
            if (*slot != before) {
#ifdef S7_ON12_SPATIAL_GPU_FIXTURE
                MEMORY_BASIC_INFORMATION m{}; VirtualQuery(*slot, &m, sizeof(m)); wchar_t path[512]{};
                GetModuleFileNameW(static_cast<HMODULE>(m.AllocationBase), path, 512);
                std::fwprintf(stderr, L"window import differs: %hs owner=%ls type=%lu\n", entry->Name, path, m.Type);
#endif
                return false;
            }
            s.slots.push_back({slot, before, after});
        }
    }
    return true;
}
}
bool ScalePoint(POINT& point, UINT fw, UINT fh, UINT tw, UINT th) {
    if (!fw || !fh || !tw || !th || fw > 16384 || fh > 16384 || tw > 16384 || th > 16384) return false;
    auto scale = [](LONG v, UINT numerator, UINT denominator, LONG& output) {
        long long result = static_cast<long long>(v) * numerator;
        result += (result < 0 ? -1 : 1) * static_cast<long long>(denominator / 2); result /= denominator;
        if (result < (std::numeric_limits<LONG>::min)() || result > (std::numeric_limits<LONG>::max)()) return false;
        output = static_cast<LONG>(result); return true;
    };
    POINT result{}; if (!scale(point.x, tw, fw, result.x) || !scale(point.y, th, fh, result.y)) return false; point = result; return true;
}
HRESULT OutputWindow::Initialize(HWND window, UINT sw, UINT sh, UINT ow, UINT oh, const HMODULE* engine, size_t count, bool (*busy)()) {
    if (state || !window || !engine || !count || count > 4 || !sw || !sh || ow < sw || oh < sh || ow > 4096 || oh > 4096
        || static_cast<unsigned long long>(sw) * oh != static_cast<unsigned long long>(sh) * ow) return E_INVALIDARG;
    auto candidate = new (std::nothrow) State; if (!candidate) return E_OUTOFMEMORY;
    state = candidate; auto& s = *state; s.window = window; s.controller = GetCurrentThreadId();
    DWORD window_pid = 0; s.owner = GetWindowThreadProcessId(window, &window_pid);
    s.sw = sw; s.sh = sh; s.ow = ow; s.oh = oh; s.busy = busy;
    if (!Owned(s)) return RPC_E_WRONG_THREAD;
    s.original = reinterpret_cast<WNDPROC>(GetWindowLongPtrW(window, GWLP_WNDPROC));
    s.engine.assign(engine, engine + count);
    // dxwrapper legitimately subclasses the window before the device exists.
    // Chain only that exact pinned wrapper, never an arbitrary foreign hook.
    const HMODULE wrapper = GetModuleHandleW(L"dxwrapper.dll");
    const bool wrapped = wrapper && Belongs(reinterpret_cast<void*>(s.original), wrapper)
        && WaterOn12Identity::Module(wrapper, WaterOn12Identity::Wrapper);
    if (!s.original || (!Caller(s, reinterpret_cast<void*>(s.original)) && !wrapped)) {
#ifdef S7_ON12_SPATIAL_GPU_FIXTURE
        MEMORY_BASIC_INFORMATION m{}; VirtualQuery(reinterpret_cast<void*>(s.original), &m, sizeof(m)); wchar_t path[512]{};
        GetModuleFileNameW(static_cast<HMODULE>(m.AllocationBase), path, 512);
        std::fwprintf(stderr, L"window procedure refused: owner=%ls type=%lu\n", path, m.Type);
#endif
        return E_NOINTERFACE;
    }
    auto user32 = GetModuleHandleW(L"user32.dll");
    s.rectangle = reinterpret_cast<decltype(s.rectangle)>(GetProcAddress(user32, "GetClientRect"));
    s.to_client = reinterpret_cast<decltype(s.to_client)>(GetProcAddress(user32, "ScreenToClient"));
    s.to_screen = reinterpret_cast<decltype(s.to_screen)>(GetProcAddress(user32, "ClientToScreen"));
    if (!s.rectangle || !s.to_client || !s.to_screen) return E_NOINTERFACE;
    for (size_t i = 0; i < count; ++i) if (!Imports(engine[i], s)) return E_NOINTERFACE;
    if (s.slots.empty()) return E_NOINTERFACE;
    s.message = RegisterWindowMessageW(L"WePlay.On12.OutputWindow.v1"); if (!s.message) return E_FAIL;
    size_t applied = 0;
    for (const auto& slot : s.slots) { if (!Patch(slot, true)) { while (applied) Patch(s.slots[--applied], false); return E_FAIL; } ++applied; }
    SetLastError(0); const LONG_PTR previous = SetWindowLongPtrW(window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(Procedure));
    if (previous != reinterpret_cast<LONG_PTR>(s.original)) {
        if (previous) SetWindowLongPtrW(window, GWLP_WNDPROC, previous);
        while (applied) Patch(s.slots[--applied], false); return E_FAIL;
    }
    s.active.store(true, std::memory_order_release);
    if (!PostMessageW(window, s.message, GetCurrentProcessId(), Token)) { Shutdown(); return E_FAIL; }
    return S_OK;
}
bool OutputWindow::Ready() const { return state && On12Graphics::Ready(*state); }
HRESULT OutputWindow::Shutdown() {
    if (!state) return S_OK; auto& s = *state;
    if (GetCurrentThreadId() != s.controller) return RPC_E_WRONG_THREAD;
    s.active.store(false, std::memory_order_release); bool okay = true;
    if (IsWindow(s.window)) {
        KillTimer(s.window, Timer);
        if (GetWindowLongPtrW(s.window, GWLP_WNDPROC) == reinterpret_cast<LONG_PTR>(Procedure))
            okay = SetWindowLongPtrW(s.window, GWLP_WNDPROC, reinterpret_cast<LONG_PTR>(s.original)) == reinterpret_cast<LONG_PTR>(Procedure);
        else okay = false;
    }
    for (auto it = s.slots.rbegin(); it != s.slots.rend(); ++it) if (*it->address == it->after && !Patch(*it, false)) okay = false;
    return okay ? S_OK : E_FAIL;
}
}
