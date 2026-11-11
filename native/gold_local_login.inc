// Opt-in, one native SDK login per process. The SDK owns its job/backend;
// our CallContext/output slot remain alive until normal process shutdown.
// This does not set CNetwork account, lobby or simulation admission flags.
struct GoldQString { const wchar_t* data; };
struct GoldOwnedQString {
    GoldQString value{};
    explicit GoldOwnedQString(const char* text) {
        reinterpret_cast<void* (__thiscall*)(void*, const char*)>(image + 0x794120)(&value, text);
    }
    ~GoldOwnedQString() {
        reinterpret_cast<void (__thiscall*)(void*)>(image + 0x794180)(&value);
    }
};
HHOOK local_login_hook = nullptr;
unsigned int local_login_port = 0;
ULONGLONG local_login_start = 0;
bool local_login_key_set = false, local_login_dispatched = false;
char local_login_name[65]{}, local_login_secret[65]{};
void* local_login_context = nullptr;
void* local_login_backend = nullptr;
LONG local_login_busy = 0;
std::atomic<DWORD> local_login_state{0}, local_login_result{0};

void LocalLoginReport(const char* phase) {
    wchar_t path[32768]{};
    const DWORD length = GetEnvironmentVariableW(L"WEPLAY_GOLD_LOCAL_STATUS", path, 32768);
    if (!length) return;
    if (length >= 32768) Stop();
    HANDLE file = CreateFileW(path, FILE_APPEND_DATA, FILE_SHARE_READ, nullptr, OPEN_ALWAYS,
        FILE_ATTRIBUTE_NORMAL | FILE_FLAG_OPEN_REPARSE_POINT, nullptr);
    if (file == INVALID_HANDLE_VALUE) Stop();
    BY_HANDLE_FILE_INFORMATION info{};
    if (!GetFileInformationByHandle(file, &info)
        || (info.dwFileAttributes & (FILE_ATTRIBUTE_REPARSE_POINT | FILE_ATTRIBUTE_DIRECTORY))) Stop();
    char line[256]{};
    const int length_written = sprintf_s(line,
        "{\"phase\":\"%s\",\"context_state\":%lu,\"result\":%lu,\"backend_present\":%s}\n",
        phase, local_login_state.load(), local_login_result.load(), local_login_backend ? "true" : "false");
    DWORD written = 0;
    if (length_written <= 0 || !WriteFile(file, line, static_cast<DWORD>(length_written), &written, nullptr)
        || written != static_cast<DWORD>(length_written)) Stop();
    CloseHandle(file);
}

void __cdecl LocalLoginAccessKey(const GoldQString*) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != image + 0x655d00) Stop();
    CheckOrbit();
    // Gold QString contains one UTF-16 pointer, unlike History's strings.
    const GoldQString key{L"WePlayGoldLoopback"};
    reinterpret_cast<void (__cdecl*)(const GoldQString*)>(image + 0x82bb80)(&key);
    local_login_key_set = true;
}

uintptr_t LocalLoginRendezVous(bool before_login = true) {
    const uintptr_t main = *reinterpret_cast<uintptr_t*>(image + 0xcf83dc);
    if (!main || !ValidRange(reinterpret_cast<void*>(main), 0x48)
        || *reinterpret_cast<uintptr_t*>(main) != image + 0xb55600) return 0;
    const uintptr_t services = *reinterpret_cast<uintptr_t*>(main + 0x10);
    if (!ValidRange(reinterpret_cast<void*>(services), 0x10)) return 0;
    const uintptr_t head = *reinterpret_cast<uintptr_t*>(services + 8);
    if (!ValidRange(reinterpret_cast<void*>(head), 12)) Stop();
    uintptr_t node = *reinterpret_cast<uintptr_t*>(head), rendezvous = 0;
    for (unsigned int count = 0; node != head; ++count) {
        if (count == 128 || !ValidRange(reinterpret_cast<void*>(node), 12)) Stop();
        const uintptr_t service = *reinterpret_cast<uintptr_t*>(node + 8);
        if (!ValidRange(reinterpret_cast<void*>(service), 0x24)) Stop();
        if (*reinterpret_cast<uintptr_t*>(service) == image + 0xb56bbc) {
            if (rendezvous) Stop();
            rendezvous = *reinterpret_cast<uintptr_t*>(service + 8);
        }
        node = *reinterpret_cast<uintptr_t*>(node);
    }
    if (!rendezvous || !ValidRange(reinterpret_cast<void*>(rendezvous), 0xc0, true)
        || *reinterpret_cast<uintptr_t*>(rendezvous) != image + 0xaec274
        || (before_login && *reinterpret_cast<uintptr_t*>(rendezvous + 0xb0))) return 0;
    return rendezvous;
}

#ifdef S7_GOLD_ACCOUNT_LOGIN
#include "gold_account_login.inc"
#else
void LocalLoginPoll() {
    if (!local_login_key_set || !owner_thread.load()
        || GetCurrentThreadId() != owner_thread.load()) return;
    CheckOrbit();
    if (local_login_dispatched) {
        if (!ValidRange(local_login_context, 0x78)
            || *static_cast<uintptr_t*>(local_login_context) != image + 0xae96a0) Stop();
        const auto* context = static_cast<unsigned char*>(local_login_context);
        const DWORD state = *reinterpret_cast<const DWORD*>(context + 0xc);
        if (state > 3) Stop();
        const DWORD previous = local_login_state.exchange(state);
        if (previous != state) {
            local_login_result.store(*reinterpret_cast<const DWORD*>(context + 0x34));
            if (state == 2 && (!local_login_backend || local_login_result.load() != 0x10001)) Stop();
            LocalLoginReport("native-context");
        }
        return;
    }
    if (GetTickCount64() - local_login_start < 10000) return;
    const uintptr_t rendezvous = LocalLoginRendezVous();
    if (!rendezvous) return;
    local_login_dispatched = true;
    using Alloc = void* (__cdecl*)(unsigned int, const char*, unsigned int);
    local_login_context = reinterpret_cast<Alloc>(image + 0x791ff0)(0x78, "weplay_gold_local_login", 1);
    if (!ValidRange(local_login_context, 0x78, true)) Stop();
    reinterpret_cast<void* (__thiscall*)(void*)>(image + 0x82b780)(local_login_context);
    GoldOwnedQString host("127.0.0.1"), sandbox("WePlayGoldLoopback"), extra("");
    GoldOwnedQString name(local_login_name), password(local_login_secret);
    using Login = bool (__thiscall*)(void*, void*, const GoldQString*, unsigned int,
        const GoldQString*, const GoldQString*, const GoldQString*, const GoldQString*, void**);
    // The original async job copies these inputs, pumps callbacks and assigns
    // our backend slot. We never complete a CallContext ourselves.
    const bool queued = reinterpret_cast<Login>(image + 0x847e50)(reinterpret_cast<void*>(rendezvous),
        local_login_context, &host.value, local_login_port, &sandbox.value, &extra.value,
        &name.value, &password.value, &local_login_backend);
    SecureZeroMemory(local_login_secret, sizeof(local_login_secret));
    LocalLoginReport(queued ? "native-login-queued" : "native-login-rejected");
}

#endif

LRESULT CALLBACK LocalLoginMessage(int code, WPARAM wparam, LPARAM lparam) {
    if (code >= 0 && !InterlockedExchange(&local_login_busy, 1)) {
        LocalLoginPoll();
        InterlockedExchange(&local_login_busy, 0);
    }
    return CallNextHookEx(local_login_hook, code, wparam, lparam);
}

bool LocalLoginPrepare(std::vector<Patch>& patches) {
    // Optional isolated loopback slot. Never accepts a LAN/external address.
    char slot[3]{};
    const DWORD slot_length = GetEnvironmentVariableA("WEPLAY_GOLD_LOOPBACK_SLOT", slot, 3);
    if (slot_length && (slot_length != 1 || slot[0] < '2' || slot[0] > '9'
        || !ConfigureGoldLoopbackGuard(static_cast<unsigned int>(slot[0] - '0')))) return false;
#if defined(S7_GOLD_ACCOUNT_LOGIN) && defined(S7_GOLD_LOCAL_SIMULATION)
    char multiplayer[3]{};
    const DWORD multiplayer_length = GetEnvironmentVariableA("WEPLAY_GOLD_LOCAL_MULTIPLAYER", multiplayer, 3);
    if (multiplayer_length) {
        if (multiplayer_length != 1 || multiplayer[0] != '1' || slot_length != 1) return false;
        local_multiplayer_enabled = true;
    }
    char peer_leave[3]{};
    const DWORD peer_leave_length = GetEnvironmentVariableA("WEPLAY_GOLD_LOCAL_PEER_LEAVE", peer_leave, 3);
    if (peer_leave_length) {
        if (peer_leave_length != 1 || peer_leave[0] != '1' || !local_multiplayer_enabled) return false;
        local_peer_leave_enabled = true;
    }
#endif
    const struct { uintptr_t rva; std::vector<unsigned char> bytes; } guards[] = {
        {0x791ff0, {0x8b,0x44,0x24,0x0c,0x8b,0x4c,0x24,0x08}},
        {0x82b780, {0x56,0x8b,0xf1,0xe8,0x98,0x2a,0xf8,0xff}},
        {0x794120, {0x8b,0x44,0x24,0x04,0x56,0x8b,0xf1,0x50}},
        {0x794180, {0x8b,0x01,0x85,0xc0,0x74,0x14}},
        {0x847e50, {0x53,0x55,0x56,0x57,0x8b,0xf1,0xe8,0xf5,0xea,0xf4,0xff}}
    };
    for (const auto& guard : guards)
        if (memcmp(reinterpret_cast<void*>(image + guard.rva), guard.bytes.data(), guard.bytes.size())) return false;
    char port[8]{};
    DWORD length = GetEnvironmentVariableA("WEPLAY_GOLD_LOCAL_PORT", port, 8);
    if (!length || length >= 8) return false;
    for (DWORD i = 0; i < length; ++i) {
        if (port[i] < '0' || port[i] > '9') return false;
        local_login_port = local_login_port * 10 + static_cast<unsigned int>(port[i] - '0');
    }
    if (local_login_port < 1024 || local_login_port > 65535) return false;
    length = GetEnvironmentVariableA("WEPLAY_GOLD_LOCAL_SECRET", local_login_secret, 65);
    if (length != 64) return false;
    for (size_t i = 0; i < 64; ++i)
        if (!((local_login_secret[i] >= '0' && local_login_secret[i] <= '9')
            || (local_login_secret[i] >= 'a' && local_login_secret[i] <= 'f'))) return false;
    length = GetEnvironmentVariableA("WEPLAY_GOLD_LOCAL_NAME", local_login_name, 65);
    if (!length || length >= 65) return false;
    for (DWORD i = 0; i < length; ++i)
        if (local_login_name[i] < 32 || local_login_name[i] > 126) return false;
    if (!SetEnvironmentVariableA("WEPLAY_GOLD_LOCAL_SECRET", nullptr)) return false;
#ifdef S7_GOLD_ACCOUNT_LOGIN
    if (!AccountLoginPrepare(patches)) return false;
#endif
    return Add(patches, 0x655cfb, {0xe8,0x80,0x5e,0x1d,0x00}, LocalLoginAccessKey);
}

bool LocalLoginInstall() {
    local_login_start = GetTickCount64();
    local_login_hook = SetWindowsHookExW(WH_GETMESSAGE, LocalLoginMessage, nullptr, GetCurrentThreadId());
    return local_login_hook != nullptr;
}
