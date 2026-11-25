// Authored CPU-only exception fixture. No game data, injection or GPU code.
// The private smoke controller creates both events before launching this child.
#include <windows.h>
#include <cwchar>

__declspec(noinline) static void TriggerAuthoredAccessViolation() {
    auto address = reinterpret_cast<volatile DWORD*>(static_cast<ULONG_PTR>(0x42));
    *address = 0x53474346;
}

int wmain(int argc, wchar_t** argv) {
    constexpr auto prefix = L"Local\\S7AuthoredCrashFixture-";
    if (argc != 3 || std::wcsncmp(argv[1], prefix, std::wcslen(prefix)) != 0 ||
        std::wcsncmp(argv[2], prefix, std::wcslen(prefix)) != 0) return 2;
    HANDLE ready = OpenEventW(EVENT_MODIFY_STATE, FALSE, argv[1]);
    HANDLE go = OpenEventW(SYNCHRONIZE, FALSE, argv[2]);
    if (!ready || !go) return 3;
    if (!SetEvent(ready)) return 4;
    const DWORD waited = WaitForSingleObject(go, 60000);
    CloseHandle(ready);
    CloseHandle(go);
    if (waited != WAIT_OBJECT_0) return 5;
    TriggerAuthoredAccessViolation();
    return 6;
}
