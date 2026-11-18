#include "standalone_gate.h"
#include "standalone_image.h"
#include "standalone_profile.h"
#include "crossplay_production_context.h"
#include "crossplay_production_ui.h"
#if defined(S7_CROSSPLAY_ROOM_LOBBY_LAB) && !defined(S7_GOLD_STARTUP)
#include "crossplay_room_instance.h"
#endif
#include <vector>

bool InstallOfflineNetworkGuard();
#ifdef S7_GOLD_STARTUP
bool ConfigureGoldLoopbackGuard(unsigned int slot);
#endif
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
#if defined(S7_CROSSPLAY_ROOM_LOBBY_LAB) && !defined(S7_GOLD_STARTUP)
bool IsolateHistoryRoomInstance(const Context& context,Crossplay::Edition edition) {
    using namespace CrossplayProduction;
    const auto rva=HistoryRoomInstanceNameRva(edition);
    if(!rva)return false;
    auto* literal=reinterpret_cast<char*>(context.image+rva);
    MEMORY_BASIC_INFORMATION page{};
    if(VirtualQuery(literal,&page,sizeof(page))!=sizeof(page)
        || reinterpret_cast<uintptr_t>(page.AllocationBase)!=context.image || page.Type!=MEM_IMAGE
        || page.State!=MEM_COMMIT || (page.Protect&(PAGE_GUARD|PAGE_NOACCESS))
        || page.RegionSize<sizeof(CommonInstanceName)
        || reinterpret_cast<uintptr_t>(literal)-reinterpret_cast<uintptr_t>(page.BaseAddress)>page.RegionSize-sizeof(CommonInstanceName)
        || memcmp(literal,CommonInstanceName,sizeof(CommonInstanceName)))return false;
    const auto name=RoomInstanceName(context.pid);
    DWORD before=0,unused=0;
    if(!VirtualProtect(literal,name.size(),PAGE_READWRITE,&before))return false;
    memcpy(literal,name.data(),name.size());
    return VirtualProtect(literal,name.size(),before,&unused)!=FALSE;
}
#endif
}
extern "C" __declspec(dllexport) DWORD WINAPI WePlayInitializeStandalone(void* opaque) {
    if (InterlockedCompareExchange(&entered,1,0)) return 0;
    const auto* context = static_cast<const Context*>(opaque);
    if (!ContextValid(context)) return 0;
    auto& stage = *reinterpret_cast<volatile DWORD*>(reinterpret_cast<uintptr_t>(context)-28);
    stage = 10; // Entry bridge status page, offset 4.
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
    Crossplay::Edition channel_edition{};
    const char* verified_hash=nullptr;
    StandaloneProfile::Edition edition = StandaloneProfile::Edition::History;
#ifdef S7_GOLD_STARTUP
    if (context->edition != 3 || context->entry != 0x7aee5b
        || !StandaloneImage::Matches(bytes.data(),bytes.size(),GoldHash)) return 0;
    entry = context->entry; edition = StandaloneProfile::Edition::Gold;
    channel_edition=Crossplay::Edition::Gold;verified_hash=GoldHash;
#else
    if (context->edition == 1 && context->entry == 0x622fb7
        && StandaloneImage::Matches(bytes.data(),bytes.size(),UbisoftHash)){
        entry = context->entry;channel_edition=Crossplay::Edition::UbisoftHistory;verified_hash=UbisoftHash;
    }
    else if (context->edition == 2 && context->entry == 0x10e3310) {
        StandaloneImage::SteamText text;
        StandaloneImage::Error error{};
        if (!StandaloneImage::DecodeSteam(bytes,text,error) || !StandaloneImage::InstallSteam(context->image,text,error)) return 0;
        entry = text.entry;
        channel_edition=Crossplay::Edition::SteamHistory;
        verified_hash="8349ae6523dfb5c54feaaa61370ec65708d2cde6d1063c1c9cfbe030b2aab410";
    } else return 0;
#endif
    stage = 11;
    const DWORD kernel_probe=ProbeStandaloneKernelIsolation();
    if (!(kernel_probe&1)) return 0;
    *reinterpret_cast<DWORD*>(reinterpret_cast<uintptr_t>(context)-12)=kernel_probe;
    stage = 12;
    if (!StandaloneProfile::Prepare(path,edition,module)) return 0;
    stage = 13;
    Crossplay::Digest executable{};
    if(!verified_hash || !CrossplayProduction::Hex(std::wstring(verified_hash,verified_hash+64),executable)
        || !CrossplayProduction::PrepareLaunch(channel_edition,executable,StandaloneProfile::ProfileDirectory()))return 0;
    stage = 14;
#ifdef S7_GOLD_STARTUP
    if(CrossplayProduction::CurrentLaunch()->mixed && !ConfigureGoldLoopbackGuard(2))return 0;
#endif
    if(!InstallOfflineNetworkGuard())return 0;
    // Fail closed until the normal session runtime is attached. A valid mixed
    // handoff must never silently launch the existing solo/History-only path.
#ifdef S7_CROSSPLAY_ROOM_LOBBY_LAB
    stage = 15;
    if(CrossplayProduction::CurrentLaunch()->mixed) {
        if(!CrossplayProduction::CurrentLaunch()->IsRoom() || kernel_probe!=7)return 0;
#ifndef S7_GOLD_STARTUP
        if(!IsolateHistoryRoomInstance(*context,channel_edition))return 0;
#endif
        // GoldInitialize applies the existing per-copy mutex in ProfilePatches.
    }
#else
    if(CrossplayProduction::CurrentLaunch()->mixed)return 0;
#endif
#ifdef S7_GOLD_STARTUP
    stage = 16;
    if (GoldInitialize(nullptr) != 1) return 0;
#endif
    stage = 17;
    CrossplayNative::ParticipantEvent ui_binding;
    if(!ui_binding.Bind(channel_edition) || !CrossplayProduction::UiConsole::Install(channel_edition,GetCurrentThreadId()))return 0;
#ifdef S7_CROSSPLAY_ROOM_LOBBY_LAB
    stage = 18;
    if(CrossplayProduction::CurrentLaunch()->IsRoom() && !CrossplayProduction::PrepareRuntime())return 0;
#endif
    return context->image+entry;
}
BOOL WINAPI DllMain(HINSTANCE instance,DWORD reason,LPVOID) {
    if (reason != DLL_PROCESS_ATTACH) return TRUE;
    module = instance;
    return StandaloneLaunchGate() ? TRUE : FALSE;
}
