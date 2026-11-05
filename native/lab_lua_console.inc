// Laboratory-only observation of real protected-table failures. The release
// game's WriteToConsole callback discards its text; retain its exact result/ABI.
struct LabLuaConsoleRecord { char bytes[2048]; DWORD length; };
#ifdef S7_NEO_FIFO_LAB
LabLuaConsoleRecord lab_lua_console_records[2048]{};
#else
LabLuaConsoleRecord lab_lua_console_records[64]{};
#endif
size_t lab_lua_console_count = 0, lab_lua_console_flushed = 0;
uint64_t lab_lua_console_file_bytes = 0;
#ifdef S7_NEO_FIFO_LAB
bool lab_lua_console_failed = false, lab_lua_console_capture_reported = false;
#else
bool lab_lua_console_failed = false;
#endif
int __cdecl ObserveLabLuaConsole(void* state) {
    const DWORD incoming = GetLastError();
    if (!steam_history && initialization == 2 && local_connect_probe
        && GetCurrentThreadId() == bootstrap_thread.load(std::memory_order_acquire)
        && lab_lua_console_count < _countof(lab_lua_console_records)) {
        using ToString = const char* (__cdecl*)(void*, int, void*);
        const char* value = Native<ToString>(0x45B650)(state, 1, nullptr);
        if (value) {
            size_t length = 0;
            while (length < sizeof(lab_lua_console_records[0].bytes) && value[length]) ++length;
            constexpr char Prefix[] = "Attempt to ";
#ifdef S7_NEO_FIFO_LAB
            if ((length >= sizeof(Prefix) - 1 && memcmp(value, Prefix, sizeof(Prefix) - 1) == 0) || (length >= 5 && memcmp(value, "NEO1|", 5) == 0)) {
#else
            if (length >= sizeof(Prefix) - 1 && memcmp(value, Prefix, sizeof(Prefix) - 1) == 0) {
#endif
                auto& record = lab_lua_console_records[lab_lua_console_count++];
                memcpy(record.bytes, value, length); record.length = static_cast<DWORD>(length);
#ifdef S7_NEO_FIFO_LAB
                if (!lab_lua_console_capture_reported) { lab_lua_console_capture_reported = true; ReportLocalStatus("neo-logistics-console-capture-active"); }
#else
                if (lab_lua_console_count == 1) ReportLocalStatus("lab-lua-protected-table-failure-captured");
#endif
            }
        }
    }
    SetLastError(incoming);
    return Native<int (__cdecl*)(void*)>(0x5D1550)(state);
}
void FlushLabLuaConsole() {
    if (steam_history || lab_lua_console_failed || lab_lua_console_flushed == lab_lua_console_count) return;
#ifdef S7_NEO_FIFO_LAB
    if (GetCurrentThreadId() != bootstrap_thread.load(std::memory_order_acquire)) return;
#endif
    const DWORD incoming = GetLastError();
    HMODULE module = nullptr; wchar_t path[MAX_PATH]{};
    if (GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS | GET_MODULE_HANDLE_EX_FLAG_UNCHANGED_REFCOUNT,
            reinterpret_cast<LPCWSTR>(&ObserveLabLuaConsole), &module)) {
        const DWORD length = GetModuleFileNameW(module, path, MAX_PATH);
        const std::wstring suffix = L"\\Base\\Bin\\uplay_r1_loader.dll";
        if (length && length < MAX_PATH && length > suffix.size()
            && _wcsicmp(path + length - suffix.size(), suffix.c_str()) == 0) {
            const auto output = std::wstring(path, length - suffix.size()) + L"\\LocalData\\lab-lua-console.private.txt";
            HANDLE file = CreateFileW(output.c_str(), FILE_APPEND_DATA | FILE_READ_ATTRIBUTES,
                FILE_SHARE_READ, nullptr, OPEN_ALWAYS, FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
            if (file != INVALID_HANDLE_VALUE) {
                BY_HANDLE_FILE_INFORMATION info{}; LARGE_INTEGER size{};
                bool okay = GetFileInformationByHandle(file, &info) && !(info.dwFileAttributes & FILE_ATTRIBUTE_REPARSE_POINT)
                    && GetFileSizeEx(file, &size) && size.QuadPart >= 0
                    && static_cast<uint64_t>(size.QuadPart) == lab_lua_console_file_bytes;
                while (okay && lab_lua_console_flushed < lab_lua_console_count) {
                    const auto& record = lab_lua_console_records[lab_lua_console_flushed]; DWORD written = 0;
#ifdef S7_NEO_FIFO_LAB
                    if (lab_lua_console_file_bytes + record.length + 2u > 16u * 1024u * 1024u) { okay = false; break; }
#endif
                    okay = WriteFile(file, record.bytes, record.length, &written, nullptr) && written == record.length;
                    if (okay) { lab_lua_console_file_bytes += written; written = 0;
                        okay = WriteFile(file, "\r\n", 2, &written, nullptr) && written == 2;
                        lab_lua_console_file_bytes += written; }
                    if (okay) ++lab_lua_console_flushed;
                }
                CloseHandle(file);
#ifdef S7_NEO_FIFO_LAB
                if (okay && lab_lua_console_flushed == lab_lua_console_count) { lab_lua_console_count = 0; lab_lua_console_flushed = 0; }
#endif
                if (!okay) { lab_lua_console_failed = true; ReportLocalStatus("lab-lua-console-write-failed"); }
            }
        }
    }
    SetLastError(incoming);
}
