// Authored GPU scenes only. Exercises the real D3D9On12 resource handoff.
#include "on12_frame_context.h"
#include "on12_scene_targets.h"
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
extern "C" HRESULT On12SpatialFixtureUpscaleAttach(IDirect3DDevice9*, bool (*)(IDirect3DDevice9*, IDirect3DSurface9*));
extern "C" HRESULT On12SpatialFixtureSceneAttach(IDirect3DDevice9*,IDirect3DVertexShader9*,IDirect3DPixelShader9*);
namespace {
LRESULT CALLBACK FixtureWindow(HWND window, UINT message, WPARAM w, LPARAM l) { return DefWindowProcW(window, message, w, l); }
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
bool ValidateOutput(IDirect3DDevice9* device, IDirect3DSurface9* target) {
    D3DSURFACE_DESC desc{}; std::vector<DWORD> pixels;
    if (FAILED(target->GetDesc(&desc)) || desc.Width != 64 || desc.Height != 64 || !Read(device, target, 64, 64, pixels)) return false;
    for (DWORD pixel : pixels) if (!Color(pixel, 0xff2050b0)) return false;
    return true;
}
}
int wmain(int argc, wchar_t** argv) {
    if (argc != 3 || (wcscmp(argv[2], L"on12") && wcscmp(argv[2], L"native"))) return 1;
    const bool on12 = !wcscmp(argv[2], L"on12"); std::string expected;
    for (size_t i = 0; i < wcslen(argv[1]); ++i) { const auto c = argv[1][i]; if (!((c >= L'0' && c <= L'9') || (c >= L'a' && c <= L'f'))) return 1; expected += static_cast<char>(c); }
    if (expected.size() != 64) return 1;
    WNDCLASSW wc{}; wc.lpfnWndProc = FixtureWindow; wc.hInstance = GetModuleHandleW(nullptr); wc.lpszClassName = L"WePlayOn12SpatialFixture";
    REQUIRE(RegisterClassW(&wc)); HWND window = CreateWindowW(wc.lpszClassName, L"On12 spatial GPU fixture", WS_OVERLAPPEDWINDOW, 0, 0, 64, 64, nullptr, nullptr, wc.hInstance, nullptr); REQUIRE(window);
    HMODULE module = LoadLibraryW(L"d3d9.dll"); REQUIRE(module);
    using Create = IDirect3D9* (WINAPI*)(UINT); auto create = reinterpret_cast<Create>(GetProcAddress(module, "Direct3DCreate9")); REQUIRE(create);
    ComPtr<IDirect3D9> api; api.Attach(create(D3D_SDK_VERSION)); REQUIRE(api);
    REQUIRE(WaterOn12Identity::Module(GetModuleHandleW(L"dxwrapper.dll"), WaterOn12Identity::Wrapper));
    D3DPRESENT_PARAMETERS pp{}; pp.Windowed = TRUE; pp.BackBufferWidth = pp.BackBufferHeight = 32;
    pp.BackBufferFormat = D3DFMT_A8R8G8B8; pp.SwapEffect = D3DSWAPEFFECT_DISCARD; pp.hDeviceWindow = window; pp.PresentationInterval = D3DPRESENT_INTERVAL_IMMEDIATE;
    pp.EnableAutoDepthStencil = TRUE; pp.AutoDepthStencilFormat = D3DFMT_D24S8; pp.Flags = D3DPRESENTFLAG_DISCARD_DEPTHSTENCIL;
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
    std::vector<DWORD> original, identity, filtered, source_aa;
    for (LONG y = 0; y < 8; ++y) for (LONG x = 0; x < 8; ++x) { RECT r{x, y, x + 1, y + 1}; REQUIRE(SUCCEEDED(device->ColorFill(input.Get(), &r, x > y ? 0xffffffff : 0xff000000))); }
    REQUIRE(Read(device.Get(), input.Get(), 8, 8, original));
    config.antialiasing = 0;
    REQUIRE(SUCCEEDED(device->BeginScene()));
    const HRESULT in_scene = context.Render(input.Get(), output.Get(), config);
    REQUIRE(SUCCEEDED(device->EndScene()));
    REQUIRE(in_scene == S_OK && context.SafeForD3D9());
    // Read immediately through D3D9: this depends on the returned native fence.
    REQUIRE(Read(device.Get(), output.Get(), 8, 8, identity)); REQUIRE(identity == original);
    config.antialiasing = 1;
    REQUIRE(context.Render(input.Get(), output.Get(), config) == S_OK && context.SafeForD3D9());
    REQUIRE(Read(device.Get(), output.Get(), 8, 8, filtered));
    source_aa = filtered;
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
    {
        ComPtr<IDirect3DSurface9> glyph, glyph_output;
        REQUIRE(SUCCEEDED(device->CreateRenderTarget(8, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &glyph, nullptr))
            && SUCCEEDED(device->CreateRenderTarget(8, 8, D3DFMT_A8R8G8B8, D3DMULTISAMPLE_NONE, 0, FALSE, &glyph_output, nullptr)));
        for (bool dark : {false, true}) {
            for (LONG y = 0; y < 8; ++y) for (LONG x = 0; x < 8; ++x) {
                const bool stroke = (x == 2 && y >= 2 && y <= 6) || ((y == 2 || y == 4 || y == 6) && x >= 2 && x <= 5);
                RECT r{x, y, x + 1, y + 1}; REQUIRE(SUCCEEDED(device->ColorFill(glyph.Get(), &r, stroke != dark ? 0xffffffff : 0xff000000)));
            }
            REQUIRE(context.Render(glyph.Get(), glyph_output.Get(), config) == S_OK);
            REQUIRE(Read(device.Get(), glyph_output.Get(), 8, 8, filtered));
            const DWORD ink = dark ? 0xff000000 : 0xffffffff;
            // The middle crossbar used to disappear under directional AA.
            REQUIRE(filtered[4 * 8 + 3] == ink && filtered[2 * 8 + 4] == ink && filtered[6 * 8 + 4] == ink && filtered[3 * 8 + 2] == ink);
        }
    }
    const auto before_invalid = context.Stats().checkouts; auto invalid = config; invalid.enabled = 2;
    REQUIRE(context.Render(input.Get(), output.Get(), invalid) == E_INVALIDARG && context.Stats().checkouts == before_invalid);
    invalid = config; invalid.antialiasing = NAN; REQUIRE(context.Render(input.Get(), output.Get(), invalid) == E_INVALIDARG && context.SafeForD3D9());
    ComPtr<IDirect3DSurface9> default_target;
    REQUIRE(SUCCEEDED(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &default_target)) && SUCCEEDED(device->SetRenderTarget(0, default_target.Get())));
    default_target.Reset();
    REQUIRE(SUCCEEDED(context.Drain(5000))); input.Reset(); output.Reset(); REQUIRE(make(8, 16));
    for (LONG y = 0; y < 8; ++y) for (LONG x = 0; x < 8; ++x) { RECT r{x, y, x + 1, y + 1}; REQUIRE(SUCCEEDED(device->ColorFill(input.Get(), &r, x > y ? 0xffffffff : 0xff000000))); }
    config.antialiasing = 1; REQUIRE(context.Render(input.Get(), output.Get(), config) == S_OK);
    std::vector<DWORD> composed; REQUIRE(Read(device.Get(), output.Get(), 16, 16, composed));
    // Independent reference: upload the previously read 8x8 AA result, then
    // request scale-only. Combined rendering must have the same stage order.
    for (LONG y = 0; y < 8; ++y) for (LONG x = 0; x < 8; ++x) { RECT r{x, y, x + 1, y + 1}; REQUIRE(SUCCEEDED(device->ColorFill(input.Get(), &r, source_aa[y * 8 + x]))); }
    config.antialiasing = 0; REQUIRE(context.Render(input.Get(), output.Get(), config) == S_OK);
    REQUIRE(Read(device.Get(), output.Get(), 16, 16, filtered) && filtered == composed);
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
    REQUIRE(stats.applied == 9 && stats.checkouts == stats.returns && !stats.failed_returns && context.SafeForD3D9());
    device.Reset(); REQUIRE(SUCCEEDED(context.Shutdown(5000)));
    // Exercise the actual host hooks on the same wrapper used by the game.
    REQUIRE(SUCCEEDED(On12SpatialFixtureAttach(wrapped_device.Get())));
    REQUIRE(SUCCEEDED(wrapped_device->Present(nullptr, nullptr, nullptr, nullptr)));
    REQUIRE(SUCCEEDED(wrapped_device->Reset(&pp)));
    REQUIRE(On12SpatialFixtureClose());
    // The engine keeps a native 32x32 window/layout. Only its world targets
    // become 16x16. A one-pixel UI stroke must remain one native pixel wide.
    REQUIRE(SUCCEEDED(context.Initialize(wrapped_device.Get(),vs->GetBufferPointer(),vs->GetBufferSize(),ps->GetBufferPointer(),ps->GetBufferSize())));
    {
        On12Graphics::SceneTargets scene;
        std::array<void*,119> functions{}; std::memcpy(functions.data(),*reinterpret_cast<void***>(wrapped_device.Get()),sizeof(functions));
        REQUIRE(SUCCEEDED(scene.Initialize(wrapped_device.Get(),&context,functions,16,16,32,32)));
        ComPtr<IDirect3DSurface9> main,logical,physical;
        ComPtr<IDirect3DTexture9> world;
        REQUIRE(SUCCEEDED(wrapped_device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&main)));
        REQUIRE(SUCCEEDED(wrapped_device->CreateTexture(32,32,1,D3DUSAGE_RENDERTARGET,D3DFMT_A8R8G8B8,D3DPOOL_DEFAULT,&world,nullptr)));
        ComPtr<IDirect3DSurface9> world_target;REQUIRE(SUCCEEDED(world->GetSurfaceLevel(0,&world_target)));
        REQUIRE(SUCCEEDED(scene.BeginFrame()));
        REQUIRE(SUCCEEDED(scene.SetTarget(0,world_target.Get())));
        REQUIRE(SUCCEEDED(scene.GetTarget(0,&logical))&&logical.Get()==world_target.Get());logical.Reset();
        REQUIRE(SUCCEEDED(context.NativeDevice()->GetRenderTarget(0,&physical)));
        D3DSURFACE_DESC physical_desc{};REQUIRE(SUCCEEDED(physical->GetDesc(&physical_desc))&&physical_desc.Width==16&&physical_desc.Height==16);physical.Reset();
        D3DVIEWPORT9 world_view{0,0,32,32,0,1},physical_view{},logical_view{};
        REQUIRE(SUCCEEDED(scene.SetViewport(&world_view))&&SUCCEEDED(scene.GetViewport(&logical_view))&&logical_view.Width==32);
        REQUIRE(SUCCEEDED(context.NativeDevice()->GetViewport(&physical_view))&&physical_view.Width==16);
        REQUIRE(SUCCEEDED(scene.Clear(0,nullptr,D3DCLEAR_TARGET,0xff7030c0,1,0)));
        REQUIRE(SUCCEEDED(scene.SetTarget(0,main.Get())));
        REQUIRE(SUCCEEDED(scene.SetTexture(0,world.Get())));
        ComPtr<IDirect3DBaseTexture9> logical_texture;REQUIRE(SUCCEEDED(scene.GetTexture(0,&logical_texture))&&logical_texture.Get()==world.Get());logical_texture.Reset();
        REQUIRE(SUCCEEDED(scene.SetTexture(0,nullptr)));
        REQUIRE(SUCCEEDED(scene.Stretch(world_target.Get(),nullptr,main.Get(),nullptr,D3DTEXF_POINT)));
        REQUIRE(SUCCEEDED(wrapped_device->BeginScene()));
        SpatialSettings quality;quality.enabled=1;
        REQUIRE(scene.BeginUi(quality)==S_OK&&scene.Ui()&&scene.Safe());
        REQUIRE(SUCCEEDED(context.NativeDevice()->GetViewport(&physical_view))&&physical_view.Width==32);
        D3DRECT stroke{21,3,22,28};REQUIRE(SUCCEEDED(scene.Clear(1,&stroke,D3DCLEAR_TARGET,0xffffffff,1,0)));
        REQUIRE(SUCCEEDED(wrapped_device->EndScene()));
        REQUIRE(SUCCEEDED(context.NativeDevice()->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&physical)));
        std::vector<DWORD> native_ui;REQUIRE(Read(context.NativeDevice(),physical.Get(),32,32,native_ui));physical.Reset();
        for(UINT y=0;y<32;++y)for(UINT x=0;x<32;++x)REQUIRE(Color(native_ui[y*32+x],x==21&&y>=3&&y<28?0xffffffff:0xff7030c0));
        REQUIRE(scene.Stats().upscaled==1&&scene.Stats().ui_width==32&&scene.Stats().resources>=3);
        ComPtr<IDirect3DStateBlock9> saved;
        REQUIRE(SUCCEEDED(scene.BeforeCapture())&&SUCCEEDED(wrapped_device->CreateStateBlock(D3DSBT_ALL,&saved))&&SUCCEEDED(scene.AfterCapture()));
        REQUIRE(SUCCEEDED(scene.BeginFrame()));
        REQUIRE(SUCCEEDED(saved->Apply())&&SUCCEEDED(scene.AfterApply()));
        REQUIRE(SUCCEEDED(context.NativeDevice()->GetViewport(&physical_view))&&physical_view.Width==16);
        REQUIRE(SUCCEEDED(scene.GetViewport(&logical_view))&&logical_view.Width==32);
        saved.Reset();
        REQUIRE(SUCCEEDED(scene.BeforeCapture())&&SUCCEEDED(wrapped_device->BeginStateBlock()));
        REQUIRE(SUCCEEDED(scene.SetViewport(&world_view)));
        REQUIRE(SUCCEEDED(wrapped_device->EndStateBlock(&saved))&&SUCCEEDED(scene.AfterCapture()));
        REQUIRE(SUCCEEDED(saved->Apply())&&SUCCEEDED(scene.AfterApply()));
        REQUIRE(SUCCEEDED(context.NativeDevice()->GetViewport(&physical_view))&&physical_view.Width==16);saved.Reset();
        REQUIRE(SUCCEEDED(context.Drain(5000))&&SUCCEEDED(scene.Shutdown()));
        main.Reset();world_target.Reset();world.Reset();
    }
    REQUIRE(SUCCEEDED(context.Shutdown(5000)));
    SetWindowLongPtrW(window, GWL_STYLE, WS_POPUP);
    const HRESULT upscale_attached = On12SpatialFixtureUpscaleAttach(wrapped_device.Get(), ValidateOutput);
    if (FAILED(upscale_attached)) std::fprintf(stderr, "upscale attach=%08lx\n", upscale_attached);
    REQUIRE(SUCCEEDED(upscale_attached));
    MSG message{}; while (PeekMessageW(&message, nullptr, 0, 0, PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    RECT logical_rect{}; REQUIRE(GetClientRect(window, &logical_rect) && logical_rect.right == 32 && logical_rect.bottom == 32);
    POINT cursor{16, 16}; REQUIRE(ClientToScreen(window, &cursor) && ScreenToClient(window, &cursor) && cursor.x == 16 && cursor.y == 16);
    ComPtr<IDirect3DDevice9On12> bridge; ComPtr<IDirect3DDevice9> raw;
    REQUIRE(SUCCEEDED(wrapped_device.As(&bridge)) && SUCCEEDED(bridge.As(&raw)));
    REQUIRE(SUCCEEDED(raw->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, &backbuffer)));
    REQUIRE(SUCCEEDED(raw->ColorFill(backbuffer.Get(), nullptr, 0xff2050b0)));
    backbuffer.Reset(); raw.Reset(); bridge.Reset();
    REQUIRE(SUCCEEDED(wrapped_device->Present(nullptr, nullptr, nullptr, nullptr)));
    REQUIRE(SUCCEEDED(wrapped_device->Reset(&pp)));
    REQUIRE(On12SpatialFixtureClose());
    constexpr char ui_shader[]="float4 S:register(c64);float4 T:register(c65);float4 VS(float3 p:POSITION):POSITION{return float4(p.x*S.x-1,p.y*T.y+1,0,1);}float4 PS():COLOR{return 1;}";
    ComPtr<ID3DBlob> ui_vs,ui_ps;errors.Reset();
    REQUIRE(SUCCEEDED(D3DCompile(ui_shader,sizeof(ui_shader)-1,nullptr,nullptr,nullptr,"VS","vs_3_0",flags,0,&ui_vs,&errors)));
    errors.Reset();REQUIRE(SUCCEEDED(D3DCompile(ui_shader,sizeof(ui_shader)-1,nullptr,nullptr,nullptr,"PS","ps_3_0",flags,0,&ui_ps,&errors)));
    ComPtr<IDirect3DVertexShader9> ui_vertex;ComPtr<IDirect3DPixelShader9> ui_pixel;
    REQUIRE(SUCCEEDED(wrapped_device->CreateVertexShader(static_cast<const DWORD*>(ui_vs->GetBufferPointer()),&ui_vertex)));
    REQUIRE(SUCCEEDED(wrapped_device->CreatePixelShader(static_cast<const DWORD*>(ui_ps->GetBufferPointer()),&ui_pixel)));
    REQUIRE(SUCCEEDED(On12SpatialFixtureSceneAttach(wrapped_device.Get(),ui_vertex.Get(),ui_pixel.Get())));
    REQUIRE(SUCCEEDED(wrapped_device->Present(nullptr,nullptr,nullptr,nullptr))); // Begin the first complete scaled world frame.
    REQUIRE(SUCCEEDED(wrapped_device->Clear(0,nullptr,D3DCLEAR_TARGET,0xff206080,1,0)));
    REQUIRE(SUCCEEDED(wrapped_device->BeginScene()));
    const float projection[]{2.0f/32,0,0,0,0,-2.0f/32,0,0,0,0,1,0,0,0,0,1};
    REQUIRE(SUCCEEDED(wrapped_device->SetFVF(D3DFVF_XYZ))&&SUCCEEDED(wrapped_device->SetVertexShader(ui_vertex.Get()))&&SUCCEEDED(wrapped_device->SetPixelShader(ui_pixel.Get())));
    REQUIRE(SUCCEEDED(wrapped_device->SetVertexShaderConstantF(64,projection,4)));
    const float clipped_ui[]{100,100,0};REQUIRE(SUCCEEDED(wrapped_device->DrawPrimitiveUP(D3DPT_POINTLIST,1,clipped_ui,sizeof(clipped_ui))));
    const D3DRECT native_stroke{21,3,22,28};REQUIRE(SUCCEEDED(wrapped_device->Clear(1,&native_stroke,D3DCLEAR_TARGET,0xffffffff,1,0)));
    REQUIRE(SUCCEEDED(wrapped_device->EndScene()));
    ComPtr<IDirect3DSurface9> full_ui;REQUIRE(SUCCEEDED(wrapped_device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&full_ui)));
    std::vector<DWORD> hooked_pixels;REQUIRE(Read(wrapped_device.Get(),full_ui.Get(),32,32,hooked_pixels));full_ui.Reset();
    for(UINT y=3;y<28;++y)REQUIRE(Color(hooked_pixels[y*32+21],0xffffffff)&&Color(hooked_pixels[y*32+20],0xff206080)&&Color(hooked_pixels[y*32+22],0xff206080));
    REQUIRE(SUCCEEDED(wrapped_device->Present(nullptr,nullptr,nullptr,nullptr)));
    REQUIRE(SUCCEEDED(wrapped_device->Reset(&pp)));
    REQUIRE(On12SpatialFixtureClose());ui_vertex.Reset();ui_pixel.Reset();
    std::printf("{\"passed\":true,\"checks\":%u,\"native_d3d12_frames\":%llu,\"checkouts\":%llu,\"returns\":%llu,\"antialiased_pixels\":%u,\"scale_2x\":true,\"aa_before_upscale\":true,\"native_ui_one_pixel\":true,\"scene_targets_smaller\":true,\"state_block_mapping\":true,\"in_place\":true,\"reset\":true,\"host_present_reset\":true,\"host_upscaling\":true,\"synthetic_only\":true}\n",
        checks, static_cast<unsigned long long>(stats.applied), static_cast<unsigned long long>(stats.checkouts), static_cast<unsigned long long>(stats.returns), softened);
    wrapped_device.Reset(); api.Reset(); DestroyWindow(window); return 0;
}
