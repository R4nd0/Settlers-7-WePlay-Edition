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
        {0x690bfe,{0x8b,0x13,0x8b,0x42,0x40,0x8b,0xcb,0xff,0xd0,
            0x8b,0x55,0x08,0x33,0xc9,0x84,0xc0}},
        {0x690c0e,{0x0f,0x94,0xc1}},
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
    if (!AccountLoginPrepare(patches) || patches.size()!=9) return false;
    unsigned int rejections=0;
    for (const auto& field : fixture) for (size_t i=0;i<field.bytes.size();++i) {
        auto* at=reinterpret_cast<unsigned char*>(image+field.rva+i);*at^=1;
        account_config_port=0;std::vector<Patch> rejected;
        const bool accepted=AccountLoginPrepare(rejected);*at^=1;
        if (accepted) return false;
        ++rejections;
    }
    for (auto& patch : patches) if (!Write(patch,true)) return false;
    // Execute the restored conversion for every possible native AL result.
    // In particular zero must stay false after the original account call.
    auto* conversion=reinterpret_cast<unsigned char*>(image+0x690c0a);
    conversion[7]=0xc3;
    for (unsigned int value=0;value<256;++value) {
        unsigned int result=0;
        __asm {
            mov eax,value
            call conversion
            mov result,ecx
        }
        if (result!=(value ? 1u : 0u)) return false;
    }
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
#ifdef S7_GOLD_LOCAL_SIMULATION
bool GoldAccountLocalScene() {
    LocalSetup();
    std::array<uintptr_t,40> account{};
    std::array<uintptr_t,18> main{};
    std::array<uintptr_t,4> manager{};
    std::array<uintptr_t,3> head{}, node{};
    account[0]=image+0xb5705c;account[0x50/4]=6;
    account[0x18/4]=reinterpret_cast<uintptr_t>(message_backend.data());
    main[0]=image+0xb55600;main[0x10/4]=reinterpret_cast<uintptr_t>(manager.data());
    manager[8/4]=reinterpret_cast<uintptr_t>(head.data());
    head[0]=reinterpret_cast<uintptr_t>(node.data());
    node[0]=reinterpret_cast<uintptr_t>(head.data());node[8/4]=reinterpret_cast<uintptr_t>(account.data());
    *reinterpret_cast<uintptr_t*>(image+0xcf83dc)=reinterpret_cast<uintptr_t>(main.data());
    account_login_owner=reinterpret_cast<uintptr_t>(account.data());
    local_login_backend=message_backend.data();local_login_dispatched=true;account_login_observed=true;
    if (!AccountLocalSceneAllowed()) return false;
    const unsigned char selected[]={0xb0,0x01,0xc3};
    memcpy(reinterpret_cast<void*>(image+0x58c910),selected,sizeof(selected));
    for (bool result : {false,true}) {
        fixture_result=result;
        if (!LocalReadyAbi() || !LocalAdvanceAbi()) return false;
    }
    // None of these invalid account contexts authorizes even a local scene.
    account[0x50/4]=0;if (AccountLocalSceneAllowed()) return false;account[0x50/4]=6;
    account[0x18/4]=0;if (AccountLocalSceneAllowed()) return false;
    account[0x18/4]=reinterpret_cast<uintptr_t>(message_backend.data());
    account[0]++;if (AccountLocalSceneAllowed()) return false;account[0]--;
    node[8/4]=0;if (AccountLocalSceneAllowed()) return false;node[8/4]=account_login_owner;
    local_login_dispatched=false;if (AccountLocalSceneAllowed()) return false;local_login_dispatched=true;
    account_login_observed=false;if (AccountLocalSceneAllowed()) return false;account_login_observed=true;
    local_login_backend=nullptr;if (AccountLocalSceneAllowed()) return false;
    account_login_owner=0;account_login_observed=false;local_login_dispatched=false;
    *reinterpret_cast<uintptr_t*>(image+0xcf83dc)=0;
    LocalSetup();
    std::printf("Gold authenticated local scene ABI/failure forwarding and seven account denials passed.\n");
    return true;
}
#endif
}
#endif
