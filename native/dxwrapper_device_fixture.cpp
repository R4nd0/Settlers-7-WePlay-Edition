// Standalone synthetic GPU check. Never loads game code or touches game profiles.
#include <windows.h>
#include <d3d9.h>
#include <d3d12.h>
#include <d3d9on12.h>
#include <cstdio>

int main() {
    const HINSTANCE instance=GetModuleHandleW(nullptr);
    WNDCLASSW wc{}; wc.lpfnWndProc=DefWindowProcW; wc.hInstance=instance;
    wc.lpszClassName=L"WePlayDxwrapperDeviceFixture";
    if(!RegisterClassW(&wc))return 1;
    HWND window=CreateWindowW(wc.lpszClassName,L"WePlay renderer device fixture",
        WS_OVERLAPPEDWINDOW,0,0,320,240,nullptr,nullptr,instance,nullptr);
    if(!window)return 2;
    HMODULE renderer=LoadLibraryW(L"d3d9.dll");
    if(!renderer)return 3;
    using Create=IDirect3D9* (WINAPI*)(UINT);
    const auto create=reinterpret_cast<Create>(GetProcAddress(renderer,"Direct3DCreate9"));
    if(!create)return 4;
    IDirect3D9* api=create(D3D_SDK_VERSION);
    if(!api)return 5;
    D3DPRESENT_PARAMETERS parameters{};
    parameters.BackBufferWidth=320; parameters.BackBufferHeight=240;
    parameters.BackBufferFormat=D3DFMT_X8R8G8B8;
    parameters.SwapEffect=D3DSWAPEFFECT_DISCARD; parameters.hDeviceWindow=window;
    parameters.Windowed=TRUE; parameters.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
    IDirect3DDevice9* device=nullptr;
    const HRESULT created=api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window,
        D3DCREATE_HARDWARE_VERTEXPROCESSING|D3DCREATE_MULTITHREADED|D3DCREATE_FPU_PRESERVE,
        &parameters,&device);
    if(FAILED(created)) {
        std::printf("{\"create_device_hr\":%ld}\n",static_cast<long>(created));
        api->Release();DestroyWindow(window);return 6;
    }
    IDirect3DDevice9On12* bridge=nullptr;
    const HRESULT queried=device->QueryInterface(__uuidof(IDirect3DDevice9On12),reinterpret_cast<void**>(&bridge));
    ID3D12Device* modern=nullptr;
    HRESULT modern_result=E_NOINTERFACE;
    UINT raytracing_tier=0;
    if(SUCCEEDED(queried) && bridge) {
        modern_result=bridge->GetD3D12Device(__uuidof(ID3D12Device),reinterpret_cast<void**>(&modern));
        if(SUCCEEDED(modern_result) && modern) {
            D3D12_FEATURE_DATA_D3D12_OPTIONS5 options{};
            if(SUCCEEDED(modern->CheckFeatureSupport(D3D12_FEATURE_D3D12_OPTIONS5,&options,sizeof(options))))
                raytracing_tier=static_cast<UINT>(options.RaytracingTier);
        }
    }
    bool pixels=true;
    for(UINT phase=0;phase<2;++phase) {
        const D3DCOLOR color=phase?0xff6e4628u:0xff28466eu;
        pixels=pixels && SUCCEEDED(device->Clear(0,nullptr,D3DCLEAR_TARGET,color,1.f,0));
        IDirect3DSurface9* target=nullptr;IDirect3DSurface9* staging=nullptr;
        pixels=pixels && SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&target));
        pixels=pixels && SUCCEEDED(device->CreateOffscreenPlainSurface(320,240,D3DFMT_X8R8G8B8,
            D3DPOOL_SYSTEMMEM,&staging,nullptr));
        pixels=pixels && target && staging && SUCCEEDED(device->GetRenderTargetData(target,staging));
        D3DLOCKED_RECT locked{};
        if(pixels && SUCCEEDED(staging->LockRect(&locked,nullptr,D3DLOCK_READONLY))) {
            const auto* row=static_cast<const unsigned char*>(locked.pBits)+120*locked.Pitch;
            const bool matched=(*reinterpret_cast<const DWORD*>(row+160*4)&0xffffffu)==(color&0xffffffu);
            const HRESULT unlocked=staging->UnlockRect();
            pixels=matched && SUCCEEDED(unlocked);
        } else pixels=false;
        if(staging)staging->Release();if(target)target->Release();
        pixels=pixels && SUCCEEDED(device->Present(nullptr,nullptr,nullptr,nullptr));
        if(phase==0)pixels=pixels && SUCCEEDED(device->Reset(&parameters));
    }
    std::printf("{\"create_device_hr\":%ld,\"on12_query_hr\":%ld,\"d3d12_device_hr\":%ld,"
        "\"raytracing_tier\":%u,\"pixels_present_reset_passed\":%s,\"synthetic_only\":true}\n",
        static_cast<long>(created),static_cast<long>(queried),static_cast<long>(modern_result),
        raytracing_tier,pixels?"true":"false");
    if(modern)modern->Release();if(bridge)bridge->Release();
    device->Release();api->Release();DestroyWindow(window);
    // Keep the wrapper loaded until process exit, including its static cleanup.
    return pixels?0:7;
}
