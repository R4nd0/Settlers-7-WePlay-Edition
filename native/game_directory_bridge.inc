// Fixed local data bridge for the session browser. No sockets, script execution,
// arbitrary paths, game-session acknowledgements or room secrets enter Lua.
bool DirectoryHex(const char* value, size_t length) {
    if (!value) return false;
    for (size_t i = 0; i < length; ++i)
        if (!((value[i] >= '0' && value[i] <= '9') || (value[i] >= 'a' && value[i] <= 'f'))) return false;
    return true;
}
std::wstring DirectoryDataPath(const wchar_t* name) {
    HMODULE module = nullptr; wchar_t path[MAX_PATH]{};
    if (!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&DirectoryDataPath), &module)) return {};
    const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
    const std::wstring suffix = L"\\Base\\Bin\\uplay_r1_loader.dll";
    if (!length || length >= MAX_PATH || length <= suffix.size()
            || _wcsicmp(path + length - suffix.size(), suffix.c_str()) != 0) return {};
    const auto parent = std::wstring(path, length - suffix.size()) + L"\\LocalData";
    const DWORD flags = GetFileAttributesW(parent.c_str());
    if (flags == INVALID_FILE_ATTRIBUTES || !(flags & FILE_ATTRIBUTE_DIRECTORY) || (flags & FILE_ATTRIBUTE_REPARSE_POINT)) return {};
    return parent + L"\\" + name;
}
bool ReadDirectorySnapshot(std::string& value, const wchar_t* name = L"global-games-v1.txt") {
    const auto path = DirectoryDataPath(name); if (path.empty()) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_READ, FILE_SHARE_READ | FILE_SHARE_WRITE | FILE_SHARE_DELETE,
        nullptr, OPEN_EXISTING, FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{}; DWORD count = 0;
    bool okay = GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))
        && GetFileSizeEx(file, &size) && size.QuadPart > 0 && size.QuadPart <= 32768;
    if (okay) {
        value.resize(static_cast<size_t>(size.QuadPart));
        okay = ReadFile(file, value.data(), static_cast<DWORD>(value.size()), &count, nullptr) && count == value.size()
            && value.find('\0') == std::string::npos;
    }
    CloseHandle(file); if (!okay) value.clear(); return okay;
}
bool RequestDirectoryJoin(const char* command, size_t length) {
    constexpr char prefix[] = "weplay-directory-join-v1:";
    constexpr size_t start = sizeof(prefix) - 1;
    if (length != start + 65 || memcmp(command, prefix, start) != 0 || command[start + 32] != ':'
        || !DirectoryHex(command + start, 32) || !DirectoryHex(command + start + 33, 32)) return false;
    std::string snapshot;
    constexpr char header[] = "weplay-games-ui-v1|";
    constexpr size_t offset = sizeof(header) - 1;
    if (!ReadDirectorySnapshot(snapshot) || snapshot.size() < offset + 34
        || snapshot.compare(0, offset, header) != 0
        || snapshot.compare(offset, 32, command + start, 32) != 0) return false;
    const auto path = DirectoryDataPath(L"global-join-v1.txt"); if (path.empty()) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD count = 0; const bool okay = WriteFile(file, command + start, 65, &count, nullptr) && count == 65;
    CloseHandle(file); return okay;
}
bool InvitationSnapshotMatches(const std::string& snapshot, const char* nonce) {
    constexpr char header[] = "weplay-invitation-ui-v1|";
    constexpr size_t offset = sizeof(header) - 1;
    if (!nonce || !DirectoryHex(nonce,32) || snapshot.size() > 96
        || snapshot.size() < offset + 34 || snapshot.compare(0, offset, header) != 0
        || snapshot.compare(offset, 32, nonce, 32) != 0) return false;
    const auto mode = snapshot.substr(offset + 32);
    return mode == "|private\n" || mode == "|public\n";
}
bool RequestInvitation(const char* command, size_t length) {
    constexpr char prefix[] = "weplay-invitation-open-v1:";
    constexpr size_t start = sizeof(prefix) - 1;
    if (!command || length != start + 32 || memcmp(command, prefix, start) != 0
        || !DirectoryHex(command + start, 32)) return false;
    std::string snapshot;
    if (!ReadDirectorySnapshot(snapshot, L"invitation-ui-v1.txt") || !InvitationSnapshotMatches(snapshot,command+start)) return false;
    const auto path = DirectoryDataPath(L"invitation-open-v1.txt"); if (path.empty()) return false;
    HANDLE file = CreateFileW(path.c_str(), GENERIC_WRITE, 0, nullptr, CREATE_NEW,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) return false;
    DWORD count = 0; const bool okay = WriteFile(file, command + start, 32, &count, nullptr) && count == 32;
    CloseHandle(file); return okay;
}
int __cdecl DirectoryConsole(void* state) {
#ifdef S7_NEO_LOGISTICS
    if(const auto handled=TryNeoLogisticsConsole(state))return handled;
#endif
    const DWORD incoming = GetLastError();
    if (state && initialization == 2 && local_connect_probe
        && GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)) {
        size_t length = 0;
        const char* command = Native<const char* (__cdecl*)(void*, int, size_t*)>(0x45B650)(state, 1, &length);
        constexpr char inviteRead[] = "weplay-invitation-read-v1";
        if (command && length == sizeof(inviteRead) - 1 && memcmp(command, inviteRead, sizeof(inviteRead) - 1) == 0) {
            std::string snapshot;
            if (ReadDirectorySnapshot(snapshot, L"invitation-ui-v1.txt") && snapshot.size() <= 96)
                Native<void (__cdecl*)(void*, const char*, size_t)>(0x45B790)(state, snapshot.data(), snapshot.size());
            else Native<void (__cdecl*)(void*, int)>(0x44C6E0)(state, 0);
            SetLastError(incoming); return 1;
        }
        constexpr char invitePrefix[] = "weplay-invitation-open-v1:";
        if (command && length >= sizeof(invitePrefix) - 1 && length <= 128
            && memcmp(command, invitePrefix, sizeof(invitePrefix) - 1) == 0) {
            Native<void (__cdecl*)(void*, int)>(0x44C6E0)(state, RequestInvitation(command, length) ? 1 : 0);
            SetLastError(incoming); return 1;
        }
        constexpr char read[] = "weplay-directory-read-v1";
        if (command && length == sizeof(read) - 1 && memcmp(command, read, sizeof(read) - 1) == 0) {
            std::string snapshot;
            if (ReadDirectorySnapshot(snapshot))
                Native<void (__cdecl*)(void*, const char*, size_t)>(0x45B790)(state, snapshot.data(), snapshot.size());
            else Native<void (__cdecl*)(void*, int)>(0x44C6E0)(state, 0);
            SetLastError(incoming); return 1;
        }
        constexpr char prefix[] = "weplay-directory-join-v1:";
        if (command && length >= sizeof(prefix) - 1 && length <= 128 && memcmp(command, prefix, sizeof(prefix) - 1) == 0) {
            const bool okay = RequestDirectoryJoin(command, length);
            Native<void (__cdecl*)(void*, int)>(0x44C6E0)(state, okay ? 1 : 0);
            SetLastError(incoming); return 1;
        }
    }
    SetLastError(incoming);
#ifdef S7_LOCAL_PAIR_LAB
    if (!steam_history) return ObserveLabLuaConsole(state);
#endif
    return Native<int (__cdecl*)(void*)>(0x5D1550)(state);
}
