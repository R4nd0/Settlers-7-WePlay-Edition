// Opt-in laboratory bridge. Original scene targets remain high-resolution;
// only presentation uses an output-sized additional swapchain and GPU blit.
#include "supersampling_hook.h"
#include "supersampling_hook_config.h"
#include "material_shader_observer.h"
#include "supersampling_hook_capture.h"
#include "atmosphere_fp.h"
#include <bcrypt.h>
#include <array>
#include <cstdint>
#include <cstdio>
#include <cstring>
#include <string>
#include <vector>
#include <intrin.h>
#include <limits>

namespace SupersamplingHook {
constexpr char RendererHash[]="265888c31ca78dffa290c39cb7e50bfb02762590e41927906e46fb32f01497fa";
struct ExecutableProfile {DWORD edition;const char* sha256;};
constexpr ExecutableProfile Executables[]{
    {1,"4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f"},
    {2,"8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410"},
    {3,"76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab"}};
constexpr char BaseHash[]="adaedb323c7e79abd758ac7237d9ed5eb6cdb43a95ef6329c4b88bbcfb2c7c0d";
constexpr char VisionHash[]="d950f1bdb11d83a9d4c6c7e1937b7d7ef747dff884e4e6b6e70e72ce7ed330a1";
// This specific Windows compatibility shim intercepts four D3D9 methods on the
// laboratory machine. It is not an exemption for arbitrary wrappers/modules.
constexpr char ApphelpHash[]="34655b97cbcaa5b93733da1d9118e0663735c442eb0016671a98389f3cde863e";
constexpr char GetterName[]="?GetD3DDevice@VVideo@@SAPAUIDirect3DDevice9@@XZ";
using Getter=IDirect3DDevice9* (__cdecl*)();
using Release=ULONG (STDMETHODCALLTYPE*)(IDirect3DDevice9*);
using Present=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,const RECT*,const RECT*,HWND,const RGNDATA*);
using Reset=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRESENT_PARAMETERS*);
using SetTarget=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,DWORD,IDirect3DSurface9*);
using SetViewport=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,const D3DVIEWPORT9*);
using Draw=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,UINT,UINT);
using DrawIndexed=HRESULT (STDMETHODCALLTYPE*)(IDirect3DDevice9*,D3DPRIMITIVETYPE,INT,UINT,UINT,UINT,UINT);

struct State {
    IDirect3DDevice9* device=nullptr;
    void** original=nullptr;
    void** vtable=nullptr;
    std::array<void*,134> table{};
    IDirect3DSwapChain9* output=nullptr;
    HWND window=nullptr;
    DWORD thread=0;
    bool resize_posted=false;
    bool logical_window_active=false;
    bool shutdown_requested=false;
    bool terminal_cleanup=false;
#ifdef S7_SUPERSAMPLING_FIXTURE
    bool fail_next_present=false;
#endif
};
SupersamplingConfig configuration{};
NativeGraphicsConfig::Version2 native_configuration{};
constexpr DWORD CompiledCapabilities=NativeGraphicsConfig::Observe;
DWORD capture_pending=0,capture_last_sequence=0;
SupersamplingStats stats{sizeof(SupersamplingStats),1};
SRWLOCK state_lock=SRWLOCK_INIT;
State* state=nullptr;
thread_local State* internal=nullptr;
Getter original_getter=nullptr;
HMODULE renderer=nullptr;
HMODULE apphelp=nullptr;
std::vector<HANDLE> pinned_files;
volatile LONG initialized=0;
volatile LONG initialization_ready=0;
std::array<void*,134> saved_methods{};
WNDPROC original_window_proc=nullptr;
UINT resize_message=0;
constexpr LPARAM ResizeToken=0x37535750;
constexpr UINT_PTR ResizeTimer=0x37535750;
std::array<HMODULE,3> game_modules{};
decltype(&::GetClientRect) physical_rect=::GetClientRect;
decltype(&::ScreenToClient) physical_to_client=::ScreenToClient;
decltype(&::ClientToScreen) physical_to_screen=::ClientToScreen;

struct Lock { Lock(){AcquireSRWLockExclusive(&state_lock);} ~Lock(){ReleaseSRWLockExclusive(&state_lock);} };
struct Internal { State* before; explicit Internal(State* value):before(internal){internal=value;} ~Internal(){internal=before;} };
struct LastErrorScope {DWORD saved=GetLastError();~LastErrorScope(){SetLastError(saved);}};
struct DiagnosticScope {LastErrorScope error;atmosphere::FpScope fp;};
struct OriginalCallScope {
    LastErrorScope error;atmosphere::FpScope fp;
    void before(){if(fp.valid)fesetenv(&fp.saved);_mm_setcsr(fp.mxcsr);SetLastError(error.saved);}
    void returned(){error.saved=GetLastError();fp.mxcsr=_mm_getcsr();fp.valid=fegetenv(&fp.saved)==0;}
};

bool Range(const void* pointer,size_t bytes,bool writable=false) {
    DiagnosticScope diagnostic;
    MEMORY_BASIC_INFORMATION page{};
    if(!pointer || !bytes || VirtualQuery(pointer,&page,sizeof(page))!=sizeof(page)
        || page.State!=MEM_COMMIT || (page.Protect&(PAGE_NOACCESS|PAGE_GUARD)))return false;
    const auto begin=reinterpret_cast<uintptr_t>(pointer),base=reinterpret_cast<uintptr_t>(page.BaseAddress);
    if(begin<base || bytes>page.RegionSize || begin-base>page.RegionSize-bytes)return false;
    return !writable || (page.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
}

bool ModuleFunction(const void* pointer,size_t slot) {
    MEMORY_BASIC_INFORMATION page{};
    const bool valid=pointer && VirtualQuery(pointer,&page,sizeof(page))==sizeof(page)
        && page.State==MEM_COMMIT && page.Type==MEM_IMAGE
        && (page.AllocationBase==renderer || (apphelp && page.AllocationBase==apphelp
            && (slot==0 || slot==2 || slot==13 || slot==16)))
        && !(page.Protect&(PAGE_NOACCESS|PAGE_GUARD))
        && (page.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY));
#ifdef S7_SUPERSAMPLING_FIXTURE
    if(!valid) {
        wchar_t module[32768]{};
        const DWORD count=GetModuleFileNameW(static_cast<HMODULE>(page.AllocationBase),module,32768);
        const wchar_t* name=count?std::wcsrchr(module,L'\\'):nullptr;
        std::fwprintf(stderr,L"COM function guard: module=%ls state=%lu type=%lu protect=%lu same_module=%u\n",
            name?name+1:L"unknown",page.State,page.Type,page.Protect,page.AllocationBase==renderer?1u:0u);
    }
#endif
    return valid;
}

bool PinIdentity(HMODULE module,const char* expected) {
    wchar_t path[32768]{};
    const DWORD length=module?GetModuleFileNameW(module,path,32768):0;
    if(!length || length>=32768)return false;
    HANDLE file=CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return false;
    BCRYPT_ALG_HANDLE algorithm=nullptr; BCRYPT_HASH_HANDLE hash=nullptr;
    bool valid=BCryptOpenAlgorithmProvider(&algorithm,BCRYPT_SHA256_ALGORITHM,nullptr,0)>=0;
    if(valid)valid=BCryptCreateHash(algorithm,&hash,nullptr,0,nullptr,0,0)>=0;
    std::array<unsigned char,65536> bytes{}; std::array<unsigned char,32> digest{};
    DWORD count=0;
    while(valid) {
        if(!ReadFile(file,bytes.data(),static_cast<DWORD>(bytes.size()),&count,nullptr)){valid=false;break;}
        if(!count)break;
        valid=BCryptHashData(hash,bytes.data(),count,0)>=0;
    }
    if(valid)valid=BCryptFinishHash(hash,digest.data(),static_cast<ULONG>(digest.size()),0)>=0;
    if(hash)BCryptDestroyHash(hash);
    if(algorithm)BCryptCloseAlgorithmProvider(algorithm,0);
    constexpr char hex[]="0123456789abcdef";
    char actual[65]{};
    for(size_t i=0;i<digest.size();++i){actual[i*2]=hex[digest[i]>>4];actual[i*2+1]=hex[digest[i]&15];}
    if(valid)valid=!std::strcmp(actual,expected);
    HMODULE pinned=nullptr;
    if(valid)valid=GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_PIN,path,&pinned) && pinned==module;
    if(valid)pinned_files.push_back(file); else CloseHandle(file);
    return valid;
}

void Report() {
    DiagnosticScope diagnostic;
    if(!configuration.report_path[0])return;
    char text[4096]{};
    const int count=std::snprintf(text,sizeof(text),
        "{\"format\":\"s7-supersampling-hook-v1\",\"lab_only\":true,\"draw_counter_semantics\":\"successful_nonzero\",\"attached\":%lu,"
        "\"present_calls\":%lu,\"output_present_calls\":%lu,\"fallback_calls\":%lu,"
        "\"reset_calls\":%lu,\"reset_successes\":%lu,\"release_calls\":%lu,\"swapchain_creations\":%lu,"
        "\"source\":[%lu,%lu],\"output\":[%lu,%lu],\"client\":[%lu,%lu],"
        "\"viewport\":[%lu,%lu],\"selected_target\":[%lu,%lu],"
        "\"high_resolution_draws\":%lu,\"other_resolution_draws\":%lu,\"indexed_draws\":%lu,"
        "\"last_reset_requested\":[%lu,%lu],\"last_hresult\":%lu,\"last_failure\":%lu,"
        "\"hook_detached\":%lu,\"zero_release_returns\":%lu,\"resize_queued\":%lu,"
        "\"resize_applied\":%lu,\"resize_failure\":%lu,\"before_client\":[%lu,%lu],\"after_client\":[%lu,%lu],"
        "\"logical_size_messages\":%lu,\"logical_rect_calls\":%lu,\"logical_point_calls\":%lu,"
        "\"logical_mouse_messages\":%lu,\"logical_api_slots\":%lu,\"logical_context_rejections\":%lu,"
        "\"edition\":%lu,\"cached_release_calls\":%lu,\"output_cache_drops\":%lu,\"window_shutdowns\":%lu,\"terminal_cleanup\":%lu,"
        "\"capture_requests\":%lu,\"capture_completed\":%lu,\"capture_failures\":%lu,\"capture_frame\":%lu}\n",
        stats.attached,stats.present_calls,stats.output_present_calls,stats.fallback_calls,
        stats.reset_calls,stats.reset_successes,stats.release_calls,stats.swapchain_creations,
        stats.source_width,stats.source_height,stats.output_width,stats.output_height,
        stats.client_width,stats.client_height,stats.viewport_width,stats.viewport_height,
        stats.target_width,stats.target_height,stats.high_resolution_draws,stats.other_resolution_draws,
        stats.indexed_draws,stats.last_reset_requested_width,stats.last_reset_requested_height,
        stats.last_hresult,stats.last_failure,stats.detached,stats.zero_release_returns,
        stats.resize_queued,stats.resize_applied,stats.resize_failure,stats.before_client_width,
        stats.before_client_height,stats.after_client_width,stats.after_client_height,
        stats.logical_size_messages,stats.logical_rect_calls,stats.logical_point_calls,
        stats.logical_mouse_messages,stats.logical_api_slots,stats.logical_context_rejections,
        stats.edition,stats.cached_release_calls,stats.output_cache_drops,stats.window_shutdowns,stats.terminal_cleanup,
        stats.capture_requests,stats.capture_completed,stats.capture_failures,stats.capture_frame);
    if(count<=0 || count>=static_cast<int>(sizeof(text)))return;
    HANDLE file=CreateFileW(configuration.report_path,GENERIC_WRITE,FILE_SHARE_READ,nullptr,CREATE_ALWAYS,
        FILE_ATTRIBUTE_NORMAL,nullptr);
    if(file==INVALID_HANDLE_VALUE)return;
    DWORD written=0; WriteFile(file,text,static_cast<DWORD>(count),&written,nullptr); CloseHandle(file);
}

void DropOutput(State& current) {
    if(current.output){auto* old=current.output;current.output=nullptr;++stats.output_cache_drops;old->Release();}
}
bool TerminalCleanup(State& current);

bool OwnedWindow(HWND window) {
    DWORD pid=0;
    return window && IsWindow(window) && GetWindowThreadProcessId(window,&pid) && pid==GetCurrentProcessId();
}

bool GameCaller(const void* address) {
    DiagnosticScope diagnostic;
    MEMORY_BASIC_INFORMATION memory{};
    if(!address || VirtualQuery(address,&memory,sizeof(memory))!=sizeof(memory)
        || memory.State!=MEM_COMMIT || memory.Type!=MEM_IMAGE
        || !(memory.Protect&(PAGE_EXECUTE|PAGE_EXECUTE_READ|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY)))return false;
    for(auto module:game_modules)if(module && memory.AllocationBase==module)return true;
    return false;
}

bool LogicalContext(State* current,HWND window,bool caller) {
    DiagnosticScope diagnostic;
    RECT actual{};
    return caller && current && current->logical_window_active && current->window==window
        && OwnedWindow(window) && !IsIconic(window) && physical_rect(window,&actual)
        && actual.right==static_cast<LONG>(configuration.output_width)
        && actual.bottom==static_cast<LONG>(configuration.output_height);
}

bool MapPoint(POINT& point,bool to_physical) {
    const auto coordinate=[](LONG value,DWORD numerator,DWORD denominator,LONG& output) {
        int64_t result=static_cast<int64_t>(value)*numerator;
        result+=(result<0?-1:1)*static_cast<int64_t>(denominator/2);
        result/=denominator;
        if(result<std::numeric_limits<LONG>::min() || result>std::numeric_limits<LONG>::max())return false;
        output=static_cast<LONG>(result);return true;
    };
    POINT mapped{};
    if(!coordinate(point.x,to_physical?configuration.output_width:configuration.render_width,
            to_physical?configuration.render_width:configuration.output_width,mapped.x)
        || !coordinate(point.y,to_physical?configuration.output_height:configuration.render_height,
            to_physical?configuration.render_height:configuration.output_height,mapped.y))return false;
    point=mapped;return true;
}

BOOL LogicalRect(HWND window,RECT* rectangle,bool caller) {
    const BOOL result=physical_rect(window,rectangle);
    if(!result)return result;
    DiagnosticScope diagnostic;
    const auto apply=[&](State* current) {
        if(LogicalContext(current,window,caller)) {
            rectangle->left=rectangle->top=0;
            rectangle->right=static_cast<LONG>(configuration.render_width);
            rectangle->bottom=static_cast<LONG>(configuration.render_height);++stats.logical_rect_calls;
        } else ++stats.logical_context_rejections;
    };
    if(internal)apply(internal);else {Lock lock;apply(state);}
    return result;
}

BOOL LogicalPoint(HWND window,POINT* point,bool caller,bool to_screen) {
    if(!Range(point,sizeof(*point),true))return to_screen?physical_to_screen(window,point):physical_to_client(window,point);
    const auto apply=[&](State* current) {
        if(!LogicalContext(current,window,caller)) {
            ++stats.logical_context_rejections;
            return to_screen?physical_to_screen(window,point):physical_to_client(window,point);
        }
        POINT mapped=*point;
        if(to_screen) {
            if(!MapPoint(mapped,true)){SetLastError(ERROR_ARITHMETIC_OVERFLOW);return FALSE;}
            if(!physical_to_screen(window,&mapped))return FALSE;
        } else {
            if(!physical_to_client(window,&mapped))return FALSE;
            if(!MapPoint(mapped,false)){SetLastError(ERROR_ARITHMETIC_OVERFLOW);return FALSE;}
        }
        *point=mapped;++stats.logical_point_calls;return TRUE;
    };
    if(internal)return apply(internal);
    Lock lock;return apply(state);
}

BOOL WINAPI HookClientRect(HWND window,RECT* rectangle) {return LogicalRect(window,rectangle,GameCaller(_ReturnAddress()));}
BOOL WINAPI HookScreenToClient(HWND window,POINT* point) {return LogicalPoint(window,point,GameCaller(_ReturnAddress()),false);}
BOOL WINAPI HookClientToScreen(HWND window,POINT* point) {return LogicalPoint(window,point,GameCaller(_ReturnAddress()),true);}

bool ClientMouseMessage(UINT message) {
    return message==WM_MOUSEMOVE || (message>=WM_LBUTTONDOWN && message<=WM_MBUTTONDBLCLK)
        || message==WM_XBUTTONDOWN || message==WM_XBUTTONUP || message==WM_XBUTTONDBLCLK;
}

LRESULT CALLBACK WindowProcedure(HWND window,UINT message,WPARAM wparam,LPARAM lparam) {
    OriginalCallScope original_state;
    if(message==WM_DESTROY || message==WM_NCDESTROY) {
        const auto request=[&](State* current) {
            if(!current || current->window!=window || current->terminal_cleanup)return;
            if(!current->shutdown_requested)++stats.window_shutdowns;
            current->shutdown_requested=true;current->logical_window_active=false;
        };
        if(internal)request(internal); // Finish an in-flight COM call before releasing its swapchain.
        else {
            Lock lock;request(state);
            if(state && state->window==window){Internal inside(state);TerminalCleanup(*state);}
        }
    }
    if(message==WM_SIZE || ClientMouseMessage(message)) {
        const auto map=[&](State* current) {
            if(!LogicalContext(current,window,true))return;
            if(message==WM_SIZE) {
                if(wparam!=SIZE_MINIMIZED && LOWORD(lparam)==configuration.output_width
                    && HIWORD(lparam)==configuration.output_height) {
                    lparam=MAKELPARAM(configuration.render_width,configuration.render_height);
                    ++stats.logical_size_messages;
                }
            } else {
                POINT point{static_cast<short>(LOWORD(lparam)),static_cast<short>(HIWORD(lparam))};
                if(MapPoint(point,false) && point.x>=-32768 && point.x<=32767 && point.y>=-32768 && point.y<=32767) {
                    lparam=MAKELPARAM(static_cast<WORD>(point.x),static_cast<WORD>(point.y));
                    ++stats.logical_mouse_messages;
                }
            }
        };
        if(internal)map(internal);else {Lock lock;map(state);}
    }
    const bool request=message==resize_message && wparam==GetCurrentProcessId() && lparam==ResizeToken;
    const bool retry=message==WM_TIMER && wparam==ResizeTimer;
    if(request || retry) {
        // Some present implementations pump window messages. Never resize while
        // inside a COM hook/lock; defer to a later owned-window-thread timer.
        if(internal) {
            SetTimer(window,ResizeTimer,50,nullptr);
            return 0;
        }
        if(retry)KillTimer(window,ResizeTimer);
        State* current=nullptr;
        RECT rectangle{0,0,static_cast<LONG>(configuration.output_width),static_cast<LONG>(configuration.output_height)};
        {
            Lock lock;
            if(!state || state->window!=window || !OwnedWindow(window) || stats.resize_applied)return 0;
            current=state;
            current->logical_window_active=true;
            RECT client{};
            if(!GetClientRect(window,&client)){stats.resize_failure=1;Report();return 0;}
            stats.before_client_width=static_cast<DWORD>(client.right);
            stats.before_client_height=static_cast<DWORD>(client.bottom);
            const DWORD style=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_STYLE));
            const DWORD extended=static_cast<DWORD>(GetWindowLongPtrW(window,GWL_EXSTYLE));
            if(!AdjustWindowRectEx(&rectangle,style,GetMenu(window)!=nullptr,extended)) {
                stats.resize_failure=2;Report();return 0;
            }
        }
        // WM_SIZE and an original Reset may run synchronously here. No renderer
        // lock/TLS scope is held; the scoped procedure supplies logical render
        // dimensions while DXVK continues to observe the actual small client.
        const BOOL applied=SetWindowPos(window,nullptr,0,0,rectangle.right-rectangle.left,
            rectangle.bottom-rectangle.top,SWP_NOMOVE|SWP_NOACTIVATE|SWP_NOZORDER);
        {
            Lock lock;
            if(state!=current || !OwnedWindow(window))return 0;
            RECT client{};
            if(!applied || !GetClientRect(window,&client))stats.resize_failure=3;
            else {
                stats.after_client_width=static_cast<DWORD>(client.right);
                stats.after_client_height=static_cast<DWORD>(client.bottom);
                if(client.right==static_cast<LONG>(configuration.output_width)
                    && client.bottom==static_cast<LONG>(configuration.output_height))stats.resize_applied=1;
                else stats.resize_failure=4;
            }
            Report();
        }
        return 0;
    }
    original_state.before();
    const LRESULT returned=original_window_proc?CallWindowProcW(original_window_proc,window,message,wparam,lparam)
        :DefWindowProcW(window,message,wparam,lparam);
    original_state.returned();return returned;
}

void QueueWindowTransition(State& current) {
    if(configuration.render_width==configuration.output_width && configuration.render_height==configuration.output_height)return;
    if(current.resize_posted || !stats.high_resolution_draws
        || stats.source_width!=configuration.render_width || stats.source_height!=configuration.render_height)return;
    DiagnosticScope diagnostic; // Preserve the original draw's post-call state on every Win32 path below.
    DWORD pid=0;
    const DWORD owner_thread=GetWindowThreadProcessId(current.window,&pid);
    if(pid!=GetCurrentProcessId() || owner_thread!=GetCurrentThreadId())return;
    if(!original_window_proc) {
        const auto previous=reinterpret_cast<WNDPROC>(GetWindowLongPtrW(current.window,GWLP_WNDPROC));
        if(!previous){stats.resize_failure=5;Report();return;}
        SetLastError(ERROR_SUCCESS);
        const auto replaced=reinterpret_cast<WNDPROC>(SetWindowLongPtrW(current.window,GWLP_WNDPROC,
            reinterpret_cast<LONG_PTR>(WindowProcedure)));
        if(replaced!=previous) {
            if(replaced)SetWindowLongPtrW(current.window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(replaced));
            stats.resize_failure=6;Report();return;
        }
        original_window_proc=previous;
    }
    if(PostMessageW(current.window,resize_message,GetCurrentProcessId(),ResizeToken)) {
        current.resize_posted=true;++stats.resize_queued;
    } else stats.resize_failure=7;
    Report();
}

void Target(State& current) {
    DiagnosticScope diagnostic;
    IDirect3DSurface9* target=nullptr; D3DSURFACE_DESC desc{};
    if(SUCCEEDED(current.device->GetRenderTarget(0,&target)) && target) {
        if(SUCCEEDED(target->GetDesc(&desc))){stats.target_width=desc.Width;stats.target_height=desc.Height;}
        target->Release();
    }
    D3DVIEWPORT9 viewport{};
    if(SUCCEEDED(current.device->GetViewport(&viewport))) {
        stats.viewport_width=viewport.Width;stats.viewport_height=viewport.Height;
    }
}

void CountDraw(bool indexed) {
    if(indexed)++stats.indexed_draws;
    if(stats.target_width==configuration.render_width && stats.target_height==configuration.render_height
        && stats.viewport_width==configuration.render_width && stats.viewport_height==configuration.render_height)
        ++stats.high_resolution_draws;
    else ++stats.other_resolution_draws;
}

ULONG STDMETHODCALLTYPE HookRelease(IDirect3DDevice9* device) {
    if(internal && internal->device==device)return reinterpret_cast<Release>(internal->original[2])(device);
    Lock lock;
    if(!state || state->device!=device)return reinterpret_cast<Release>(saved_methods[2])(device);
    State* current=state;
    const auto release=reinterpret_cast<Release>(current->original[2]);
    ULONG count=0;
    {
        Internal inside(current);
        if(native_configuration.flags&NativeGraphicsConfig::Observe)MaterialShaderObserver::Reset(device);
        // Keep the output chain across ordinary temporary references. Its public
        // child reference deliberately retains the parent until Reset, fallback,
        // explicit shutdown, or the owned window's verified destruction event.
        // Never infer object death solely from the advisory Release return.
        if(current->output)++stats.cached_release_calls;
        ++stats.release_calls;
        count=release(device);
    }
    // Release's numeric return is advisory. A zero was observed in an earlier
    // invalid SDK-sized-vtable clone experiment; its cause was not isolated.
    // Retain this bounded laboratory state independently of that return value.
    if(!count)++stats.zero_release_returns;
    return count;
}

HRESULT STDMETHODCALLTYPE HookReset(IDirect3DDevice9* device,D3DPRESENT_PARAMETERS* parameters) {
    if(internal && internal->device==device)return reinterpret_cast<Reset>(internal->original[16])(device,parameters);
    Lock lock;
    if(!state || state->device!=device)return reinterpret_cast<Reset>(saved_methods[16])(device,parameters);
    OriginalCallScope original_state;
    Internal inside(state);
    if(native_configuration.flags&NativeGraphicsConfig::Observe)MaterialShaderObserver::Reset(device);
    DropOutput(*state);
    ++stats.reset_calls;
    if(Range(parameters,sizeof(*parameters))) {
        stats.last_reset_requested_width=parameters->BackBufferWidth;
        stats.last_reset_requested_height=parameters->BackBufferHeight;
    }
    // Preserve the engine's actual Reset request. Logical window queries and
    // messages must make the engine configure its own high-resolution scene;
    // changing only the backbuffer would not establish that scene resolution.
    original_state.before();
    const HRESULT result=reinterpret_cast<Reset>(state->original[16])(device,parameters);
    original_state.returned();
    stats.last_hresult=static_cast<DWORD>(result);
    if(SUCCEEDED(result)){++stats.reset_successes;Target(*state);}
    Report();return result;
}

HRESULT STDMETHODCALLTYPE HookPresent(IDirect3DDevice9* device,const RECT* source,const RECT* destination,
                                      HWND override_window,const RGNDATA* dirty) {
    if(internal && internal->device==device)return reinterpret_cast<Present>(internal->original[17])
        (device,source,destination,override_window,dirty);
    Lock lock;
    if(!state || state->device!=device)return reinterpret_cast<Present>(saved_methods[17])(device,source,destination,override_window,dirty);
    OriginalCallScope original_state;
    Internal inside(state);auto& current=*state;
    if(native_configuration.flags&NativeGraphicsConfig::Observe)MaterialShaderObserver::EndFrame(device);
    if(current.terminal_cleanup) {
        original_state.before();
        const HRESULT returned=reinterpret_cast<Present>(current.original[17])
            (device,source,destination,override_window,dirty);
        original_state.returned();return returned;
    }
    ++stats.present_calls;
    NativeGraphicsCapture::Result captured{};bool capture_attempted=false;
    if(configuration.render_width==configuration.output_width && configuration.render_height==configuration.output_height) {
        if(capture_pending) {
            IDirect3DSurface9* backbuffer=nullptr;
            if(SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer)) && backbuffer) {
                D3DSURFACE_DESC description{};RECT physical{};
                if(SUCCEEDED(backbuffer->GetDesc(&description))) {
                    stats.source_width=stats.output_width=description.Width;
                    stats.source_height=stats.output_height=description.Height;
                }
                if(physical_rect(current.window,&physical)) {
                    stats.client_width=static_cast<DWORD>(physical.right);stats.client_height=static_cast<DWORD>(physical.bottom);
                }
                Target(current);
                NativeGraphicsCapture::Read(device,backbuffer,backbuffer,configuration.report_path,capture_pending,stats,captured);
                backbuffer->Release();capture_attempted=true;
            } else ++stats.capture_failures;
            capture_pending=0;
        }
        original_state.before();
        const HRESULT original=reinterpret_cast<Present>(current.original[17])(device,source,destination,override_window,dirty);
        original_state.returned();
        if(capture_attempted) {
            NativeGraphicsCapture::Finish(captured,original,original,false,false);stats.capture_frame=stats.present_calls;
            if(captured.status)++stats.capture_failures;else ++stats.capture_completed;
        }
        stats.last_hresult=static_cast<DWORD>(original);stats.last_failure=0;
        if(current.shutdown_requested)TerminalCleanup(current);
        if(stats.present_calls==1 || stats.present_calls%60==0)Report();
        return original;
    }
    HRESULT result=D3DERR_INVALIDCALL;
    DWORD failure=0;
    IDirect3DSurface9* high=nullptr;IDirect3DSurface9* low=nullptr;
    D3DSURFACE_DESC desc{};RECT client{};
    if(GetCurrentThreadId()!=current.thread)failure=1;
    else if(source || destination || override_window || dirty)failure=2;
    else if(!OwnedWindow(current.window) || !GetClientRect(current.window,&client))failure=3;
    else {
        stats.client_width=static_cast<DWORD>(client.right);stats.client_height=static_cast<DWORD>(client.bottom);
        if(client.right!=static_cast<LONG>(configuration.output_width)
            || client.bottom!=static_cast<LONG>(configuration.output_height))failure=4;
        else if(FAILED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&high)) || !high)failure=5;
        else if(FAILED(high->GetDesc(&desc)))failure=6;
        else {
            stats.source_width=desc.Width;stats.source_height=desc.Height;
            if(desc.Width!=configuration.render_width || desc.Height!=configuration.render_height
                || desc.MultiSampleType!=D3DMULTISAMPLE_NONE)failure=7;
            else if(!current.output) {
                D3DPRESENT_PARAMETERS parameters{};
                parameters.Windowed=TRUE;parameters.SwapEffect=D3DSWAPEFFECT_DISCARD;
                parameters.hDeviceWindow=current.window;parameters.BackBufferCount=1;
                parameters.BackBufferWidth=configuration.output_width;
                parameters.BackBufferHeight=configuration.output_height;
                parameters.BackBufferFormat=desc.Format;
                parameters.PresentationInterval=D3DPRESENT_INTERVAL_IMMEDIATE;
                result=device->CreateAdditionalSwapChain(&parameters,&current.output);
                if(FAILED(result) || !current.output)failure=8;
                else ++stats.swapchain_creations;
            }
            if(!failure) {
                result=current.output->GetBackBuffer(0,D3DBACKBUFFER_TYPE_MONO,&low);
                if(FAILED(result) || !low)failure=9;
                else {
                    D3DSURFACE_DESC output{};
                    result=low->GetDesc(&output);
                    if(FAILED(result) || output.Width!=configuration.output_width
                        || output.Height!=configuration.output_height)failure=10;
                    else {
                        stats.output_width=output.Width;stats.output_height=output.Height;
                        result=device->StretchRect(high,nullptr,low,nullptr,D3DTEXF_LINEAR);
                        if(FAILED(result))failure=11;
                        else if(capture_pending && !current.shutdown_requested) {
                            NativeGraphicsCapture::Read(device,high,low,configuration.report_path,capture_pending,stats,captured);
                            capture_pending=0;capture_attempted=true;
                        }
                    }
                }
            }
        }
    }
    if(low)low->Release();if(high)high->Release();
    HRESULT filtered_result=D3DERR_INVALIDCALL;bool fallback_used=false;
    if(!failure) {
        original_state.before();
#ifdef S7_SUPERSAMPLING_FIXTURE
        if(current.fail_next_present){current.fail_next_present=false;result=D3DERR_INVALIDCALL;}
        else
#endif
        result=current.output->Present(nullptr,nullptr,nullptr,nullptr,0);
        original_state.returned();filtered_result=result;
        if(SUCCEEDED(result))++stats.output_present_calls;
        else failure=12;
    }
    if(failure && !current.shutdown_requested) {
        ++stats.fallback_calls;DropOutput(current);
        original_state.before();fallback_used=true;
        result=reinterpret_cast<Present>(current.original[17])(device,source,destination,override_window,dirty);
        original_state.returned();
    }
    stats.last_hresult=static_cast<DWORD>(result);stats.last_failure=failure;
    if(capture_attempted) {
        NativeGraphicsCapture::Finish(captured,filtered_result,result,fallback_used,true);stats.capture_frame=stats.present_calls;
        if(captured.status)++stats.capture_failures;else ++stats.capture_completed;
    }
    if(current.shutdown_requested)TerminalCleanup(current);
    if(stats.present_calls==1 || stats.present_calls%60==0 || failure)Report();
    return result;
}

HRESULT STDMETHODCALLTYPE HookTarget(IDirect3DDevice9* device,DWORD slot,IDirect3DSurface9* surface) {
    if(internal && internal->device==device)return reinterpret_cast<SetTarget>(internal->original[37])(device,slot,surface);
    Lock lock;if(!state || state->device!=device)return reinterpret_cast<SetTarget>(saved_methods[37])(device,slot,surface);
    Internal inside(state);const HRESULT result=reinterpret_cast<SetTarget>(state->original[37])(device,slot,surface);
    if(SUCCEEDED(result) && slot==0)Target(*state);
    return result;
}
HRESULT STDMETHODCALLTYPE HookViewport(IDirect3DDevice9* device,const D3DVIEWPORT9* viewport) {
    if(internal && internal->device==device)return reinterpret_cast<SetViewport>(internal->original[47])(device,viewport);
    Lock lock;if(!state || state->device!=device)return reinterpret_cast<SetViewport>(saved_methods[47])(device,viewport);
    Internal inside(state);const HRESULT result=reinterpret_cast<SetViewport>(state->original[47])(device,viewport);
    if(SUCCEEDED(result) && Range(viewport,sizeof(*viewport))) {
        stats.viewport_width=viewport->Width;stats.viewport_height=viewport->Height;
    }
    return result;
}
HRESULT STDMETHODCALLTYPE HookDraw(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,UINT first,UINT count) {
    if(internal && internal->device==device)return reinterpret_cast<Draw>(internal->original[81])(device,type,first,count);
    Lock lock;if(!state || state->device!=device)return reinterpret_cast<Draw>(saved_methods[81])(device,type,first,count);
    Internal inside(state);
    const HRESULT result=reinterpret_cast<Draw>(state->original[81])(device,type,first,count);
    if(SUCCEEDED(result) && count)CountDraw(false);
    if(native_configuration.flags&NativeGraphicsConfig::Observe)MaterialShaderObserver::ObserveDraw(device,MaterialShaderObserver::Draw,result);
    if(SUCCEEDED(result) && count)QueueWindowTransition(*state);
    return result;
}
HRESULT STDMETHODCALLTYPE HookDrawIndexed(IDirect3DDevice9* device,D3DPRIMITIVETYPE type,INT base,
                                          UINT min,UINT vertices,UINT first,UINT count) {
    if(internal && internal->device==device)return reinterpret_cast<DrawIndexed>(internal->original[82])
        (device,type,base,min,vertices,first,count);
    Lock lock;if(!state || state->device!=device)return reinterpret_cast<DrawIndexed>(saved_methods[82])(device,type,base,min,vertices,first,count);
    Internal inside(state);
    const HRESULT result=reinterpret_cast<DrawIndexed>(state->original[82])(device,type,base,min,vertices,first,count);
    if(SUCCEEDED(result) && count && vertices)CountDraw(true);
    if(native_configuration.flags&NativeGraphicsConfig::Observe)MaterialShaderObserver::ObserveDraw(device,MaterialShaderObserver::Indexed,result);
    if(SUCCEEDED(result) && count && vertices)QueueWindowTransition(*state);
    return result;
}

const std::array<std::pair<size_t,void*>,7> Intercepts{{
    {2,reinterpret_cast<void*>(HookRelease)},{16,reinterpret_cast<void*>(HookReset)},
    {17,reinterpret_cast<void*>(HookPresent)},{37,reinterpret_cast<void*>(HookTarget)},
    {47,reinterpret_cast<void*>(HookViewport)},{81,reinterpret_cast<void*>(HookDraw)},
    {82,reinterpret_cast<void*>(HookDrawIndexed)}}};

bool Exchange(void** address,void* before,void* after) {
    DWORD protection=0,unused=0;
    if(!VirtualProtect(address,sizeof(void*),PAGE_READWRITE,&protection))return false;
    const bool changed=InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(address),after,before)==before;
    const bool restored=VirtualProtect(address,sizeof(void*),protection,&unused)!=FALSE;
    if(changed && !restored) {
        InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(address),before,after);
        VirtualProtect(address,sizeof(void*),protection,&unused);
    }
    return changed && restored;
}

bool TerminalCleanup(State& current) {
    DiagnosticScope diagnostic;
    if(current.terminal_cleanup)return true;
    current.shutdown_requested=true;current.logical_window_active=false;
    if(native_configuration.flags&NativeGraphicsConfig::Observe) {
        MaterialShaderObserver::Reset(current.device);MaterialShaderObserver::Flush();MaterialShaderObserver::Shutdown();
    }
    bool restored=true;
    for(size_t i=Intercepts.size();i;i--) {
        const auto& intercept=Intercepts[i-1];
        if(!Exchange(current.vtable+intercept.first,intercept.second,current.table[intercept.first]))restored=false;
    }
    // The exact class vtable lives in a pinned image; restoring it does not
    // dereference a potentially released object. Drop the public child only
    // after restoration, so a final parent destructor uses its original table.
    DropOutput(current);
    if(original_window_proc && OwnedWindow(current.window)
        && reinterpret_cast<WNDPROC>(GetWindowLongPtrW(current.window,GWLP_WNDPROC))==WindowProcedure) {
        KillTimer(current.window,ResizeTimer);
        SetLastError(ERROR_SUCCESS);
        const LONG_PTR previous=SetWindowLongPtrW(current.window,GWLP_WNDPROC,reinterpret_cast<LONG_PTR>(original_window_proc));
        if((!previous && GetLastError()!=ERROR_SUCCESS)
            || reinterpret_cast<WNDPROC>(GetWindowLongPtrW(current.window,GWLP_WNDPROC))!=original_window_proc)restored=false;
    }
    current.terminal_cleanup=true;stats.terminal_cleanup=1;stats.detached=restored?1u:0u;stats.attached=0;
    if(!restored)stats.last_failure=110;
    Report();return restored;
}

bool Attach(IDirect3DDevice9* device) {
    if(!device || !Range(device,sizeof(void*),true)){stats.last_failure=100;Report();return false;}
    Lock lock;
    const auto fail=[](DWORD reason){stats.last_failure=reason;Report();return false;};
    if(state) {
        if(state->terminal_cleanup)return false;
        if(state->device==device && *reinterpret_cast<void***>(device)==state->vtable)return true;
        return fail(109); // A new device epoch requires a clean lab process.
    }
    auto** original=*reinterpret_cast<void***>(device);
    if(!Range(original,119*sizeof(void*)))return fail(101);
    MEMORY_BASIC_INFORMATION table_memory{};
    if(VirtualQuery(original,&table_memory,sizeof(table_memory))!=sizeof(table_memory)
        || table_memory.Type!=MEM_IMAGE || (table_memory.AllocationBase!=renderer
            && (!apphelp || table_memory.AllocationBase!=apphelp)))return fail(111);
    bool methods_valid=true;
    for(size_t i=0;i<119;++i)if(!ModuleFunction(original[i],i)) {
#ifdef S7_SUPERSAMPLING_FIXTURE
        std::fprintf(stderr,"guarded_index=%zu\n",i);
#endif
        stats.last_hresult=static_cast<DWORD>(i);methods_valid=false;
    }
    if(!methods_valid)return fail(102);
    size_t slots=119;
    IDirect3DDevice9Ex* extended=nullptr;
    const HRESULT query=device->QueryInterface(__uuidof(IDirect3DDevice9Ex),reinterpret_cast<void**>(&extended));
    if(SUCCEEDED(query)) {
        const bool same=extended && static_cast<IDirect3DDevice9*>(extended)==device;
        if(extended)extended->Release();
        if(!same || !Range(original,134*sizeof(void*)))return fail(103);
        slots=134;
        for(size_t i=119;i<slots;++i)if(!ModuleFunction(original[i],i)){stats.last_hresult=static_cast<DWORD>(i);return fail(104);}
    }
    IDirect3DSwapChain9* chain=nullptr;D3DPRESENT_PARAMETERS parameters{};
    if(FAILED(device->GetSwapChain(0,&chain)) || !chain)return fail(105);
    const HRESULT described=chain->GetPresentParameters(&parameters);chain->Release();
    if(FAILED(described) || !parameters.Windowed || !OwnedWindow(parameters.hDeviceWindow))return fail(106);
    auto* current=new State;
    current->device=device;current->vtable=original;current->original=current->table.data();current->window=parameters.hDeviceWindow;
    current->thread=GetCurrentThreadId();
    for(size_t i=0;i<slots;++i)current->table[i]=saved_methods[i]=original[i];
    // Preserve the original implementation vptr, RTTI prefix and every compiler
    // private/extended slot. Replacing an SDK-sized cloned vtable is unsafe for
    // DXVK's MinGW C++ implementation even when QueryInterface rejects D3D9Ex.
    size_t patched=0;
    for(;patched<Intercepts.size();++patched) {
        const auto& intercept=Intercepts[patched];
        if(!Exchange(original+intercept.first,current->table[intercept.first],intercept.second))break;
    }
    if(patched!=Intercepts.size()) {
        for(size_t i=patched;i;i--) {
            const auto& intercept=Intercepts[i-1];
            Exchange(original+intercept.first,intercept.second,current->table[intercept.first]);
        }
        delete current;return fail(107);
    }
    state=current;stats.attached=1;
    {Internal inside(current);Target(*current);
        IDirect3DSurface9* backbuffer=nullptr;D3DSURFACE_DESC description{};
        if(SUCCEEDED(device->GetBackBuffer(0,0,D3DBACKBUFFER_TYPE_MONO,&backbuffer)) && backbuffer) {
            if(SUCCEEDED(backbuffer->GetDesc(&description))) {
                stats.source_width=description.Width;stats.source_height=description.Height;
            }
            backbuffer->Release();
        }
    }
    Report();return true;
}

IDirect3DDevice9* __cdecl HookGetter() {
    IDirect3DDevice9* device=original_getter();
    DiagnosticScope diagnostic;
    if(!internal) {
        try {Attach(device);}
        catch(...) {stats.last_failure=108;Report();}
    }
    return device;
}

struct Slot {void** address;void* original;void* replacement;};
bool Slots(HMODULE module,std::vector<Slot>& slots,const char* library,const char* function,
           void* expected,void* replacement) {
    auto* image=reinterpret_cast<unsigned char*>(module);
    const auto* dos=reinterpret_cast<IMAGE_DOS_HEADER*>(image);
    if(!Range(dos,sizeof(*dos)) || dos->e_magic!=IMAGE_DOS_SIGNATURE || dos->e_lfanew<64 || dos->e_lfanew>4096)return false;
    const auto* nt=reinterpret_cast<IMAGE_NT_HEADERS32*>(image+dos->e_lfanew);
    if(!Range(nt,sizeof(*nt)) || nt->Signature!=IMAGE_NT_SIGNATURE || nt->OptionalHeader.Magic!=IMAGE_NT_OPTIONAL_HDR32_MAGIC)return false;
    const DWORD size=nt->OptionalHeader.SizeOfImage;
    const auto valid=[&](DWORD rva,size_t bytes){return rva<size && bytes<=size-rva && Range(image+rva,bytes);};
    const auto directory=nt->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if(!valid(directory.VirtualAddress,directory.Size))return false;
    auto* entry=reinterpret_cast<IMAGE_IMPORT_DESCRIPTOR*>(image+directory.VirtualAddress);
    for(size_t n=0;n<directory.Size/sizeof(*entry) && entry[n].Name;++n) {
        auto& item=entry[n];if(!valid(item.Name,std::strlen(library)+1))return false;
        if(_stricmp(reinterpret_cast<char*>(image+item.Name),library))continue;
        if(!item.OriginalFirstThunk)return false;
        for(DWORD i=0;;++i) {
            const DWORD offset=i*sizeof(IMAGE_THUNK_DATA32);
            if(!valid(item.OriginalFirstThunk+offset,sizeof(IMAGE_THUNK_DATA32))
                || !valid(item.FirstThunk+offset,sizeof(IMAGE_THUNK_DATA32)))return false;
            const auto thunk=reinterpret_cast<IMAGE_THUNK_DATA32*>(image+item.OriginalFirstThunk+offset)->u1.AddressOfData;
            if(!thunk)break;
            if(IMAGE_SNAP_BY_ORDINAL32(thunk))continue;
            if(!valid(thunk,sizeof(WORD)+std::strlen(function)+1))return false;
            const auto* name=reinterpret_cast<IMAGE_IMPORT_BY_NAME*>(image+thunk);
            if(std::memcmp(name->Name,function,std::strlen(function)+1))continue;
            auto** address=reinterpret_cast<void**>(image+item.FirstThunk+offset);
            if(*address!=expected)return false;
            slots.push_back({address,*address,replacement});
        }
    }
    return true;
}

bool SetSlot(const Slot& slot,bool forward) {
    DWORD protection=0,unused=0;
    if(!VirtualProtect(slot.address,sizeof(void*),PAGE_READWRITE,&protection))return false;
    void* before=forward?slot.original:slot.replacement;
    void* after=forward?slot.replacement:slot.original;
    const bool changed=InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(slot.address),after,before)==before;
    const bool restored=VirtualProtect(slot.address,sizeof(void*),protection,&unused)!=FALSE;
    if(changed && !restored) {
        // Do not leave this current slot outside the caller's rollback count.
        InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(slot.address),before,after);
        VirtualProtect(slot.address,sizeof(void*),protection,&unused);
    }
    return changed && restored;
}
}

extern "C" DWORD WINAPI WePlaySupersamplingInitialize(void* opaque) {
    using namespace SupersamplingHook;
    if(InterlockedCompareExchange(&initialized,1,0))return 0;
    SIZE_T read=0;DWORD header[2]{};
    if(!ReadProcessMemory(GetCurrentProcess(),opaque,header,sizeof(header),&read) || read!=sizeof(header)
        || (header[0]!=sizeof(SupersamplingConfig) && header[0]!=sizeof(NativeGraphicsConfig::Version2)))return 0;
    std::array<unsigned char,sizeof(NativeGraphicsConfig::Version2)> bytes{};
    if(!ReadProcessMemory(GetCurrentProcess(),opaque,bytes.data(),header[0],&read) || read!=header[0]
        || NativeGraphicsConfig::Decode(bytes.data(),header[0],CompiledCapabilities,native_configuration)!=NativeGraphicsConfig::Error::None)return 0;
    configuration=native_configuration.base;
    renderer=GetModuleHandleW(L"d3d9.dll");
    if(!PinIdentity(renderer,RendererHash)){stats.last_failure=201;Report();return 0;}
    apphelp=GetModuleHandleW(L"apphelp.dll");
    if(apphelp) {
        wchar_t loaded[32768]{},directory[32768]{},system[32768]{};
        const DWORD length=GetModuleFileNameW(apphelp,loaded,32768);
        const UINT count=GetSystemWow64DirectoryW(directory,32768);
        const UINT system_count=GetSystemDirectoryW(system,32768);
        const std::wstring expected=std::wstring(directory)+L"\\apphelp.dll";
        const std::wstring system_alias=std::wstring(system)+L"\\apphelp.dll";
        if(!length || length>=32768 || !count || count>=32768
            || !system_count || system_count>=32768
            || (_wcsicmp(loaded,expected.c_str()) && _wcsicmp(loaded,system_alias.c_str()))
            || !PinIdentity(apphelp,ApphelpHash)){stats.last_failure=202;Report();return 0;}
    }
    HMODULE self=nullptr;
    if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,
        reinterpret_cast<const wchar_t*>(&WePlaySupersamplingInitialize),&self)){stats.last_failure=203;Report();return 0;}
    resize_message=RegisterWindowMessageW(L"WePlay.Supersampling.OutputWindow.v1");
    if(!resize_message){stats.last_failure=204;Report();return 0;}
    const HMODULE user32=GetModuleHandleW(L"user32.dll");
    physical_rect=reinterpret_cast<decltype(physical_rect)>(GetProcAddress(user32,"GetClientRect"));
    physical_to_client=reinterpret_cast<decltype(physical_to_client)>(GetProcAddress(user32,"ScreenToClient"));
    physical_to_screen=reinterpret_cast<decltype(physical_to_screen)>(GetProcAddress(user32,"ClientToScreen"));
    if(!physical_rect || !physical_to_client || !physical_to_screen){stats.last_failure=205;Report();return 0;}
    if((native_configuration.flags&NativeGraphicsConfig::Observe)
        && !MaterialShaderObserver::Initialize(native_configuration.catalog_path,native_configuration.catalog_sha256,
            native_configuration.observer_report_path)){stats.last_failure=208;Report();return 0;}
#ifndef S7_SUPERSAMPLING_FIXTURE
    HMODULE executable=GetModuleHandleW(nullptr),base=GetModuleHandleW(L"vbase90.dll"),vision=GetModuleHandleW(L"vision90.dll");
    for(const auto& profile:Executables)if(PinIdentity(executable,profile.sha256)){stats.edition=profile.edition;break;}
    if(!stats.edition || !PinIdentity(base,BaseHash) || !PinIdentity(vision,VisionHash))return 0;
    game_modules={{executable,base,vision}};
    original_getter=reinterpret_cast<Getter>(GetProcAddress(base,GetterName));
    if(!original_getter)return 0;
    std::vector<Slot> slots;
    if(!Slots(executable,slots,"vbase90.dll",GetterName,reinterpret_cast<void*>(original_getter),reinterpret_cast<void*>(HookGetter))
        || !Slots(vision,slots,"vbase90.dll",GetterName,reinterpret_cast<void*>(original_getter),reinterpret_cast<void*>(HookGetter))
        || slots.size()!=2)return 0;
    const size_t getters=slots.size();
    for(auto module:game_modules) {
        if(!Slots(module,slots,"USER32.dll","GetClientRect",reinterpret_cast<void*>(physical_rect),reinterpret_cast<void*>(HookClientRect))
            || !Slots(module,slots,"USER32.dll","ScreenToClient",reinterpret_cast<void*>(physical_to_client),reinterpret_cast<void*>(HookScreenToClient))
            || !Slots(module,slots,"USER32.dll","ClientToScreen",reinterpret_cast<void*>(physical_to_screen),reinterpret_cast<void*>(HookClientToScreen))) {
            stats.last_failure=206;Report();return 0;
        }
    }
    stats.logical_api_slots=static_cast<DWORD>(slots.size()-getters);
    if(!stats.logical_api_slots){stats.last_failure=207;Report();return 0;}
    size_t applied=0;
    for(;applied<slots.size();++applied)if(!SetSlot(slots[applied],true))break;
    if(applied!=slots.size()) {
        for(size_t i=applied;i;i--)SetSlot(slots[i-1],false);
        return 0;
    }
#else
    game_modules[0]=GetModuleHandleW(nullptr);
#endif
    GraphicsObservation::Initialize(); // Readiness/availability is recorded per actual capture.
    Report();InterlockedExchange(&initialization_ready,1);return 1;
}

extern "C" DWORD WINAPI WePlayNativeGraphicsConfigureObservation(void* opaque) {
    using namespace SupersamplingHook;
    static_assert(sizeof(MaterialShaderObserver::CaptureConfig)==952,"Observer configuration ABI changed.");
    DiagnosticScope diagnostic;
    MaterialShaderObserver::CaptureConfig config{};SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),opaque,&config,sizeof(config),&read) || read!=sizeof(config)
        || config.size!=sizeof(config) || config.version!=1)return 0;
    Lock lock;
    if(!initialization_ready || !(native_configuration.flags&NativeGraphicsConfig::Observe)
        || (state && state->terminal_cleanup))return 0;
    if(state){Internal inside(state);return MaterialShaderObserver::ConfigureCapture(&config)?1u:0u;}
    return MaterialShaderObserver::ConfigureCapture(&config)?1u:0u;
}

extern "C" DWORD WINAPI WePlayNativeGraphicsRequestCapture(void* opaque) {
    using namespace SupersamplingHook;
    NativeGraphicsCapture::Request request{};SIZE_T read=0;
    if(!ReadProcessMemory(GetCurrentProcess(),opaque,&request,sizeof(request),&read) || read!=sizeof(request)
        || request.size!=sizeof(request) || request.version!=1 || !request.sequence)return 0;
    Lock lock;
    if(!state || !stats.attached || state->shutdown_requested || state->terminal_cleanup || capture_pending
        || request.sequence<=capture_last_sequence || !NativeGraphicsCapture::TargetAvailable(configuration.report_path,request.sequence))return 0;
    capture_pending=request.sequence;capture_last_sequence=request.sequence;++stats.capture_requests;Report();return 1;
}

extern "C" DWORD WINAPI WePlaySupersamplingReadStats(void* output) {
    using namespace SupersamplingHook;
    if(!output)return 0;
    Lock lock;SIZE_T written=0;
    return WriteProcessMemory(GetCurrentProcess(),output,&stats,sizeof(stats),&written)
        && written==sizeof(stats)?1u:0u;
}

#ifdef S7_SUPERSAMPLING_FIXTURE
extern "C" DWORD WINAPI WePlaySupersamplingAttach(IDirect3DDevice9* device) {
    return SupersamplingHook::Attach(device)?1u:0u;
}
extern "C" DWORD WINAPI WePlaySupersamplingDetach(IDirect3DDevice9* device) {
    using namespace SupersamplingHook;
    Lock lock;
    if(!state || state->device!=device || GetCurrentThreadId()!=state->thread)return 0;
    Internal inside(state);
    if(*reinterpret_cast<void***>(device)!=state->vtable)return 0;
    if(!TerminalCleanup(*state))return 0;
    // State/table storage is intentionally retained until process exit. No
    // callback is invalidated while a caller could still be returning from it.
    state=nullptr;stats.attached=0;stats.detached=1;Report();return 1;
}
extern "C" BOOL WINAPI WePlaySupersamplingFixtureRect(HWND window,RECT* rectangle,const void* caller) {
    return SupersamplingHook::LogicalRect(window,rectangle,SupersamplingHook::GameCaller(caller));
}
extern "C" BOOL WINAPI WePlaySupersamplingFixturePoint(HWND window,POINT* point,const void* caller,BOOL to_screen) {
    return SupersamplingHook::LogicalPoint(window,point,SupersamplingHook::GameCaller(caller),to_screen!=FALSE);
}
extern "C" DWORD WINAPI WePlaySupersamplingFixtureFailPresent() {
    using namespace SupersamplingHook;
    Lock lock;if(!state || !state->output)return 0;state->fail_next_present=true;return 1;
}
#endif
