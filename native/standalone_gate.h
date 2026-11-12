#pragma once
#define WIN32_LEAN_AND_MEAN
#include <windows.h>
#include <stdint.h>
#include <cstring>

// A release adapter accepts only the launcher's suspended, child-restricted
// process. This gate does no file IO, loading or patching under the loader lock.
inline bool StandaloneLaunchGate() {
    PROCESS_MITIGATION_CHILD_PROCESS_POLICY policy{};
    if (!GetProcessMitigationPolicy(GetCurrentProcess(),ProcessChildProcessPolicy,&policy,sizeof(policy))
        || !policy.NoChildProcessCreation || policy.AuditNoChildProcessCreation) return false;
    const uintptr_t image = reinterpret_cast<uintptr_t>(GetModuleHandleW(nullptr));
    if (!image) return false;
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(image);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 || dos->e_lfanew > 4096) return false;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(image+dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC
        || pe->OptionalHeader.AddressOfEntryPoint > pe->OptionalHeader.SizeOfImage-5) return false;
    const auto* entry = reinterpret_cast<const unsigned char*>(image+pe->OptionalHeader.AddressOfEntryPoint);
    if (entry[0] != 0xe9) return false;
    int32_t displacement = 0; memcpy(&displacement,entry+1,4);
    const uintptr_t stub = reinterpret_cast<uintptr_t>(entry)+5+displacement;
    MEMORY_BASIC_INFORMATION info{};
    if (VirtualQuery(reinterpret_cast<void*>(stub),&info,sizeof(info)) != sizeof(info)
        || reinterpret_cast<uintptr_t>(info.AllocationBase) != stub || info.Type != MEM_PRIVATE
        || info.State != MEM_COMMIT || info.Protect != PAGE_EXECUTE_READ || info.RegionSize < 4096) return false;
    const auto* bytes = reinterpret_cast<const unsigned char*>(stub);
    return bytes[0] == 0x9c && bytes[1] == 0x60 && !memcmp(bytes+480,"WPS7ENTRY1",10);
}
