#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <d3d9.h>
#include <cstdio>
#include <cstring>
#include <cwchar>
#include <string>
#include <vector>

static std::string dxvk_description(const char (&text)[MAX_DEVICE_IDENTIFIER_STRING]) {
    // DXVK copies the Vulkan device name as UTF-8 into the D3D9 description.
    // Preserve those exact bytes for the later DXVK device-name filter.
    const auto* end = static_cast<const char*>(std::memchr(text, 0, sizeof(text)));
    if (!end || end == text) return {};
    const auto length = static_cast<int>(end - text);
    if (!MultiByteToWideChar(CP_UTF8, MB_ERR_INVALID_CHARS, text, length, nullptr, 0)) return {};
    return std::string(text, static_cast<size_t>(length));
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
    if(argc==2&&std::wcscmp(argv[1],L"--capabilities")==0) {
        std::puts("s7-render-probe-smaa-v1");
        return 0;
    }
    const bool smaa=argc==3&&std::wcscmp(argv[2],L"--smaa")==0;
    if (argc!=2&&!smaa) return 3;
    SetErrorMode(SEM_FAILCRITICALERRORS | SEM_NOGPFAULTERRORBOX | SEM_NOOPENFILEERRORBOX);
    HMODULE dll = LoadLibraryExW(argv[1], nullptr, LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
    if (!dll) { std::puts("{\"format\":\"s7-render-probe-v1\",\"devices\":[]}"); return 2; }
    using Create = IDirect3D9* (WINAPI*)(UINT);
    auto create = reinterpret_cast<Create>(GetProcAddress(dll, "Direct3DCreate9"));
    IDirect3D9* d3d = create ? create(D3D_SDK_VERSION) : nullptr;
    const int width=smaa?320:64,height=smaa?240:64;
    HWND window = CreateWindowExW(0, L"STATIC", L"S7 graphics check", WS_POPUP, -30000, -30000, width, height,
                                 nullptr, nullptr, GetModuleHandleW(nullptr), nullptr);
    std::vector<std::string> devices;
    if (d3d && window) {
        const UINT count = d3d->GetAdapterCount();
        if (count <= 16) for (UINT i=0; i<(smaa&&count?1:count); ++i) {
            D3DADAPTER_IDENTIFIER9 id{};
            if (FAILED(d3d->GetAdapterIdentifier(i, 0, &id))) continue;
            D3DPRESENT_PARAMETERS pp{};
            pp.BackBufferWidth=static_cast<UINT>(width);pp.BackBufferHeight=static_cast<UINT>(height);
            pp.BackBufferFormat = D3DFMT_UNKNOWN;
            pp.BackBufferCount = 1; pp.SwapEffect = D3DSWAPEFFECT_DISCARD;
            pp.hDeviceWindow = window; pp.Windowed = TRUE; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
            IDirect3DDevice9* device = nullptr;
            const HRESULT result = d3d->CreateDevice(i, D3DDEVTYPE_HAL, window,
                D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device);
            if (SUCCEEDED(result) && device) {
                const HRESULT clear = device->Clear(0, nullptr, D3DCLEAR_TARGET, 0xff284366, 1.0f, 0);
                HRESULT present=device->Present(nullptr,nullptr,nullptr,nullptr);
                if(smaa) {
                    const D3DRECT mark{35,37,207,113};
                    for(unsigned frame=0;frame<120&&SUCCEEDED(present);++frame) {
                        device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff284366,1.0f,0);
                        device->Clear(1,&mark,D3DCLEAR_TARGET,0xffeeeeee,1.0f,0);
                        present=device->Present(nullptr,nullptr,nullptr,nullptr);
                        Sleep(12);
                    }
                }
                if (SUCCEEDED(clear) && SUCCEEDED(present)) {
                    const auto name = dxvk_description(id.Description);
                    if (!name.empty()) devices.push_back(name);
                }
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
