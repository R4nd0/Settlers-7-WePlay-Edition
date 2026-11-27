// Authored GPU scenes only. Exercises the real D3D9On12 resource handoff.
#include "on12_frame_context.h"
#include "water_on12_identity.h"
#include <d3dcompiler.h>
#include <d3d9on12.h>
#include <wrl/client.h>
#include <vector>
#include <array>
#include <cstdio>
#include <cmath>
#include <cstring>
#include <thread>

using Microsoft::WRL::ComPtr;
using On12Graphics::FrameContext;
using On12Graphics::SpatialSettings;
extern "C" HRESULT On12SpatialFixtureAttach(IDirect3DDevice9*);
extern "C" bool On12SpatialFixtureClose();
namespace {
unsigned checks = 0;
bool Check(bool okay, unsigned line) { ++checks; if (!okay) std::fprintf(stderr, "check failed at %u\n", line); return okay; }
#define REQUIRE(x) do { if (!Check(!!(x), __LINE__)) return 20; } while (false)
bool Read(IDirect3DDevice9* device, IDirect3DSurface9* source, UINT width, UINT height, std::vector<DWORD>& pixels) {
    ComPtr<IDirect3DSurface9> staging;
    if (FAILED(device->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &staging, nullptr))
        || FAILED(device->GetRenderTargetData(source, staging.Get()))) return false;
    D3DLOCKED_RECT lock{}; if (FAILED(staging->LockRect(&lock, nullptr, D3DLOCK_READONLY))) return false;
    pixels.resize(width * height);
    for (UINT y = 0; y < height; ++y) std::memcpy(pixels.data() + y * width, static_cast<const BYTE*>(lock.pBits) + y * lock.Pitch, width * 4);
    return SUCCEEDED(staging->UnlockRect());
}
bool Color(DWORD value, DWORD expected, int tolerance = 0) {
    for (unsigned shift : {0u, 8u, 16u}) if (std::abs(int((value >> shift) & 255) - int((expected >> shift) & 255)) > tolerance) return false;
    return true;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || (wcscmp(argv[2], L"on12") && wcscmp(argv[2], L"native"))) return 1;
    const bool on12 = !wcscmp(argv[2], L"on12"); std::string expected;
    for (size_t i = 0; i < wcslen(argv[1]); ++i) { const auto c = argv[1][i]; if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return 1; expected += static_cast<char>(c); }
    if (expected.size() != 64) return 1;
    WNDCLASSW wc{}; wc.lpfnWndProc = DefWindowProcW; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"WePlayOn12SpatialFixture";
    REQUIRE(RegisterClassW(&wc)); HWND window = CreateWindowW(wc.lpszClassName, L"On12 spatial GPU fixture", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr); REQUIRE(window);
    HMODULE module = LoadLibraryW(L"d3d9.dll"); REQUIRE(module);
    using Create = IDirect3D9* (WINAPI*)(UINT); auto create = reinterpret_cast<Create>(GetProcAddress(module, "Direct3DCreate9")); REQUIRE(create);
    ComPtr<IDirect3D9> api; api.Attach(create(D3D_SDK_VERSION)); REQUIRE(api);
    REQUIRE(WaterOn12Identity::Module(GetModuleHandleW(L"dxwrapper.dll"), WaterOn12Identity::Wrapper));
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.BackBufferWidth = pp.BackBufferHeight = 32;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    ComPtr<IDirect3DDevice9> wrapped_device, device; REQUIRE(SUCCEEDED(api->CreateDevice(0, D3DDEVTYPE_HAL, window, D3DCREATE_HARDWARE_VERTEXPROCESSING | D3DCREATE_FPU_PRESERVE, &pp, &device)));
    if (on12) REQUIRE(WaterOn12Identity::Module(GetModuleHandleW(L"d3d9on12.dll"), expected.c_str()));
    ComPtr<ID3DBlob> vs, ps, errors;
    constexpr UINT flags = D3DCOMPILE_ENABLE_STRICTNESS | D3DCOMPILE_WARNINGS_ARE_ERRORS | D3DCOMPILE_OPTIMIZATION_LEVEL3;
    REQUIRE(SUCCEEDED(D3DCompileFromFile(L"on12_spatial_pass.hlsl", nullptr, nullptr, "VS", "vs_5_0", flags, 0, &vs, &errors)));
    errors.Reset(); REQUIRE(SUCCEEDED(D3DCompileFromFile(L"on12_spatial_pass.hlsl", nullptr, nullptr, "PS", "ps_5_0", flags, 0, &ps, &errors)));
    FrameContext context; SpatialSettings config;
    REQUIRE(context.Render(nullptr, nullptr, config) == S_FALSE);
    HRESULT initialized = context.Initialize(device.Get(), vs->GetBufferPointer(), vs->GetBufferSize(), ps->GetBufferPointer(), ps->GetBufferSize());
    if (FAILED(initialized) && on12) std::fprintf(stderr, "initialize=%08lx\n", initialized);
    if (!on12) {
        REQUIRE(initialized == E_NOINTERFACE && context.SafeForD3D9());
        REQUIRE(context.Render(nullptr, nullptr, config) == S_FALSE);
        std::printf("{\"passed\":true,\"on12_only_rejection\":true,\"checks\":%u,\"synthetic_only\":true}\n", checks);
        device.Reset(); api.Reset(); DestroyWindow(window); return 0;
    }
    REQUIRE(SUCCEEDED(initialized)); config.enabled = 1;
    // A wrapper object must be rejected before resource checkout, rather than
    // being downcast inside Microsoft's runtime as if it were a native object.
    ComPtr<IDirect3DSurface9> wrapped_surface;
    REQUIRE(SUCCEEDED(device->CreateRenderTarget(8, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &wrapped_surface, nullptr)));
    REQUIRE(context.Render(wrapped_surface.Get(), wrapped_surface.Get(), config) == E_INVALIDARG && context.SafeForD3D9() && !context.Stats().checkouts);
    wrapped_surface.Reset(); wrapped_device = device; device = context.NativeDevice(); REQUIRE(device);
    ComPtr<IDirect3DSurface9> input, output;
    auto make = [&](UINT source, UINT target) {
        return SUCCEEDED(device->CreateRenderTarget(source, source, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &input, nullptr))
            && SUCCEEDED(device->CreateRenderTarget(target, target, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &output, nullptr));
    };
    REQUIRE(make(8, 8));
    HRESULT foreign = S_OK; std::thread other([&] { foreign = context.Render(input.Get(), output.Get(), config); }); other.join();
    REQUIRE(foreign == RPC_E_WRONG_THREAD && context.SafeForD3D9() && !context.Stats().checkouts);
    ComPtr<IDirect3DSurface9> system_memory;
    REQUIRE(SUCCEEDED(device->CreateOffscreenPlainSurface(8, 8, D3DFMT_A8R8G8B8, D3DPOOL_SYSTEMMEM, &system_memory, nullptr)));
    REQUIRE(context.Render(system_memory.Get(), output.Get(), config) == DXGI_ERROR_UNSUPPORTED
        && context.SafeForD3D9() && !context.Stats().checkouts);
    system_memory.Reset();
    // Deliberately use a vertex shader in the pixel stage. PSO creation fails
    // after checkout, so this exercises the return-without-submission path.
    {
        FrameContext bad_pipeline;
        REQUIRE(SUCCEEDED(bad_pipeline.Initialize(device.Get(), vs->GetBufferPointer(), vs->GetBufferSize(), vs->GetBufferPointer(), vs->GetBufferSize())));
        REQUIRE(FAILED(bad_pipeline.Render(input.Get(), output.Get(), config)) && bad_pipeline.SafeForD3D9());
        const auto bad = bad_pipeline.Stats();
        REQUIRE(bad.checkouts == 2 && bad.returns == 2 && !bad.applied && !bad.failed_returns);
        REQUIRE(SUCCEEDED(bad_pipeline.Shutdown(5000)));
    }
    D3DVIEWPORT9 viewport{}; REQUIRE(SUCCEEDED(device->GetViewport(&viewport)));
    REQUIRE(SUCCEEDED(device->SetRenderState(D3DRS_CULLMODE, D3DCULL_NONE)));
    REQUIRE(SUCCEEDED(device->SetRenderState(D3DRS_LIGHTING, FALSE)));
    REQUIRE(SUCCEEDED(device->SetRenderState(D3DRS_ZENABLE, FALSE)));
    REQUIRE(SUCCEEDED(device->SetVertexShader(nullptr)) && SUCCEEDED(device->SetPixelShader(nullptr)));
    REQUIRE(SUCCEEDED(device->SetTexture(0, nullptr)) && SUCCEEDED(device->SetFVF(D3DFVF_XYZRHW | D3DFVF_DIFFUSE)));
    REQUIRE(SUCCEEDED(device->SetTextureStageState(0, D3DTSS_COLOROP, D3DTOP_SELECTARG1))
        && SUCCEEDED(device->SetTextureStageState(0, D3DTSS_COLORARG1, D3DTA_DIFFUSE)));
    REQUIRE(SUCCEEDED(device->SetRenderTarget(0, output.Get())));
    D3DVIEWPORT9 draw_viewport{0, 0, 8, 8, 0, 1}; REQUIRE(SUCCEEDED(device->SetViewport(&draw_viewport)));
    viewport = draw_viewport;
    std::vector<DWORD> original, identity, filtered;
    for (LONG y = 0; y < 8; ++y) for (LONG x = 0; x < 8; ++x) { RECT r{x, y, x + 1, y + 1}; REQUIRE(SUCCEEDED(device->ColorFill(input.Get(), &r, x > y ? 0xffffffff : 0xff000000))); }
    REQUIRE(Read(device.Get(), input.Get(), 8, 8, original));
    config.antialiasing = 0;
    REQUIRE(context.Render(input.Get(), output.Get(), config) == S_OK && context.SafeForD3D9());
    // Read immediately through D3D9: this depends on the returned native fence.
    REQUIRE(Read(device.Get(), output.Get(), 8, 8, identity)); REQUIRE(identity == original);
    config.antialiasing = 1;
    REQUIRE(context.Render(input.Get(), output.Get(), config) == S_OK && context.SafeForD3D9());
    REQUIRE(Read(device.Get(), output.Get(), 8, 8, filtered));
    unsigned softened = 0; for (DWORD p : filtered) if ((p & 255) > 0 && (p & 255) < 255) ++softened;
    REQUIRE(softened >= 4 && softened < 48); REQUIRE(Color(filtered[7], 0xffffffff) && Color(filtered[56], 0xff000000));
    D3DVIEWPORT9 after{}; DWORD cull = 0; REQUIRE(SUCCEEDED(device->GetViewport(&after)) && !std::memcmp(&after, &viewport, sizeof(after)));
    REQUIRE(SUCCEEDED(device->GetRenderState(D3DRS_CULLMODE, &cull)) && cull == D3DCULL_NONE);
    // No state rebind after native D3D12. A real D3D9 draw must use the state
    // established before checkout, including the previously bound target.
    struct Vertex { float x, y, z, rhw; DWORD color; };
    const Vertex triangle[]{{-0.5f,-0.5f,0,1,0xffa020d0},{15.5f,-0.5f,0,1,0xffa020d0},{-0.5f,15.5f,0,1,0xffa020d0}};
    REQUIRE(SUCCEEDED(device->BeginScene()));
    const HRESULT drawn = device->DrawPrimitiveUP(D3DPT_TRIANGLELIST, 1, triangle, sizeof(Vertex));
    REQUIRE(SUCCEEDED(device->EndScene()) && SUCCEEDED(drawn));
    REQUIRE(Read(device.Get(), output.Get(), 8, 8, filtered) && Color(filtered[3 * 8 + 3], 0xffa020d0));
    const auto before_invalid = context.Stats().checkouts; auto invalid = config; invalid.enabled = 2;
    REQUIRE(context.Render(input.Get(), output.Get(), invalid) == E_INVALIDARG && context.Stats().checkouts == before_invalid);
    invalid = config; invalid.antialiasing = NAN; REQUIRE(context.Render(input.Get(), output.Get(), invalid) == E_INVALIDARG && context.SafeForD3D9());
    ComPtr<IDirect3DSurface9> default_target;
    REQUIRE(SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &default_target)) && SUCCEEDED(device->SetRenderTarget(0, default_target.Get())));
    default_target.Reset();
    REQUIRE(SUCCEEDED(context.Drain(5000))); input.Reset(); output.Reset(); REQUIRE(make(4, 8));
    const std::array<RECT, 4> rectangles{{{0, 0, 2, 2}, {2, 0, 4, 2}, {0, 2, 2, 4}, {2, 2, 4, 4}}};
    const DWORD colors[]{0xffff0000, 0xff00ff00, 0xff0000ff, 0xffffff00};
    for (unsigned i = 0; i < 4; ++i) REQUIRE(SUCCEEDED(device->ColorFill(input.Get(), &rectangles[i], colors[i])));
    config.antialiasing = 0; REQUIRE(context.Render(input.Get(), output.Get(), config) == S_OK);
    REQUIRE(Read(device.Get(), output.Get(), 8, 8, filtered));
    REQUIRE(Color(filtered[1 * 8 + 1], colors[0]) && Color(filtered[1 * 8 + 6], colors[1])
        && Color(filtered[6 * 8 + 1], colors[2]) && Color(filtered[6 * 8 + 6], colors[3]));
    REQUIRE(Color(filtered[3], 0xffcb3400, 1));
    // Sharper than bilinear (191/64) without overshooting the source range.
    for (DWORD pixel : filtered) REQUIRE((pixel >> 24) == 255);
    REQUIRE(SUCCEEDED(context.Drain(5000))); input.Reset(); output.Reset();
    ComPtr<IDirect3DSurface9> backbuffer; REQUIRE(SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)));
    REQUIRE(SUCCEEDED(device->ColorFill(backbuffer.Get(), nullptr, 0xff204060))); config.antialiasing = 1;
    alignas(16) unsigned char fp_before[512]{}, fp_after[512]{}; SetLastError(0x73571357); _fxsave(fp_before);
    HRESULT applied = context.Render(backbuffer.Get(), backbuffer.Get(), config); _fxsave(fp_after); DWORD last_error = GetLastError();
    REQUIRE(applied == S_OK && last_error == 0x73571357 && !std::memcmp(fp_before, fp_after, 4) && !std::memcmp(fp_before + 24, fp_after + 24, 4));
    REQUIRE(Read(device.Get(), backbuffer.Get(), 32, 32, filtered)); for (DWORD p : filtered) REQUIRE(Color(p, 0xff204060));
    REQUIRE(SUCCEEDED(context.Drain(5000))); backbuffer.Reset(); REQUIRE(SUCCEEDED(device->Reset(&pp))); REQUIRE(make(8, 8));
    REQUIRE(SUCCEEDED(device->ColorFill(input.Get(), nullptr, 0xffa04020)));
    REQUIRE(context.Render(input.Get(), output.Get(), config) == S_OK && Read(device.Get(), output.Get(), 8, 8, filtered));
    for (DWORD p : filtered) REQUIRE(Color(p, 0xffa04020));
    REQUIRE(SUCCEEDED(context.Drain(5000))); input.Reset(); output.Reset(); const auto stats = context.Stats();
    REQUIRE(stats.applied == 5 && stats.checkouts == stats.returns && !stats.failed_returns && context.SafeForD3D9());
    device.Reset(); REQUIRE(SUCCEEDED(context.Shutdown(5000)));
    // Exercise the actual host hooks on the same wrapper used by the game.
    REQUIRE(SUCCEEDED(On12SpatialFixtureAttach(wrapped_device.Get())));
    REQUIRE(SUCCEEDED(wrapped_device->Present(nullptr, nullptr, nullptr, nullptr)));
    REQUIRE(SUCCEEDED(wrapped_device->Reset(&pp)));
    REQUIRE(On12SpatialFixtureClose());
    std::printf("{\"passed\":true,\"checks\":%u,\"native_d3d12_passes\":%llu,\"checkouts\":%llu,\"returns\":%llu,\"antialiased_pixels\":%u,\"scale_2x\":true,\"in_place\":true,\"reset\":true,\"host_present_reset\":true,\"synthetic_only\":true}\n",
        checks, static_cast<unsigned long long>(stats.applied), static_cast<unsigned long long>(stats.checkouts), static_cast<unsigned long long>(stats.returns), softened);
    wrapped_device.Reset(); api.Reset(); DestroyWindow(window); return 0;
}
