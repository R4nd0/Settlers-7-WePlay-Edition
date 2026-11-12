#include "standalone_profile.h"
#include <shlobj.h>
#include <cstring>
#include <string>
#include <vector>

namespace StandaloneProfile {
namespace {
using SpecialFolder = BOOL(WINAPI*)(HWND, LPWSTR, int, BOOL);
using Folder = HRESULT(WINAPI*)(HWND, int, HANDLE, DWORD, LPWSTR);
SpecialFolder original_special = nullptr;
Folder original_folder = nullptr;
wchar_t documents[MAX_PATH]{};
wchar_t profile[MAX_PATH]{};
size_t documents_length = 0;
bool initialized = false;

BOOL WINAPI LocalSpecial(HWND owner, LPWSTR output, int folder, BOOL create) {
    if ((folder & 0xff) != CSIDL_PERSONAL) return original_special(owner, output, folder, create);
    if (!output || !documents_length) { SetLastError(ERROR_INVALID_PARAMETER); return FALSE; }
    memcpy(output, documents, (documents_length+1)*sizeof(wchar_t));
    return TRUE;
}
HRESULT WINAPI LocalFolder(HWND owner, int folder, HANDLE token, DWORD flags, LPWSTR output) {
    if ((folder & 0xff) != CSIDL_PERSONAL) return original_folder(owner, folder, token, flags, output);
    // A foreign token must never receive the current game's private profile.
    if (!output || !documents_length || token || flags != SHGFP_TYPE_CURRENT) return E_INVALIDARG;
    memcpy(output, documents, (documents_length+1)*sizeof(wchar_t));
    return S_OK;
}
bool Directory(const std::wstring& path, bool create) {
    DWORD attributes = GetFileAttributesW(path.c_str());
    if (attributes == INVALID_FILE_ATTRIBUTES && create) {
        if (GetLastError() != ERROR_FILE_NOT_FOUND && GetLastError() != ERROR_PATH_NOT_FOUND) return false;
        if (!CreateDirectoryW(path.c_str(), nullptr) && GetLastError() != ERROR_ALREADY_EXISTS) return false;
        attributes = GetFileAttributesW(path.c_str());
    }
    return attributes != INVALID_FILE_ATTRIBUTES && (attributes & FILE_ATTRIBUTE_DIRECTORY)
        && !(attributes & FILE_ATTRIBUTE_REPARSE_POINT);
}
struct Slot { void** address; void* original; void* replacement; };
bool Slots(HMODULE module, std::vector<Slot>& slots, bool& main_found) {
    if (!module) return true;
    const uintptr_t base = reinterpret_cast<uintptr_t>(module);
    const auto* dos = reinterpret_cast<const IMAGE_DOS_HEADER*>(base);
    if (dos->e_magic != IMAGE_DOS_SIGNATURE || dos->e_lfanew < 64 || dos->e_lfanew > 4096) return false;
    const auto* pe = reinterpret_cast<const IMAGE_NT_HEADERS32*>(base+dos->e_lfanew);
    if (pe->Signature != IMAGE_NT_SIGNATURE || pe->FileHeader.Machine != IMAGE_FILE_MACHINE_I386
        || pe->OptionalHeader.Magic != IMAGE_NT_OPTIONAL_HDR32_MAGIC) return false;
    const size_t size = pe->OptionalHeader.SizeOfImage;
    const auto range = [=](size_t offset, size_t length) { return offset <= size && length <= size-offset; };
    const auto directory = pe->OptionalHeader.DataDirectory[IMAGE_DIRECTORY_ENTRY_IMPORT];
    if (!directory.VirtualAddress || !range(directory.VirtualAddress, directory.Size)) return false;
    bool terminated = false;
    for (size_t offset = 0; offset+sizeof(IMAGE_IMPORT_DESCRIPTOR) <= directory.Size; offset += sizeof(IMAGE_IMPORT_DESCRIPTOR)) {
        const auto* descriptor = reinterpret_cast<const IMAGE_IMPORT_DESCRIPTOR*>(base+directory.VirtualAddress+offset);
        if (!descriptor->Name) { terminated = true; break; }
        if (!range(descriptor->Name, 1) || !memchr(reinterpret_cast<const void*>(base+descriptor->Name), 0, size-descriptor->Name)) return false;
        if (_stricmp(reinterpret_cast<const char*>(base+descriptor->Name), "shell32.dll")) continue;
        bool thunks_terminated = false;
        for (size_t index = 0; index < 512; ++index) {
            const size_t rva = descriptor->FirstThunk+index*sizeof(IMAGE_THUNK_DATA32);
            if (!range(rva, sizeof(IMAGE_THUNK_DATA32))) return false;
            const auto* thunk = reinterpret_cast<const IMAGE_THUNK_DATA32*>(base+rva);
            if (!thunk->u1.Function) { thunks_terminated = true; break; }
            auto* pointer = reinterpret_cast<void*>(thunk->u1.Function);
            void* replacement = nullptr;
            if (pointer == reinterpret_cast<void*>(original_special)) replacement = reinterpret_cast<void*>(&LocalSpecial);
            if (pointer == reinterpret_cast<void*>(original_folder)) replacement = reinterpret_cast<void*>(&LocalFolder);
            if (!replacement) continue;
            if (module == GetModuleHandleW(nullptr)) main_found = true;
            auto** address = reinterpret_cast<void**>(base+rva);
            bool duplicate = false;
            for (const auto& slot : slots) if (slot.address == address) duplicate = true;
            if (!duplicate) slots.push_back({address,pointer,replacement});
        }
        if (!thunks_terminated) return false;
    }
    return terminated;
}
bool Put(const Slot& slot, bool restore) {
    void* expected = restore ? slot.replacement : slot.original;
    void* replacement = restore ? slot.original : slot.replacement;
    DWORD protection = 0, ignored = 0;
    if (!VirtualProtect(slot.address, sizeof(void*), PAGE_READWRITE, &protection)) return false;
    const bool exchanged = InterlockedCompareExchangePointer(slot.address,replacement,expected) == expected;
    if (!VirtualProtect(slot.address,sizeof(void*),protection,&ignored)) {
        TerminateProcess(GetCurrentProcess(),0xe0537022);
        return false;
    }
    return exchanged;
}
}

bool Prepare(const wchar_t* executable, Edition edition, HMODULE initializer) {
    if (initialized || !executable || !initializer) return false;
    const std::wstring path(executable);
    const std::wstring suffix = edition == Edition::History ? L"\\Base\\Bin\\Settlers7R.exe" : L"\\Data\\Base\\_Dbg\\Bin\\Release\\Settlers7R.exe";
    if (path.size() <= suffix.size() || _wcsicmp(path.c_str()+path.size()-suffix.size(),suffix.c_str())) return false;
    const std::wstring root = path.substr(0,path.size()-suffix.size());
    const std::wstring local = root+L"\\LocalData";
    const std::wstring base = local+L"\\ProfileRoot";
    const std::wstring actual = base+(edition == Edition::History ? L"\\THE SETTLERS 7 - History Edition" : L"\\Profiles");
    if (actual.size()+64 >= MAX_PATH || !Directory(root,false) || !Directory(local,true)
        || !Directory(base,true) || !Directory(actual,true)) return false;
    HMODULE shell = LoadLibraryExW(L"shell32.dll",nullptr,LOAD_LIBRARY_SEARCH_SYSTEM32);
    if (!shell) return false;
    original_special = reinterpret_cast<SpecialFolder>(GetProcAddress(shell,"SHGetSpecialFolderPathW"));
    original_folder = reinterpret_cast<Folder>(GetProcAddress(shell,"SHGetFolderPathW"));
    if (!original_special || !original_folder) { FreeLibrary(shell); return false; }
    // Shell32 is pinned by the game's imports; keep this reference for hook life.
    std::vector<Slot> slots;
    bool main_found = false;
    if (!Slots(GetModuleHandleW(nullptr),slots,main_found)
        || !Slots(GetModuleHandleW(L"vBase90.dll"),slots,main_found)
        || !Slots(GetModuleHandleW(L"Vision90.dll"),slots,main_found)
        || !Slots(initializer,slots,main_found) || !main_found || slots.empty() || slots.size() > 32) {
        FreeLibrary(shell); return false;
    }
    memcpy(documents,base.c_str(),(base.size()+1)*sizeof(wchar_t));
    memcpy(profile,actual.c_str(),(actual.size()+1)*sizeof(wchar_t));
    documents_length = base.size();
    for (size_t i = 0; i < slots.size(); ++i) if (!Put(slots[i],false)) {
        for (size_t j = 0; j < i; ++j) if (!Put(slots[j],true)) TerminateProcess(GetCurrentProcess(),0xe0537022);
        documents_length = 0;
        FreeLibrary(shell);
        return false;
    }
    initialized = true;
    return true;
}
const wchar_t* DocumentsBase() { return initialized ? documents : nullptr; }
const wchar_t* ProfileDirectory() { return initialized ? profile : nullptr; }
}
