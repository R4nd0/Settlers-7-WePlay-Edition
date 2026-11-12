#include "standalone_profile.h"
#include <shlobj.h>
#include <cstdio>
#include <string>

int wmain() {
    wchar_t current[MAX_PATH]{};
    if (!GetCurrentDirectoryW(MAX_PATH,current)) return 1;
    const std::wstring root = std::wstring(current)+L"\\profile-test";
    if (!CreateDirectoryW(root.c_str(),nullptr) && GetLastError()!=ERROR_ALREADY_EXISTS) return 2;
    const std::wstring fake = root+L"\\Base\\Bin\\Settlers7R.exe";
    if (StandaloneProfile::Prepare(L"C:\\unowned\\unknown.exe",StandaloneProfile::Edition::History,GetModuleHandleW(nullptr))) return 3;
    if (!StandaloneProfile::Prepare(fake.c_str(),StandaloneProfile::Edition::History,GetModuleHandleW(nullptr))) return 4;
    wchar_t a[MAX_PATH]{},b[MAX_PATH]{};
    if (!SHGetSpecialFolderPathW(nullptr,a,CSIDL_PERSONAL,TRUE)
        || SHGetFolderPathW(nullptr,CSIDL_PERSONAL,nullptr,SHGFP_TYPE_CURRENT,b)!=S_OK
        || wcscmp(a,StandaloneProfile::DocumentsBase()) || wcscmp(a,b)) return 5;
    if (!SHGetSpecialFolderPathW(nullptr,b,CSIDL_WINDOWS,FALSE) || !wcscmp(a,b)) return 6;
    if (SHGetFolderPathW(nullptr,CSIDL_PERSONAL,reinterpret_cast<HANDLE>(1),SHGFP_TYPE_CURRENT,b)!=E_INVALIDARG) return 7;
    if (StandaloneProfile::Prepare(fake.c_str(),StandaloneProfile::Edition::History,GetModuleHandleW(nullptr))) return 8;
    std::puts("PASS standalone profile: path containment, real IAT calls, personal-only routing, foreign-token and repeat rejection");
    return 0;
}
