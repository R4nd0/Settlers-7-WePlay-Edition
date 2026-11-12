#include "standalone_gate.h"
#include "standalone_image.h"
#include "standalone_profile.h"
#include <vector>

bool InstallOfflineNetworkGuard();
DWORD ProbeStandaloneKernelIsolation();
#ifdef S7_GOLD_STARTUP
extern "C" DWORD WINAPI GoldInitialize(void*);
#endif
namespace {
struct Context { uint32_t magic,version,size,pid,tid,image,entry,edition; };
volatile LONG entered = 0;
HMODULE module = nullptr;
constexpr char UbisoftHash[] = "4ef51632a8a193050ebe8ef3f3fe38a9b240b4aa57ff62a4f381bc0f539df64f";
constexpr char GoldHash[] = "76ae5c8a9f74d997a662d347803398cf39ea034e5344725c0ccb0a1ea94bf5ab";
bool ContextValid(const Context* context) {
    MEMORY_BASIC_INFORMATION info{};
    if (!context || VirtualQuery(context,&info,sizeof(info)) != sizeof(info) || info.Type != MEM_PRIVATE
        || info.State != MEM_COMMIT || info.Protect != PAGE_READWRITE
        || reinterpret_cast<uintptr_t>(info.AllocationBase)!=reinterpret_cast<uintptr_t>(context)-32
        || reinterpret_cast<uintptr_t>(context)-reinterpret_cast<uintptr_t>(info.BaseAddress) > info.RegionSize-sizeof(Context)) return false;
    return context->magic == 0x37535057 && context->version == 1 && context->size == sizeof(Context)
        && context->pid == GetCurrentProcessId() && context->tid == GetCurrentThreadId()
        && context->image == reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) && StandaloneLaunchGate();
}
}
extern "C" __declspec(dllexport) DWORD WINAPI WePlayInitializeStandalone(void* opaque) {
    if (InterlockedCompareExchange(&entered,1,0)) return 0;
    const auto* context = static_cast<const Context*>(opaque);
    if (!ContextValid(context)) return 0;
    wchar_t path[MAX_PATH]{};
    DWORD length = GetModuleFileNameW(nullptr,path,MAX_PATH);
    if (!length || length >= MAX_PATH) return 0;
    HANDLE file = CreateFileW(path,GENERIC_READ,FILE_SHARE_READ,nullptr,OPEN_EXISTING,
        FILE_FLAG_OPEN_REPARSE_POINT | FILE_FLAG_SEQUENTIAL_SCAN,nullptr);
    if (file == INVALID_HANDLE_VALUE) return 0;
    std::vector<unsigned char> bytes;
    const bool read = StandaloneImage::ReadLockedFile(file,20*1024*1024,bytes);
    CloseHandle(file);
    if (!read) return 0;
    uint32_t entry = 0;
    StandaloneProfile::Edition edition = StandaloneProfile::Edition::History;
#ifdef S7_GOLD_STARTUP
    if (context->edition != 3 || context->entry != 0x7aee5b
        || !StandaloneImage::Matches(bytes.data(),bytes.size(),GoldHash)) return 0;
    entry = context->entry; edition = StandaloneProfile::Edition::Gold;
#else
    if (context->edition == 1 && context->entry == 0x622fb7
        && StandaloneImage::Matches(bytes.data(),bytes.size(),UbisoftHash)) entry = context->entry;
    else if (context->edition == 2 && context->entry == 0x10e3310) {
        StandaloneImage::SteamText text;
        StandaloneImage::Error error{};
        if (!StandaloneImage::DecodeSteam(bytes,text,error) || !StandaloneImage::InstallSteam(context->image,text,error)) return 0;
        entry = text.entry;
    } else return 0;
#endif
    const DWORD kernel_probe=ProbeStandaloneKernelIsolation();
    if (!(kernel_probe&1)) return 0;
    *reinterpret_cast<DWORD*>(reinterpret_cast<uintptr_t>(context)-12)=kernel_probe;
    if (!StandaloneProfile::Prepare(path,edition,module) || !InstallOfflineNetworkGuard()) return 0;
#ifdef S7_GOLD_STARTUP
    if (GoldInitialize(nullptr) != 1) return 0;
#endif
    return context->image+entry;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    module = instance;
    return StandaloneLaunchGate() ? TRUE : FALSE;
}
