// Standalone GPU experiment. Never injects into or starts a game, changes a
// display mode, or replaces the pinned D3D9 runtime. Results apply to this host.
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d9.h>
#include <dwmapi.h>
#include <bcrypt.h>
#include <algorithm>
#include <array>
#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <filesystem>
#include <fstream>
#include <stdexcept>
#include <string>
#include <vector>

namespace {
constexpr char RendererHash[] = "265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa";
constexpr wchar_t WindowClass[] = L"WePlaySupersamplingProbe";

void Require(bool condition, const char* message) {
    if (!condition) throw std::runtime_error(message);
}
void Check(HRESULT result, const char* message) { Require(SUCCEEDED(result), message); }

template<class T> class Com {
    T* value_ = nullptr;
public:
    ~Com() { reset(); }
    Com() = default;
    Com(const Com&) = delete;
    Com& operator=(const Com&) = delete;
    T* get() const { return value_; }
    T* operator->() const { return value_; }
    T** put() { reset(); return &value_; }
    void reset() { if (value_) { value_->Release(); value_ = nullptr; } }
};

class Runtime {
    HANDLE file_ = INVALID_HANDLE_VALUE;
    HMODULE module_ = nullptr;
public:
    explicit Runtime(const std::filesystem::path& path) {
        Require(path.is_absolute(), "renderer_path_not_absolute");
        file_ = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ, nullptr,
                            OPEN_EXISTING, FILE_ATTRIBUTE_NORMAL, nullptr);
        Require(file_ != INVALID_HANDLE_VALUE, "renderer_file_unavailable");
        try {
            BCRYPT_ALG_HANDLE algorithm = nullptr;
            BCRYPT_HASH_HANDLE hash = nullptr;
            Require(BCryptOpenAlgorithmProvider(&algorithm, BCRYPT_SHA256_ALGORITHM, nullptr, 0) >= 0,
                    "hash_provider_failed");
            const auto status = BCryptCreateHash(algorithm, &hash, nullptr, 0, nullptr, 0, 0);
            if (status < 0) { BCryptCloseAlgorithmProvider(algorithm, 0); throw std::runtime_error("hash_create_failed"); }
            std::array<unsigned char, 65536> block{};
            std::array<unsigned char, 32> digest{};
            DWORD count = 0;
            bool valid = true;
            for (;;) {
                if (!ReadFile(file_, block.data(), static_cast<DWORD>(block.size()), &count, nullptr)) { valid = false; break; }
                if (!count) break;
                if (BCryptHashData(hash, block.data(), count, 0) < 0) { valid = false; break; }
            }
            valid = valid && BCryptFinishHash(hash, digest.data(), static_cast<ULONG>(digest.size()), 0) >= 0;
            BCryptDestroyHash(hash);
            BCryptCloseAlgorithmProvider(algorithm, 0);
            constexpr char hex[] = "0123456789abcdef";
            std::string actual;
            for (auto byte : digest) { actual += hex[byte >> 4]; actual += hex[byte & 15]; }
            Require(valid && actual == RendererHash, "renderer_sha256_mismatch");
            Require(!GetModuleHandleW(L"d3d9.dll"), "another_d3d9_already_loaded");
            module_ = LoadLibraryExW(path.c_str(), nullptr,
                LOAD_LIBRARY_SEARCH_DLL_LOAD_DIR | LOAD_LIBRARY_SEARCH_DEFAULT_DIRS);
            Require(module_ != nullptr, "renderer_load_failed");
            wchar_t loaded[32768]{};
            const DWORD length = GetModuleFileNameW(module_, loaded, 32768);
            Require(length && length < 32768
                && std::filesystem::equivalent(path, loaded), "loaded_renderer_path_mismatch");
        } catch (...) {
            if (module_) FreeLibrary(module_);
            CloseHandle(file_); file_ = INVALID_HANDLE_VALUE;
            throw;
        }
    }
    ~Runtime() { if (module_) FreeLibrary(module_); if (file_ != INVALID_HANDLE_VALUE) CloseHandle(file_); }
    IDirect3D9* create() const {
        using Factory = IDirect3D9* (WINAPI*)(UINT);
        const auto factory = reinterpret_cast<Factory>(GetProcAddress(module_, "Direct3DCreate9"));
        Require(factory != nullptr, "renderer_factory_missing");
        return factory(D3D_SDK_VERSION);
    }
};

class Window {
    HWND window_ = nullptr;
public:
    Window(UINT width, UINT height) {
        WNDCLASSW klass{};
        klass.lpfnWndProc = DefWindowProcW;
        klass.hInstance = GetModuleHandleW(nullptr);
        klass.lpszClassName = WindowClass;
        Require(RegisterClassW(&klass) != 0, "window_class_failed");
        // Only our own synthetic window is captured. No display mode changes.
        window_ = CreateWindowExW(WS_EX_TOOLWINDOW, WindowClass, L"WePlay GPU downsample probe",
            WS_POPUP, 32, 32, static_cast<int>(width), static_cast<int>(height),
            nullptr, nullptr, klass.hInstance, nullptr);
        Require(window_ != nullptr, "window_create_failed");
    }
    ~Window() { if (window_) DestroyWindow(window_); UnregisterClassW(WindowClass, GetModuleHandleW(nullptr)); }
    HWND get() const { return window_; }
    void resize(UINT width, UINT height) {
        Require(SetWindowPos(window_, nullptr, 0, 0, static_cast<int>(width), static_cast<int>(height),
            SWP_NOMOVE | SWP_NOACTIVATE | SWP_NOZORDER) != FALSE, "window_resize_failed");
    }
    void show() {
        Require(SetWindowPos(window_, HWND_TOPMOST, 0, 0, 0, 0,
            SWP_NOMOVE | SWP_NOSIZE | SWP_NOACTIVATE | SWP_SHOWWINDOW) != FALSE,
            "preview_show_failed");
    }
};

std::vector<std::uint32_t> Capture(HWND window, UINT width, UINT height) {
    POINT origin{};
    Require(ClientToScreen(window,&origin)!=FALSE,"client_screen_origin_failed");
    HDC source = GetDC(nullptr);
    Require(source != nullptr, "client_capture_dc_failed");
    HDC destination = CreateCompatibleDC(source);
    BITMAPINFO info{};
    info.bmiHeader.biSize=sizeof(BITMAPINFOHEADER);
    info.bmiHeader.biWidth=static_cast<LONG>(width);
    info.bmiHeader.biHeight=-static_cast<LONG>(height);
    info.bmiHeader.biPlanes=1; info.bmiHeader.biBitCount=32; info.bmiHeader.biCompression=BI_RGB;
    void* bits=nullptr;
    HBITMAP bitmap=destination?CreateDIBSection(source,&info,DIB_RGB_COLORS,&bits,nullptr,0):nullptr;
    if (!bitmap) {
        if (destination) DeleteDC(destination);
        ReleaseDC(nullptr,source);
        throw std::runtime_error("client_capture_bitmap_failed");
    }
    HGDIOBJ previous=SelectObject(destination,bitmap);
    const bool copied=previous && previous!=HGDI_ERROR
        && BitBlt(destination,0,0,static_cast<int>(width),static_cast<int>(height),
            source,origin.x,origin.y,SRCCOPY);
    GdiFlush();
    std::vector<std::uint32_t> pixels;
    if (copied) {
        const auto* begin=static_cast<const std::uint32_t*>(bits);
        pixels.assign(begin,begin+static_cast<size_t>(width)*height);
    }
    if (previous && previous!=HGDI_ERROR) SelectObject(destination,previous);
    DeleteObject(bitmap); DeleteDC(destination); ReleaseDC(nullptr,source);
    Require(copied,"client_capture_copy_failed");
    return pixels;
}

std::uint32_t Pattern(UINT x, UINT y, UINT width, UINT height) {
    const UINT marker = std::min(width, height) / 8;
    if (x < marker && y < marker) return 0xffff0000;
    if (x >= width-marker && y < marker) return 0xff00ff00;
    if (x < marker && y >= height-marker) return 0xff0000ff;
    if (x >= width-marker && y >= height-marker) return 0xffffff00;
    return ((x ^ y) & 1) ? 0xffffffff : 0xff000000;
}

std::vector<std::uint32_t> Readback(IDirect3DDevice9* device, IDirect3DSurface9* source,
                                    UINT width, UINT height) {
    Com<IDirect3DSurface9> system;
    Check(device->CreateOffscreenPlainSurface(width, height, D3DFMT_A8R8G8B8,
        D3DPOOL_SYSTEMMEM, system.put(), nullptr), "readback_allocation_failed");
    Check(device->GetRenderTargetData(source, system.get()), "gpu_readback_failed");
    D3DLOCKED_RECT locked{};
    Check(system->LockRect(&locked, nullptr, D3DLOCK_READONLY), "readback_lock_failed");
    std::vector<std::uint32_t> pixels(static_cast<size_t>(width)*height);
    for (UINT y = 0; y < height; ++y) {
        const auto* row = static_cast<const unsigned char*>(locked.pBits) + static_cast<size_t>(y)*locked.Pitch;
        std::memcpy(pixels.data()+static_cast<size_t>(y)*width, row, static_cast<size_t>(width)*4);
    }
    Check(system->UnlockRect(), "readback_unlock_failed");
    return pixels;
}

void Bitmap(const std::filesystem::path& path, const std::vector<std::uint32_t>& pixels,
            UINT width, UINT height) {
    Require(!std::filesystem::exists(path), "output_file_already_exists");
    BITMAPFILEHEADER file{};
    BITMAPINFOHEADER info{};
    const DWORD bytes = width*height*4;
    file.bfType = 0x4d42; file.bfOffBits = sizeof(file)+sizeof(info); file.bfSize = file.bfOffBits+bytes;
    info.biSize = sizeof(info); info.biWidth = static_cast<LONG>(width);
    info.biHeight = -static_cast<LONG>(height); info.biPlanes = 1; info.biBitCount = 32;
    info.biCompression = BI_RGB; info.biSizeImage = bytes;
    std::ofstream stream(path, std::ios::binary | std::ios::out);
    Require(stream.good(), "bitmap_open_failed");
    stream.write(reinterpret_cast<const char*>(&file), sizeof(file));
    stream.write(reinterpret_cast<const char*>(&info), sizeof(info));
    stream.write(reinterpret_cast<const char*>(pixels.data()), bytes);
    stream.close();
    Require(stream.good(), "bitmap_write_failed");
}

double Expected(UINT x, UINT y, UINT width, UINT height, UINT output_width, UINT output_height) {
    const double sx = (static_cast<double>(x)+.5)*width/output_width-.5;
    const double sy = (static_cast<double>(y)+.5)*height/output_height-.5;
    const UINT left = static_cast<UINT>(std::floor(sx)), top = static_cast<UINT>(std::floor(sy));
    const double fx = sx-left, fy = sy-top;
    const auto sample = [&](UINT px, UINT py) { return static_cast<double>(Pattern(px, py, width, height)&255); };
    return sample(left,top)*(1-fx)*(1-fy)+sample(left+1,top)*fx*(1-fy)
        +sample(left,top+1)*(1-fx)*fy+sample(left+1,top+1)*fx*fy;
}

struct Result {
    std::string name;
    UINT source_width, source_height, output_width, output_height, client_width, client_height;
    bool present_scaling = false, client_capture = false;
    unsigned corners = 0, samples = 0;
    double error = 0;
    double frame_error = 0;
    unsigned center = 0;
    bool presentation = false;
};

Result Downsample(IDirect3DDevice9* device, HWND window, UINT width, UINT height,
                  UINT numerator, UINT denominator, const std::filesystem::path& directory,
                  const char* label, bool present_scaling=false) {
    const UINT high_width = width*numerator/denominator, high_height = height*numerator/denominator;
    Com<IDirect3DSurface9> upload, high, backbuffer;
    Check(device->CreateOffscreenPlainSurface(high_width, high_height, D3DFMT_A8R8G8B8,
        D3DPOOL_SYSTEMMEM, upload.put(), nullptr), "upload_allocation_failed");
    D3DLOCKED_RECT locked{};
    Check(upload->LockRect(&locked, nullptr, 0), "upload_lock_failed");
    for (UINT y = 0; y < high_height; ++y) {
        auto* row = reinterpret_cast<std::uint32_t*>(static_cast<unsigned char*>(locked.pBits)
            + static_cast<size_t>(y)*locked.Pitch);
        for (UINT x = 0; x < high_width; ++x) row[x] = Pattern(x,y,high_width,high_height);
    }
    Check(upload->UnlockRect(), "upload_unlock_failed");
    Check(device->CreateRenderTarget(high_width, high_height, D3DFMT_A8R8G8B8,
        D3DMULTISAMPLE_NONE, 0, FALSE, high.put(), nullptr), "high_target_allocation_failed");
    Check(device->UpdateSurface(upload.get(), nullptr, high.get(), nullptr), "gpu_upload_failed");
    Check(device->GetBackBuffer(0, 0, D3DBACKBUFFER_TYPE_MONO, backbuffer.put()), "backbuffer_missing");
    D3DSURFACE_DESC source{}, output{};
    Check(high->GetDesc(&source), "source_description_failed");
    Check(backbuffer->GetDesc(&output), "output_description_failed");
    RECT client{};
    Require(GetClientRect(window,&client) != FALSE, "client_rectangle_failed");
    Require(source.Width == high_width && source.Height == high_height
        && output.Width == (present_scaling?high_width:width)
        && output.Height == (present_scaling?high_height:height)
        && client.right == static_cast<LONG>(width) && client.bottom == static_cast<LONG>(height),
        "surface_or_window_dimensions_mismatch");
    Check(device->StretchRect(high.get(), nullptr, backbuffer.get(), nullptr, D3DTEXF_LINEAR),
        "linear_gpu_downsample_failed");
    const auto source_pixels = Readback(device,high.get(),high_width,high_height);
    Require(source_pixels[static_cast<size_t>(high_height/2)*high_width+high_width/2]
        == Pattern(high_width/2,high_height/2,high_width,high_height), "source_pattern_readback_mismatch");
    // Preserve a GPU readback independently of the actual presented client.
    const auto gpu_pixels = Readback(device,backbuffer.get(),output.Width,output.Height);
    if (present_scaling) Require(gpu_pixels==source_pixels,"present_source_copy_changed_pixels");
    Bitmap(directory/(std::string(label)+"-source.bmp"),source_pixels,high_width,high_height);
    Bitmap(directory/(std::string(label)+"-backbuffer.bmp"),gpu_pixels,output.Width,output.Height);
    Check(device->Present(nullptr,nullptr,nullptr,nullptr), "presentation_failed");
    MSG message{};
    while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
    Check(DwmFlush(),"desktop_compositor_flush_failed");
    Sleep(100);
    const auto pixels = Capture(window,width,height);
    Result result{label,source.Width,source.Height,output.Width,output.Height,
        static_cast<UINT>(client.right),static_cast<UINT>(client.bottom)};
    result.present_scaling=present_scaling;
    result.presentation=true;
    const std::array<std::array<UINT,2>,4> at{{{{4,4}},{{width-5,4}},{{4,height-5}},{{width-5,height-5}}}};
    const std::array<std::uint32_t,4> colors{{0x00ff0000,0x0000ff00,0x000000ff,0x00ffff00}};
    for (size_t i=0;i<at.size();++i)
        if ((pixels[static_cast<size_t>(at[i][1])*width+at[i][0]]&0xffffff) == colors[i]) ++result.corners;
    Require(result.corners == 4, "frame_corners_not_preserved");
    for (UINT y=height/2-4;y<height/2+4;++y) for (UINT x=width/2-4;x<width/2+4;++x) {
        const auto rgb=pixels[static_cast<size_t>(y)*width+x];
        const double expected=Expected(x,y,high_width,high_height,width,height);
        for (unsigned shift : {0u,8u,16u}) {
            result.error=std::max(result.error,std::abs(static_cast<double>((rgb>>shift)&255)-expected));
            ++result.samples;
        }
    }
    if (result.error>3.0) std::fprintf(stderr,"case=%s center=%u expected=%.3f max_error=%.3f\n",
        label,pixels[static_cast<size_t>(height/2)*width+width/2]&255,
        Expected(width/2,height/2,high_width,high_height,width,height),result.error);
    if (result.error>3.0) for (UINT y=height/2-2;y<height/2+2;++y) {
        for (UINT x=width/2-4;x<width/2+4;++x)
            std::fprintf(stderr,"%u/%.0f ",pixels[static_cast<size_t>(y)*width+x]&255,
                Expected(x,y,high_width,high_height,width,height));
        std::fputc('\n',stderr);
    }
    Require(result.error <= 3.0 || present_scaling, "filtered_pixels_do_not_match_bilinear_reference");
    // Reject any occluded/foreign pixels before persisting the client capture.
    // Full-frame comparison also checks all four edges, not merely the centre.
    double frame_error=0;
    for (UINT y=0;y<height;++y) for (UINT x=0;x<width;++x) {
        const auto rgb=pixels[static_cast<size_t>(y)*width+x];
        const double sx=(static_cast<double>(x)+.5)*high_width/width-.5;
        const double sy=(static_cast<double>(y)+.5)*high_height/height-.5;
        const UINT left=static_cast<UINT>(std::floor(sx)),top=static_cast<UINT>(std::floor(sy));
        const double fx=sx-left,fy=sy-top;
        for (unsigned shift:{0u,8u,16u}) {
            const auto channel=[&](UINT px,UINT py) {
                return static_cast<double>((Pattern(px,py,high_width,high_height)>>shift)&255);
            };
            const double expected=channel(left,top)*(1-fx)*(1-fy)+channel(left+1,top)*fx*(1-fy)
                +channel(left,top+1)*(1-fx)*fy+channel(left+1,top+1)*fx*fy;
            frame_error=std::max(frame_error,std::abs(static_cast<double>((rgb>>shift)&255)-expected));
        }
    }
    Require(frame_error<=3.0 || present_scaling,"presented_client_does_not_match_complete_reference");
    result.frame_error=frame_error;
    result.client_capture=frame_error<=3.0;
    result.center=pixels[static_cast<size_t>(height/2)*width+width/2]&255;
    // A failed direct-Present reference remains a measured failure. Do not
    // persist that screen crop: only independently identified fixture frames
    // are written. Known source/backbuffer readbacks remain available.
    if (result.client_capture) Bitmap(directory/(std::string(label)+"-output.bmp"),pixels,width,height);
    return result;
}

void Print(const std::vector<Result>& results, const char* device_name, bool reset, bool invalid_reset) {
    // Adapter names can contain quotes; keep the receipt stable and bounded.
    std::string adapter;
    bool direct_passed=true;
    for (const auto& result:results) if (result.present_scaling && !result.client_capture) direct_passed=false;
    for (size_t i=0;device_name[i] && i<128;++i) {
        const char c=device_name[i];
        adapter += (c >= 32 && c <=126 && c!='"' && c!='\\') ? c : '_';
    }
    std::printf("{\"format\":\"s7-supersampling-gpu-probe-v1\",\"game_tested\":false,"
        "\"renderer_sha256\":\"%s\",\"adapter\":\"%s\",\"filter\":\"D3DTEXF_LINEAR\","
        "\"resize_reset_passed\":%s,\"invalid_reset_rejected\":%s,"
        "\"device_loss_tested\":false,\"probe_completed\":true,\"direct_present_bilinear_passed\":%s,\"cases\":[",RendererHash,adapter.c_str(),
        reset?"true":"false",invalid_reset?"true":"false",direct_passed?"true":"false");
    for (size_t i=0;i<results.size();++i) {
        const auto& r=results[i];
        std::printf("%s{\"name\":\"%s\",\"source\":[%u,%u],\"backbuffer\":[%u,%u],"
            "\"client\":[%u,%u],\"downsample_path\":\"%s\",\"client_capture_verified\":%s,\"preserved_corners\":%u,\"reference_samples\":%u,"
            "\"max_channel_error\":%.4f,\"full_frame_max_channel_error\":%.4f,\"center_luma\":%u,\"presented\":%s}",
            i?",":"",r.name.c_str(),r.source_width,r.source_height,r.output_width,r.output_height,
            r.client_width,r.client_height,r.present_scaling?"windowed_present":"explicit_stretchrect",
            r.client_capture?"true":"false",r.corners,r.samples,r.error,r.frame_error,r.center,r.presentation?"true":"false");
    }
    std::puts("]}");
}
}

int wmain(int argc,wchar_t** argv) {
    try {
        Require(argc==3 || (argc==4 && !std::wcscmp(argv[3],L"--show")),
            "usage_supersampling_probe_absolute_dxvk_d3d9_path_new_output_directory_optional_show");
        const auto output=std::filesystem::absolute(argv[2]);
        Require(!std::filesystem::exists(output) && std::filesystem::create_directory(output),
            "use_a_new_output_directory_with_existing_parent");
        SetProcessDPIAware();
        Runtime runtime(std::filesystem::absolute(argv[1]));
        Com<IDirect3D9> api;
        *api.put()=runtime.create();
        Require(api.get()!=nullptr,"d3d9_creation_failed");
        D3DADAPTER_IDENTIFIER9 adapter{};
        Check(api->GetAdapterIdentifier(D3DADAPTER_DEFAULT,0,&adapter),"adapter_query_failed");
        Window window(640,360);
        window.show();
        D3DPRESENT_PARAMETERS parameters{};
        parameters.Windowed=TRUE; parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;
        parameters.hDeviceWindow=window.get(); parameters.BackBufferWidth=640; parameters.BackBufferHeight=360;
        parameters.BackBufferCount=1; parameters.BackBufferFormat=D3DFMT_A8R8G8B8;
        parameters.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
        Com<IDirect3DDevice9> device;
        Check(api->CreateDevice(D3DADAPTER_DEFAULT,D3DDEVTYPE_HAL,window.get(),
            D3DCREATE_HARDWARE_VERTEXPROCESSING,&parameters,device.put()),"d3d9_device_creation_failed");
        D3DCAPS9 caps{};
        Check(device->GetDeviceCaps(&caps),"device_caps_failed");
        Require((caps.StretchRectFilterCaps&D3DPTFILTERCAPS_MINFLINEAR)!=0,
            "linear_minification_unsupported");
        std::vector<Result> results;
        results.push_back(Downsample(device.get(),window.get(),640,360,1,1,output,"native-1x"));
        results.push_back(Downsample(device.get(),window.get(),640,360,3,2,output,"supersample-1_5x"));
        results.push_back(Downsample(device.get(),window.get(),640,360,2,1,output,"supersample-2x"));
        // Release every default-pool reference before each Reset. Test the
        // proposed high-backbuffer/small-client path separately from StretchRect.
        parameters.BackBufferWidth=1280; parameters.BackBufferHeight=720;
        Check(device->Reset(&parameters),"present_2x_reset_failed");
        results.push_back(Downsample(device.get(),window.get(),640,360,2,1,output,"present-2x",true));
        parameters.BackBufferWidth=960; parameters.BackBufferHeight=540;
        Check(device->Reset(&parameters),"present_1_5x_reset_failed");
        results.push_back(Downsample(device.get(),window.get(),640,360,3,2,output,"present-1_5x",true));
        // Deliberately invalid backbuffer count checks API rejection without
        // creating a real device-loss condition or touching a global display mode.
        auto invalid=parameters; invalid.BackBufferCount=4;
        const HRESULT invalid_result=device->Reset(&invalid);
        const bool invalid_reset=FAILED(invalid_result);
        Require(invalid_reset,"invalid_reset_was_not_rejected");
        window.resize(800,450);
        parameters.BackBufferWidth=800; parameters.BackBufferHeight=450;
        Check(device->Reset(&parameters),"resize_reset_failed");
        results.push_back(Downsample(device.get(),window.get(),800,450,2,1,output,"resized-2x"));
        Check(device->TestCooperativeLevel(),"device_not_ready_after_reset");
        if (argc==4) {
            // Repaint after showing; this is bounded and never takes focus.
            const ULONGLONG until=GetTickCount64()+5000;
            while (GetTickCount64()<until) {
                MSG message{};
                while (PeekMessageW(&message,nullptr,0,0,PM_REMOVE)) { TranslateMessage(&message); DispatchMessageW(&message); }
                if (!IsWindow(window.get())) break;
                Sleep(16);
            }
        }
        Print(results,adapter.Description,true,invalid_reset);
        return 0;
    } catch (const std::exception& error) {
        std::fprintf(stderr,"supersampling_probe: %s\n",error.what());
        return 1;
    }
}
