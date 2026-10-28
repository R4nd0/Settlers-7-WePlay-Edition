#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <string>
#include <vector>

static std::string utf8(const char* text) {
    const int n = MultiByteToWideChar(CP_ACP, 0, text, -1, nullptr, 0);
    if (n <= 1 || n > 1024) return {};
    std::vector<wchar_t> w(static_cast<size_t>(n));
    MultiByteToWideChar(CP_ACP, 0, text, -1, w.data(), n);
    const int m = WideCharToMultiByte(CP_UTF8, 0, w.data(), n - 1, nullptr, 0, nullptr, nullptr);
    std::string out(static_cast<size_t>(m), '\0');
    WideCharToMultiByte(CP_UTF8, 0, w.data(), n - 1, out.data(), m, nullptr, nullptr);
    return out;
}
static void json_string(const std::string& text) {
    std::putchar('"');
    for (const unsigned char c : text) {
        if (c == '"' || c == '\\') { std::putchar('\\'); std::putchar(c); }
        else if (c < 32) std::printf("\\u%04x", c);
        else std::putchar(c);
    }
    std::putchar('"');
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 2) return 3;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    HMODULE dll = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) { std::puts("{\"format\":\"s7-render-probe-v1\",\"devices\":[]}"); return 2; }
    using Create = IDirect3D9* (WINAPI*)(UINT);
    auto create = reinterpret_cast<Create>(GetProcAddress(dll, "Direct3DCreate9"));
    IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
    HWND window = CreateWindowExW(0, L"STATIC", L"S7 graphics check", WS_POPUP, -30000, -30000, 64, 64,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    std::vector<std::string> devices;
    if (d3d && window) {
        const UINT count = d3d->GetAdapterCount();
        if (count <= 16) for (UINT i = 0; i < count; ++i) {
            D3DADAPTER_IDENTIFIER9 id{};
            if (FAILED(d3d->GetAdapterIdentifier(i, 0, &id))) continue;
            D3DPRESENT_PARAMETERS pp{};
            pp.BackBufferWidth = 64; pp.BackBufferHeight = 64; pp.BackBufferFormat = D3DFMT_UNKNOWN;
            pp.BackBufferCount = 1; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            pp.hDeviceWindow = window; pp.Windowed = TRUE; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            IDirect3DDevice9* device = nullptr;
            const HRESULT result = d3d->CreateDevice(i, D3DDEVTYPE_HAL, window,
                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device);
            if (SUCCEEDED(result) && device) {
                const HRESULT clear = device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff284366, 1.0f, 0);
                const HRESULT present = device->Present(nullptr, nullptr, nullptr, nullptr);
                if (SUCCEEDED(clear) && SUCCEEDED(present)) devices.push_back(utf8(id.Description));
                device->Release();
            }
        }
    }
    std::printf("{\"format\":\"s7-render-probe-v1\",\"devices\":[");
    for (size_t i = 0; i < devices.size(); ++i) { if (i) std::putchar(','); json_string(devices[i]); }
    std::puts("]}");
    if (window) DestroyWindow(window);
    if (d3d) d3d->Release();
    FreeLibrary(dll);
    return devices.empty() ? 2 : 0;
}
