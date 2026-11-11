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
        {0x652610,{0x55,0x8b,0xec,0xa1}},
        {0x652618,{0x8b,0x48,0x04,0x53,0x8b,0x5d,0x14,0x56,0x57}},
        {0x84f3f0,{0x8b,0x44,0x24,0x04,0x50,0xe8,0x86,0xc7,0xfd,0xff}},
        {0x84a963,{0xe8,0xf8,0x97,0xf4,0xff}},
        {0x89880d,{0xe8,0x2e,0xb9,0xef,0xff}},
        {0x82a57a,{0xe8,0x31,0x51,0x03,0x00}},
        {0x82a5aa,{0xe8,0xa1,0x66,0x03,0x00}},
        {0x860c7f,{0xe8,0x3c,0x9f,0x06,0x00}},
        {0x860f5c,{0xe8,0x3f,0x85,0x06,0x00}},
        {0x6580da,{0xe8,0xe1,0x01,0x1f,0x00}},
        {0x658162,{0xe8,0x89,0x72,0x1f,0x00}},
        {0x68674d,{0xe8,0xbe,0xbe,0xfc,0xff}},
        {0x687bec,{0xe8,0x1f,0xaa,0xfc,0xff}}
    };
    strcpy_s(local_login_name,"WePlayGoldLocal");
    if (!SetEnvironmentVariableA("WEPLAY_GOLD_CONFIG_PORT","55123")) return false;
    for (const auto& field : fixture)
        memcpy(reinterpret_cast<void*>(image+field.rva),field.bytes.data(),field.bytes.size());
    *reinterpret_cast<uintptr_t*>(image+0x652614)=image+0xcf83d8;
    account_config_port=0;
    std::vector<Patch> patches;
    if (!AccountLoginPrepare(patches) || patches.size()!=11) return false;
    unsigned int rejections=0;
    for (const auto& field : fixture) for (size_t i=0;i<field.bytes.size();++i) {
        auto* at=reinterpret_cast<unsigned char*>(image+field.rva+i);*at^=1;
        account_config_port=0;std::vector<Patch> rejected;
        const bool accepted=AccountLoginPrepare(rejected);*at^=1;
        if (accepted) return false;
        ++rejections;
    }
    for (unsigned int i=0;i<4;++i) {
        auto* at=reinterpret_cast<unsigned char*>(image+0x652614+i);*at^=1;
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
    for (const char* label : {"WePlayGoldPeer", "X",
        "abcdefghijklmnopqrstuvwxyzABCDEFGHIJKLMNOPQRSTUVWXYZ0123456789ab"}) {
        strcpy_s(local_login_name,label); account_config_port=0;
        std::vector<Patch> alternate;
        if (!AccountLoginPrepare(alternate) || alternate.size()!=11) return false;
        std::wstring wide_label; for (const char* p=label; *p; ++p) wide_label+=static_cast<wchar_t>(*p);
        if (!IdentityAbi(label,wide_label.c_str())) return false;
    }
    for (const char* invalid : {"", "fixture\n", "fixture\x7f", "fixture\x80"}) {
        strcpy_s(local_login_name,invalid); account_config_port=0;
        std::vector<Patch> rejected;
        if (AccountLoginPrepare(rejected)) return false;
    }
    memset(local_login_name,'X',sizeof(local_login_name));account_config_port=0;
    std::vector<Patch> unterminated;
    if (AccountLoginPrepare(unterminated)) return false;
    strcpy_s(local_login_name,"WePlayGoldLocal");
    if (!ConfigureLocalIdentity(local_login_name) || !IdentityAbi()) return false;
    SetEnvironmentVariableA("WEPLAY_GOLD_CONFIG_PORT",nullptr);
    account_http_owned=false;if (!AccountTestMethod()) return false;
    account_http_owned=true;if (!AccountTestMethod()) return false;
    account_http_owned=false;
    std::printf("Gold account instruction rejections: %u; HTTP ABI/failure forwarding passed.\n",rejections);
    return true;
}
#ifdef S7_GOLD_LOCAL_SIMULATION
std::array<uintptr_t,7> participant_arguments{};
std::string participant_name;
unsigned int participant_result=0;
unsigned int __stdcall AccountTestCreate(void* session, const GoldStartup::AsciiString* input,
    unsigned int field70, unsigned int field7c, unsigned int principal,
    unsigned int flag75, unsigned int flag74) {
    participant_arguments={reinterpret_cast<uintptr_t>(session),reinterpret_cast<uintptr_t>(input),
        field70,field7c,principal,flag75,flag74};
    participant_name.assign(static_cast<const char*>(input->bytes),input->length);
    SetLastError(43);
    return participant_result;
}
bool AccountParticipantAbi(void* session) {
    auto* target=reinterpret_cast<unsigned char*>(image+0x652610);target[0]=0xe9;
    const uint32_t jump=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(AccountTestCreate)
        -reinterpret_cast<uintptr_t>(target)-5);
    memcpy(target+1,&jump,4);
    strcpy_s(local_login_name,"WePlayGoldLocal");
    char existing[]="Existing local name";
    for (uintptr_t rva : {0x68674du,0x687becu}) for (bool supplied : {false,true})
        for (unsigned int native_result : {0u,0xfa123u}) {
        GoldStartup::AsciiString input{supplied ? nullptr : existing,
            supplied ? 0u : 19u,supplied ? 0u : 19u,0};
        const auto* input_pointer=&input;
        auto* at=reinterpret_cast<unsigned char*>(image+rva);at[0]=0xe8;
        const uint32_t call=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(AccountCreateParticipant)
            -reinterpret_cast<uintptr_t>(at)-5);
        memcpy(at+1,&call,4);at[5]=0xc3;
        DWORD before=0,after=0,observed_bx=0,observed_si=0,observed_di=0,result=0;
        participant_result=native_result;
        __asm {
            mov before,esp
            mov ebx,0x12345678
            mov esi,0x34567890
            mov edi,0x56789012
            lea eax,participant_return
            push eax
            push 0xdeadbeef
            push 0xabcdef42
            push 42
            push 0x76543210
            push 0x13572468
            push input_pointer
            push session
            jmp at
        participant_return:
            mov result,eax
            mov after,esp
            mov observed_bx,ebx
            mov observed_si,esi
            mov observed_di,edi
        }
        const DWORD error=GetLastError();
        if (before!=after || observed_bx!=0x12345678 || observed_si!=0x34567890 || observed_di!=0x56789012
            || result!=native_result || error!=43
            || participant_arguments[0]!=reinterpret_cast<uintptr_t>(session)
            || (!supplied && participant_arguments[1]!=reinterpret_cast<uintptr_t>(input_pointer))
            || participant_arguments[2]!=0x13572468 || participant_arguments[3]!=0x76543210
            || participant_arguments[4]!=42 || participant_arguments[5]!=0xabcdef42
            || participant_arguments[6]!=0xdeadbeef
            || participant_name!=(supplied ? "WePlayGoldLocal" : existing)
            || input.length!=(supplied ? 0u : 19u) || input.bytes!=(supplied ? nullptr : existing)) return false;
    }
    return true;
}
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
    std::array<uintptr_t,18> session{};
    std::array<uintptr_t,3> session_node{},rendezvous_node{},context{};
    std::array<uintptr_t,9> rendezvous_service{};
    std::array<uintptr_t,48> rendezvous{};
    session[0]=image+0xb569bc;main[0x14/4]=reinterpret_cast<uintptr_t>(session.data());
    node[0]=reinterpret_cast<uintptr_t>(session_node.data());
    session_node[0]=reinterpret_cast<uintptr_t>(rendezvous_node.data());
    session_node[2]=reinterpret_cast<uintptr_t>(session.data());
    rendezvous_node[0]=reinterpret_cast<uintptr_t>(head.data());
    rendezvous_node[2]=reinterpret_cast<uintptr_t>(rendezvous_service.data());
    rendezvous_service[0]=image+0xb56bbc;
    rendezvous_service[2]=reinterpret_cast<uintptr_t>(rendezvous.data());
    rendezvous[0]=image+0xaec274;
    rendezvous[0x78/4]=reinterpret_cast<uintptr_t>(context.data());context[2]=42;
    rendezvous[0xb0/4]=1;
    if (LocalLoginRendezVous()!=0
        || LocalLoginRendezVous(false)!=reinterpret_cast<uintptr_t>(rendezvous.data())) return false;
    GoldStartup::AsciiString empty{};
    const auto eligible=[&]() { return AccountParticipantNameEligible(session.data(),&empty,42); };
    if (!eligible() || !AccountParticipantAbi(session.data())) return false;
    empty.length=1;if (eligible()) return false;empty.length=0;
    empty.capacity=4097;if (eligible()) return false;empty.capacity=0;
    char nonempty[]="x";empty.bytes=nonempty;if (eligible()) return false;empty.bytes=nullptr;
    if (AccountParticipantNameEligible(session.data(),nullptr,42)
        || AccountParticipantNameEligible(session.data(),&empty,0)
        || AccountParticipantNameEligible(session.data(),&empty,43)
        || AccountParticipantNameEligible(nullptr,&empty,42)) return false;
    session[0]++;if (eligible()) return false;session[0]--;
    session_node[2]=0;if (eligible()) return false;session_node[2]=main[0x14/4];
    rendezvous_service[2]=0;if (eligible()) return false;
    rendezvous_service[2]=reinterpret_cast<uintptr_t>(rendezvous.data());
    rendezvous[0]++;if (eligible()) return false;rendezvous[0]--;
    rendezvous[0x78/4]=0;if (eligible()) return false;
    rendezvous[0x78/4]=reinterpret_cast<uintptr_t>(context.data());
    account[0x50/4]=0;if (eligible()) return false;account[0x50/4]=6;
    const DWORD thread=owner_thread.exchange(0);const bool wrong_thread=eligible();owner_thread.store(thread);
    if (wrong_thread || !eligible()) return false;
    std::printf("Gold native participant creation: both callers, original success/failure, seven arguments, stack/register/error preservation and identity exclusions passed.\n");
    const unsigned char selected[]={0xb0,0x01,0xc3};
    memcpy(reinterpret_cast<void*>(image+0x58c910),selected,sizeof(selected));
    for (bool result : {false,true}) {
        fixture_result=result;
        if (!LocalReadyAbi() || !LocalAdvanceAbi()) return false;
    }
    // Native playing-session ownership and exact connected roster are required
    // independently of successful account login or the global network flag.
    std::array<uintptr_t,0x1ac/4> preparation{};
    std::vector<uintptr_t> players(25*0x5c4/4);
    uintptr_t players_ref=reinterpret_cast<uintptr_t>(players.data());
    preparation[0xa4/4]=reinterpret_cast<uintptr_t>(&players_ref);
    *reinterpret_cast<uintptr_t*>(image+0xcf834c)=reinterpret_cast<uintptr_t>(preparation.data());
    session[0x10/4]=2;session[0x30/4]=2;session[0xc/4]=1;
    session[0x3c/4]=7;session[0x40/4]=9;
    for (unsigned int i=1;i<=2;++i) {
        players[(i*0x5c4+0x64)/4]=1;
        players[(i*0x5c4+0x60)/4]=3;
        players[(i*0x5c4+0x5c)/4]=i;
    }
    if (NetworkLocalReadyAllowed()) return false;
    local_multiplayer_enabled=true;
    for (unsigned int local : {1u,2u}) {
        session[0xc/4]=local;
        if (!NetworkLocalReadyAllowed()) return false;
        for (bool result : {false,true}) {
            fixture_result=result;
            if (!LocalReadyAbi(true) || !LocalAdvanceAbi()) return false;
        }
    }
    unsigned int network_denials=0;
    const auto denied=[&](uintptr_t& field,uintptr_t bad) {
        const auto prior=field;field=bad;
        const bool accepted=NetworkLocalReadyAllowed();field=prior;
        ++network_denials;return !accepted && NetworkLocalReadyAllowed();
    };
    for (unsigned int value : {0u,1u,3u,0xffffffffu})
        if (!denied(session[0x10/4],value)) return false;
    for (unsigned int value : {0u,1u,3u})
        if (!denied(session[0x30/4],value)) return false;
    for (unsigned int offset : {0x3cu,0x40u}) for (unsigned int value : {0u,0xffffffffu})
        if (!denied(session[offset/4],value)) return false;
    for (unsigned int value : {0u,3u,0xffffffffu})
        if (!denied(session[0xc/4],value)) return false;
    if (!denied(session[0],0) || !denied(session_node[2],0)
        || !denied(account[0x50/4],0) || !denied(preparation[0xa4/4],0)
        || !denied(players_ref,0)) return false;
    for (unsigned int i=1;i<=2;++i) {
        for (unsigned int kind : {0u,2u}) if (!denied(players[(i*0x5c4+0x64)/4],kind)) return false;
        for (unsigned int status : {0u,1u,2u}) if (!denied(players[(i*0x5c4+0x60)/4],status)) return false;
        for (unsigned int index : {0u,3u,0xffffffffu,3u-i})
            if (!denied(players[(i*0x5c4+0x5c)/4],index)) return false;
    }
    if (!denied(players[(3*0x5c4+0x64)/4],2)) return false;
    const auto original_thread=owner_thread.exchange(0);
    const bool wrong_network_thread=NetworkLocalReadyAllowed();owner_thread.store(original_thread);
    if (wrong_network_thread || !NetworkLocalReadyAllowed()) return false;
    local_multiplayer_enabled=false;
    *reinterpret_cast<uintptr_t*>(image+0xcf834c)=0;
    std::printf("Gold two-human native network Ready: both roles, original false/true/output/error/ABI forwarding and %u session/roster denials passed.\n",network_denials+2);
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
