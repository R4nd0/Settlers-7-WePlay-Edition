#ifdef S7_GOLD_ACCOUNT_LOGIN
namespace {
int account_test_option = -1, account_test_value = -1;
void* account_test_easy = nullptr;
int __cdecl AccountTestCurl(void* easy, int option, int value) {
    account_test_easy = easy; account_test_option = option; account_test_value = value;
    return 43; // Preserve a genuine failure result; the bridge must not fake success.
}
bool AccountTestMethod(bool wrong_caller = false, int option = 0, int value = 1) {
    auto* target = reinterpret_cast<unsigned char*>(image + 0x8c94a0);
    target[0] = 0xe9;
    const uint32_t jump = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(AccountTestCurl)
        - reinterpret_cast<uintptr_t>(target) - 5);
    memcpy(target + 1, &jump, 4);
    auto* at = reinterpret_cast<unsigned char*>(image + 0x860f5c + (wrong_caller ? 1 : 0));
    at[0] = 0xe8;
    const uint32_t call = static_cast<uint32_t>(reinterpret_cast<uintptr_t>(AccountHttpMethod)
        - reinterpret_cast<uintptr_t>(at) - 5);
    memcpy(at + 1, &call, 4);
    const unsigned char leave[] = {0x83,0xc4,0x0c,0xc3};
    memcpy(at + 5, leave, sizeof(leave));
    DWORD before = 0, after = 0, result = 0;
    __asm {
        mov before, esp
        lea eax, method_return
        push eax
        push value
        push option
        push 0x12345678
        jmp at
    method_return:
        mov result, eax
        mov after, esp
    }
    return before == after && result == 43 && account_test_easy == reinterpret_cast<void*>(0x12345678)
        && account_test_value == value && account_test_option == (account_http_owned ? 80 : option);
}
bool GoldAccountGuards() {
    const struct { uintptr_t rva; std::vector<unsigned char> bytes; } fixture[] = {
        {0x794140,{0x8b,0x44,0x24,0x04,0x56,0x8b,0xf1}},
        {0x85f6b0,{0x51,0x53,0x56,0x8b,0xd9,0x57,0x33,0xff}},
        {0x860f44,{0xeb,0x0f}},
        {0x8c94a0,{0x8b,0x44,0x24,0x04,0x85,0xc0}},
        {0x84f3f0,{0x8b,0x44,0x24,0x04,0x50,0xe8,0x86,0xc7,0xfd,0xff}},
        {0x84a963,{0xe8,0xf8,0x97,0xf4,0xff}},
        {0x89880d,{0xe8,0x2e,0xb9,0xef,0xff}},
        {0x82a57a,{0xe8,0x31,0x51,0x03,0x00}},
        {0x82a5aa,{0xe8,0xa1,0x66,0x03,0x00}},
        {0x860c7f,{0xe8,0x3c,0x9f,0x06,0x00}},
        {0x860f5c,{0xe8,0x3f,0x85,0x06,0x00}},
        {0x6580da,{0xe8,0xe1,0x01,0x1f,0x00}},
        {0x658162,{0xe8,0x89,0x72,0x1f,0x00}}
    };
    strcpy_s(local_login_name,"WePlayGoldLocal");
    if (!SetEnvironmentVariableA("WEPLAY_GOLD_CONFIG_PORT","55123")) return false;
    for (const auto& field : fixture)
        memcpy(reinterpret_cast<void*>(image+field.rva),field.bytes.data(),field.bytes.size());
    account_config_port=0;
    std::vector<Patch> patches;
    if (!AccountLoginPrepare(patches) || patches.size()!=8) return false;
    unsigned int rejections=0;
    for (const auto& field : fixture) for (size_t i=0;i<field.bytes.size();++i) {
        auto* at=reinterpret_cast<unsigned char*>(image+field.rva+i);*at^=1;
        account_config_port=0;std::vector<Patch> rejected;
        const bool accepted=AccountLoginPrepare(rejected);*at^=1;
        if (accepted) return false;
        ++rejections;
    }
    for (auto& patch : patches) if (!Write(patch,true)) return false;
    for (auto& patch : patches) if (!Write(patch,false)) return false;
    strcpy_s(local_login_name,"another-local-identity");account_config_port=0;
    std::vector<Patch> rejected;
    if (AccountLoginPrepare(rejected)) return false;
    SetEnvironmentVariableA("WEPLAY_GOLD_CONFIG_PORT",nullptr);
    account_http_owned=false;if (!AccountTestMethod()) return false;
    account_http_owned=true;if (!AccountTestMethod()) return false;
    account_http_owned=false;
    std::printf("Gold account instruction rejections: %u; HTTP ABI/failure forwarding passed.\n",rejections);
    return true;
}
}
#endif
