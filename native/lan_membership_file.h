#pragma once
#include "lan_membership.h"
#include <windows.h>

namespace lan_membership {
struct File {
    HANDLE handle = INVALID_HANDLE_VALUE;
    explicit File(HANDLE value) : handle(value) {}
    ~File() { if (handle != INVALID_HANDLE_VALUE) CloseHandle(handle); }
    File(const File&) = delete;
    File& operator=(const File&) = delete;
};
inline bool ExactOpenedPath(HANDLE handle, const std::wstring& expected) {
    wchar_t resolved[MAX_PATH + 8]{};
    DWORD length = GetFinalPathNameByHandleW(handle, resolved, MAX_PATH + 8, FILE_NAME_NORMALIZED | VOLUME_NAME_DOS);
    return length >= 4 && length < MAX_PATH + 8 && wcsncmp(resolved, L"\\\\?\\", 4) == 0
        && _wcsicmp(resolved + 4, expected.c_str()) == 0;
}
inline bool ValidRoot(const wchar_t* root) {
    if (!root) return false;
    const size_t length = wcsnlen_s(root, MAX_PATH);
    constexpr wchar_t suffix[] = L"\\LocalData";
    if (length <= 3 + wcslen(suffix) || length >= MAX_PATH - 20 || root[1] != L':' || root[2] != L'\\'
        || !((root[0] >= L'A' && root[0] <= L'Z') || (root[0] >= L'a' && root[0] <= L'z'))
        || _wcsicmp(root + length - wcslen(suffix), suffix) != 0) return false;
    for (size_t i = 2; i < length; ++i) if (root[i] == L':' || root[i] == L'/' || root[i] == L'*' || root[i] == L'?') return false;
    wchar_t canonical[MAX_PATH]{};
    DWORD size = GetFullPathNameW(root, MAX_PATH, canonical, nullptr);
    if (!size || size >= MAX_PATH || _wcsicmp(root, canonical) != 0) return false;
    File directory(CreateFileW(root, FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION info{};
    return directory.handle != INVALID_HANDLE_VALUE && GetFileInformationByHandle(directory.handle, &info)
        && (info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        && ExactOpenedPath(directory.handle, root);
}
inline bool Read(const std::wstring& root, uint32_t local, uint32_t coordinator, uint64_t now, Snapshot& output,
                 void (*before_read)() = nullptr) {
    output = {};
    if (!ValidRoot(root.c_str())) return false;
    File directory(CreateFileW(root.c_str(), FILE_READ_ATTRIBUTES, FILE_SHARE_READ | FILE_SHARE_WRITE, nullptr, OPEN_EXISTING,
                              FILE_FLAG_BACKUP_SEMANTICS | FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION directory_info{};
    if (directory.handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(directory.handle, &directory_info)
        || !(directory_info.dwFileAttributes & FILE_ATTRIBUTE_DIRECTORY) || (directory_info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
        || !ExactOpenedPath(directory.handle, root)) return false;
    const std::wstring path = root + L"\\lan-peers.ini";
    // An opened old/new file stays stable during an atomic launcher replacement.
    File file(CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE, nullptr,
                         OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr));
    BY_HANDLE_FILE_INFORMATION before{}, after{};
    if (file.handle == INVALID_HANDLE_VALUE || !GetFileInformationByHandle(file.handle, &before)
        || before.nFileSizeHigh || !before.nFileSizeLow || before.nFileSizeLow > MaxFileSize || before.nNumberOfLinks != 1
        || (before.dwFileAttributes & (FILE_ATTRIBUTE_DIRECTORY | FILE_ATTRIBUTE_REPARSE_POINT))
        || !ExactOpenedPath(file.handle, path)) return false;
    char bytes[MaxFileSize]{}; DWORD received = 0, extra = 0; char extra_byte = 0;
    if (before_read) before_read(); // Only synthetic test builds supply a hook.
    if (!ReadFile(file.handle, bytes, before.nFileSizeLow, &received, nullptr) || received != before.nFileSizeLow
        || !ReadFile(file.handle, &extra_byte, 1, &extra, nullptr) || extra
        || !GetFileInformationByHandle(file.handle, &after) || before.nFileSizeHigh != after.nFileSizeHigh
        || before.nFileSizeLow != after.nFileSizeLow || CompareFileTime(&before.ftLastWriteTime, &after.ftLastWriteTime)
        || !ExactOpenedPath(directory.handle, root)) return false;
    return Parse(bytes, received, local, coordinator, now, output);
}
} // namespace lan_membership
