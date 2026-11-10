// Experimental, one automatic Gold account login against our own loopback
// services. Original account Update, HTTP parsing, profile methods and service
// activation own all results. No account/connection success fields are patched.
uintptr_t account_login_owner = 0;
unsigned int account_config_port = 0;
bool account_login_observed = false;
__declspec(thread) bool account_http_owned = false;
__declspec(thread) bool account_headers_owned = false;
__declspec(thread) void* account_http_request = nullptr;

bool AccountUriEquals(const unsigned char* uri, unsigned int offset, const wchar_t* expected) {
    const auto* value = *reinterpret_cast<const wchar_t* const*>(uri + offset);
    const size_t count = wcslen(expected);
    return ValidRange(value, (count + 1) * sizeof(wchar_t))
        && wcsncmp(value, expected, count + 1) == 0;
}

void* __fastcall AccountHttpFactory(void* engine, void*, unsigned int method, const unsigned char* uri) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != image + 0x82a57f
        || !ValidRange(uri, 0x24) || account_http_owned) Stop();
    const bool owned = method == 0 && AccountUriEquals(uri, 0, L"http")
        && AccountUriEquals(uri, 0xc, L"127.0.0.1")
        && *reinterpret_cast<const unsigned int*>(uri + 0x10) == account_config_port;
    account_http_owned = owned;
    void* request = reinterpret_cast<void* (__thiscall*)(void*, unsigned int, const void*)>(
        image + 0x85f6b0)(engine, method, uri);
    account_http_owned = false;
    if (owned) {
        if (account_http_request || !ValidRange(request, 0x28, true)
            || *static_cast<uintptr_t*>(request) != image + 0xaee7e8) Stop();
        account_http_request = request;
    }
    return request;
}

bool __fastcall AccountHttpHeaders(void* request, void*, const void* headers) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != image + 0x82a5af || account_headers_owned) Stop();
    const bool owned = account_http_request && request == account_http_request;
    if (owned) account_http_request = nullptr;
    account_headers_owned = owned;
    const bool result = reinterpret_cast<bool (__thiscall*)(void*, const void*)>(
        image + 0x860c50)(request, headers);
    account_headers_owned = false;
    return result;
}

void* __cdecl AccountHeaderAppend(void* list, const char* header) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != image + 0x860c84) Stop();
    char host[48]{};
    if (account_headers_owned && ValidRange(header, 5) && !_strnicmp(header, "Host:", 5)) {
        sprintf_s(host, "Host: 127.0.0.1:%u", account_config_port);
        header = host;
    }
    // curl_slist_append owns its copy; the native request keeps/frees the list.
    return reinterpret_cast<void* (__cdecl*)(void*, const char*)>(image + 0x8cabc0)(list, header);
}

int __cdecl AccountHttpMethod(void* easy, int option, int value) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != image + 0x860f61) Stop();
    // The fingerprinted prepared EXE bypasses the original method selector,
    // leaving option zero. Restore the genuine HTTPGET option only inside
    // the verified own configuration factory; all other calls stay unchanged.
    if (account_http_owned) {
        if (!easy || option != 0 || value != 1) Stop();
        option = 80;
    }
    return reinterpret_cast<int (__cdecl*)(void*, int, int)>(image + 0x8c94a0)(easy, option, value);
}

GoldQString* __fastcall AccountConfigHost(GoldQString* output, void*, const GoldQString*) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != image + 0x84a968) Stop();
    CheckOrbit();
    auto* client = reinterpret_cast<unsigned char*>(output) - 0x20;
    if (!ValidRange(client, 0x24, true)
        || *reinterpret_cast<uintptr_t*>(client) != image + 0xaec42c) Stop();
    char host[32]{};
    if (sprintf_s(host, "127.0.0.1:%u", account_config_port) <= 0) Stop();
    return reinterpret_cast<GoldQString* (__thiscall*)(void*, const char*)>(
        image + 0x794120)(output, host);
}

GoldQString* __fastcall AccountConfigUri(GoldQString* output, void*, const wchar_t* uri) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != image + 0x898812) Stop();
    wchar_t expected[80]{};
    swprintf_s(expected, L"http://127.0.0.1:%u/", account_config_port);
    const size_t count = wcslen(expected);
    if (!ValidRange(uri, (count + 1) * sizeof(wchar_t)) || wcsncmp(uri, expected, count)) Stop();
    wchar_t owned[256]{};
    swprintf_s(owned, L"http://127.0.0.1:%u/OnlineConfigService.svc/GetOnlineConfig?"
        L"onlineConfigID=WePlayGoldLoopback&target=client", account_config_port);
    return reinterpret_cast<GoldQString* (__thiscall*)(void*, const wchar_t*)>(
        image + 0x794140)(output, owned);
}

bool AccountIsService(uintptr_t account) {
    const uintptr_t main = *reinterpret_cast<uintptr_t*>(image + 0xcf83dc);
    if (!ValidRange(reinterpret_cast<void*>(main), 0x48)
        || *reinterpret_cast<uintptr_t*>(main) != image + 0xb55600) return false;
    const uintptr_t manager = *reinterpret_cast<uintptr_t*>(main + 0x10);
    if (!ValidRange(reinterpret_cast<void*>(manager), 0x10)) return false;
    const uintptr_t head = *reinterpret_cast<uintptr_t*>(manager + 8);
    if (!ValidRange(reinterpret_cast<void*>(head), 12)) return false;
    uintptr_t node = *reinterpret_cast<uintptr_t*>(head);
    bool found = false;
    for (unsigned int count = 0; node != head; ++count) {
        if (count == 128 || !ValidRange(reinterpret_cast<void*>(node), 12)) return false;
        if (*reinterpret_cast<uintptr_t*>(node + 8) == account) {
            if (found) return false;
            found = true;
        }
        node = *reinterpret_cast<uintptr_t*>(node);
    }
    return found;
}

bool AccountLocalSceneAllowed() {
    // This authorizes only the already-guarded local scene, never a session or
    // network Ready call. Native account state/backend are read, not changed.
    return local_login_dispatched && account_login_observed && account_login_owner
        && ValidRange(reinterpret_cast<void*>(account_login_owner), 0x94)
        && *reinterpret_cast<uintptr_t*>(account_login_owner) == image + 0xb5705c
        && AccountIsService(account_login_owner)
        && *reinterpret_cast<DWORD*>(account_login_owner + 0x50) == 6
        && *reinterpret_cast<void**>(account_login_owner + 0x18) == local_login_backend
        && local_login_backend;
}

bool __fastcall AccountSdkLogin(void* rendezvous, void*, void* context, const GoldQString*,
    const GoldQString*, const GoldQString*, void** backend) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != image + 0x6580df
        || local_login_dispatched || reinterpret_cast<uintptr_t>(backend) < 0x10000) Stop();
    CheckOrbit();
    const uintptr_t account = reinterpret_cast<uintptr_t>(backend) - 0x18;
    if (!ValidRange(reinterpret_cast<void*>(account), 0x94, true)
        || *reinterpret_cast<uintptr_t*>(account) != image + 0xb5705c
        || !AccountIsService(account) || *reinterpret_cast<unsigned int*>(account + 0x50) != 2
        || *reinterpret_cast<void**>(account + 0x54) != context || *backend
        || !ValidRange(context, 0x78, true) || *static_cast<uintptr_t*>(context) != image + 0xae96a0
        || LocalLoginRendezVous() != reinterpret_cast<uintptr_t>(rendezvous)) Stop();
    account_login_owner = account;
    local_login_context = context;
    local_login_dispatched = true;
    GoldOwnedQString host("127.0.0.1"), sandbox("WePlayGoldLoopback"), extra("");
    GoldOwnedQString name(local_login_name), password(local_login_secret);
    using Login = bool (__thiscall*)(void*, void*, const GoldQString*, unsigned int,
        const GoldQString*, const GoldQString*, const GoldQString*, const GoldQString*, void**);
    const bool queued = reinterpret_cast<Login>(image + 0x847e50)(rendezvous, context, &host.value,
        local_login_port, &sandbox.value, &extra.value, &name.value, &password.value, backend);
    SecureZeroMemory(local_login_secret, sizeof(local_login_secret));
    LocalLoginReport(queued ? "account-sdk-login-queued" : "account-sdk-login-rejected");
    return queued;
}

void __cdecl AccountExtensionInit(const GoldQString*) {
    if (reinterpret_cast<uintptr_t>(_ReturnAddress()) != image + 0x658167) Stop();
    CheckOrbit();
    const GoldQString key{L"WePlayGoldLoopback"};
    // Preserve the original extension factory/registration and its return path.
    reinterpret_cast<void (__cdecl*)(const GoldQString*)>(image + 0x84f3f0)(&key);
}

void LocalLoginPoll() {
    if (!local_login_dispatched || account_login_observed || !owner_thread.load()
        || GetCurrentThreadId() != owner_thread.load()) return;
    CheckOrbit();
    if (!ValidRange(reinterpret_cast<void*>(account_login_owner), 0x94)
        || *reinterpret_cast<uintptr_t*>(account_login_owner) != image + 0xb5705c
        || *reinterpret_cast<void**>(account_login_owner + 0x54) != local_login_context
        || !ValidRange(local_login_context, 0x78)
        || *static_cast<uintptr_t*>(local_login_context) != image + 0xae96a0) Stop();
    const DWORD account_state = *reinterpret_cast<DWORD*>(account_login_owner + 0x50);
    local_login_state.store(*reinterpret_cast<DWORD*>(static_cast<unsigned char*>(local_login_context) + 0xc));
    local_login_result.store(*reinterpret_cast<DWORD*>(static_cast<unsigned char*>(local_login_context) + 0x34));
    local_login_backend = *reinterpret_cast<void**>(account_login_owner + 0x18);
    static DWORD previous = 0xffffffff;
    if (account_state != previous) {
        previous = account_state;
        char phase[48]{};
        sprintf_s(phase, "native-account-state-%lu", account_state);
        LocalLoginReport(phase);
    }
    // Observation ends before logout/shutdown can release the original objects.
    account_login_observed = account_state == 0 || account_state == 6;
}

bool AccountLoginPrepare(std::vector<Patch>& patches) {
    // Gold's existing local identity adapter uses this exact independent name.
    // Re-login, another identity, persistent progression and lobby admission
    // require their own lifecycle implementation, not changed account flags.
    if (strcmp(local_login_name, "WePlayGoldLocal")) return false;
    char port[8]{};
    const DWORD length = GetEnvironmentVariableA("WEPLAY_GOLD_CONFIG_PORT", port, 8);
    if (!length || length >= 8) return false;
    for (DWORD i = 0; i < length; ++i) {
        if (port[i] < '0' || port[i] > '9') return false;
        account_config_port = account_config_port * 10 + static_cast<unsigned int>(port[i] - '0');
    }
    if (account_config_port < 1024 || account_config_port > 65535) return false;
    const struct { uintptr_t rva; std::vector<unsigned char> bytes; } guards[] = {
        {0x794140, {0x8b,0x44,0x24,0x04,0x56,0x8b,0xf1}},
        {0x85f6b0, {0x51,0x53,0x56,0x8b,0xd9,0x57,0x33,0xff}},
        {0x860f44, {0xeb,0x0f}},
        // The offline unlock inverted this Lua binding's native backend result.
        // Restore its original boolean conversion, retaining the actual account
        // virtual call (+40) and its false result when no backend exists.
        {0x690bfe, {0x8b,0x13,0x8b,0x42,0x40,0x8b,0xcb,0xff,0xd0,
            0x8b,0x55,0x08,0x33,0xc9,0x84,0xc0}},
        {0x8c94a0, {0x8b,0x44,0x24,0x04,0x85,0xc0}},
        {0x84f3f0, {0x8b,0x44,0x24,0x04,0x50,0xe8,0x86,0xc7,0xfd,0xff}}
    };
    for (const auto& guard : guards)
        if (memcmp(reinterpret_cast<void*>(image + guard.rva), guard.bytes.data(), guard.bytes.size())) return false;
    const unsigned char inverted[] = {0x0f,0x94,0xc1};
    const unsigned char original[] = {0x0f,0x95,0xc1};
    return AddBytes(patches, 0x690c0e, inverted, original, sizeof(original))
        && Add(patches, 0x84a963, {0xe8,0xf8,0x97,0xf4,0xff}, AccountConfigHost)
        && Add(patches, 0x89880d, {0xe8,0x2e,0xb9,0xef,0xff}, AccountConfigUri)
        && Add(patches, 0x82a57a, {0xe8,0x31,0x51,0x03,0x00}, AccountHttpFactory)
        && Add(patches, 0x82a5aa, {0xe8,0xa1,0x66,0x03,0x00}, AccountHttpHeaders)
        && Add(patches, 0x860c7f, {0xe8,0x3c,0x9f,0x06,0x00}, AccountHeaderAppend)
        && Add(patches, 0x860f5c, {0xe8,0x3f,0x85,0x06,0x00}, AccountHttpMethod)
        && Add(patches, 0x6580da, {0xe8,0xe1,0x01,0x1f,0x00}, AccountSdkLogin)
        && Add(patches, 0x658162, {0xe8,0x89,0x72,0x1f,0x00}, AccountExtensionInit);
}
