#include "standalone_gate.h"
#include <cstdio>
#ifdef S7_ENTRY_FIXTURE_DLL
namespace { bool ready = false; }
static bool MilesForwarders() {
    wchar_t requested[2]{};
    if (!GetEnvironmentVariableW(L"WEPLAY_TEST_MILES",requested,2)) return true;
    HMODULE gate = LoadLibraryW(L"mss32.dll");
    HMODULE original = LoadLibraryW(L"weplay_miles_original.dll");
    if (!gate || !original) return false;
    const auto base = reinterpret_cast<uintptr_t>(original);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    const auto* nt = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    const auto* exports = reinterpret_cast<const IMAGE_EXPORT_DIRECTORY*>(base+nt->OptionalHeader.DataDirectory[0].VirtualAddress);
    const auto* names = reinterpret_cast<const DWORD*>(base+exports->AddressOfNames);
    const auto* ordinals = reinterpret_cast<const WORD*>(base+exports->AddressOfNameOrdinals);
    if (exports->NumberOfNames != 355 || exports->NumberOfFunctions != 355) return false;
    for (DWORD i = 0; i < exports->NumberOfNames; ++i) {
        const auto* name = reinterpret_cast<const char*>(base+names[i]);
        auto target = GetProcAddress(original,name);
        if (!target || GetProcAddress(gate,name) != target
            || GetProcAddress(gate,MAKEINTRESOURCEA(exports->Base+ordinals[i])) != target) return false;
    }
    std::puts("PASS all 355 Miles forwarders by actual Windows loader resolution, by name and ordinal");
    return true;
}
extern "C" __declspec(dllexport) DWORD WINAPI WePlayInitializeStandalone(void* opaque) {
    const auto* context = static_cast<const uint32_t*>(opaque);
    if (!StandaloneLaunchGate() || !context || context[0] != 0x37535057 || context[1] != 1
        || context[2] != 32 || context[3] != GetCurrentProcessId() || context[4] != GetCurrentThreadId()
        || context[5] != reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr)) || context[7] != 99) return 0;
    if (!MilesForwarders()) return 0;
    ready = true;
    return context[5]+context[6];
}
extern "C" __declspec(dllexport) int __cdecl FixtureReady() { return ready ? 1 : 0; }
BOOL WINAPI DllMain(HINSTANCE,DWORD reason,LPVOID) { return reason != DLL_PROCESS_ATTACH || StandaloneLaunchGate(); }
#else
void* volatile fixture_imports[] = {reinterpret_cast<void*>(&Sleep),reinterpret_cast<void*>(&GetTickCount),
    reinterpret_cast<void*>(&LoadLibraryW),reinterpret_cast<void*>(&GetProcAddress),reinterpret_cast<void*>(&TerminateProcess)};
int wmain(int argc,wchar_t** argv) {
    if (argc == 2 && !wcscmp(argv[1],L"--direct")) {
        if (LoadLibraryW(L"standalone_entry_fixture.dll") || GetLastError() != ERROR_DLL_INIT_FAILED) return 9;
        std::puts("PASS direct-load rejection");
        return 0;
    }
    HMODULE library = GetModuleHandleW(L"standalone_entry_fixture.dll");
    auto ready = reinterpret_cast<int(__cdecl*)()>(GetProcAddress(library,"FixtureReady"));
    PROCESS_MITIGATION_CHILD_PROCESS_POLICY policy{};
    if (!library || !ready || ready() != 1 || !fixture_imports[0]
        || !GetProcessMitigationPolicy(GetCurrentProcess(),ProcessChildProcessPolicy,&policy,sizeof(policy))
        || !policy.NoChildProcessCreation) return 1;
    wchar_t path[MAX_PATH]{};
    if (!GetModuleFileNameW(nullptr,path,MAX_PATH)) return 2;
    STARTUPINFOW startup{}; startup.cb = sizeof(startup);
    PROCESS_INFORMATION child{};
    if (CreateProcessW(path,nullptr,nullptr,nullptr,FALSE,CREATE_SUSPENDED,nullptr,nullptr,&startup,&child)) {
        // This child is our exact suspended fixture, never a foreign process.
        TerminateProcess(child.hProcess,0xe0537024); CloseHandle(child.hThread); CloseHandle(child.hProcess);
        return 3;
    }
    if (GetLastError() != ERROR_CHILD_PROCESS_BLOCKED) return 4;
    std::puts("PASS actual x86 bridge: initializer before CRT, restored entry, preserved startup, child-process policy enforced");
    return 0;
}
#endif
