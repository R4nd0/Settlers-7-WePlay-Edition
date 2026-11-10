namespace {
void GoldVisualFixture() {
    for (uintptr_t rva : {0x1593cau,0x3a71aau}) {
        const unsigned char opcode[]={0xf3,0x0f,0x59,0x05};
        memcpy(reinterpret_cast<void*>(image+rva-4),opcode,4);
        *reinterpret_cast<uintptr_t*>(image+rva)=image+0xb626e4;
    }
    for (uintptr_t rva : {0x40c95fu,0x40db4fu}) {
        const unsigned char instruction[]={0xb8,0,8,0,0};
        memcpy(reinterpret_cast<void*>(image+rva),instruction,5);
    }
    const uintptr_t operands[][2]={{0x40e821,0xb62778},{0x40e834,0xb6243c},
        {0x40e878,0xb62770},{0x40e891,0xb6276c},{0x40ea42,0xb6276c},{0x40e85e,0xb62774}};
    for (const auto& operand:operands) {
        auto* at=reinterpret_cast<unsigned char*>(image+operand[0]);
        at[-2]=operand[0]==0x40e85e ? 0xd8 : 0xd9;
        at[-1]=operand[0]==0x40e85e ? 0x0d : 0x05;
        *reinterpret_cast<uintptr_t*>(at)=image+operand[1];
    }
    const unsigned char caller[]={0x8b,0x93,0xac,0x19,0,0,0x8b,0x83,0xb0,0x19,0,0,
        0x6a,1,0x8d,0x4b,0x7c,0x51,0x52,0xff,0xd0};
    memcpy(reinterpret_cast<void*>(image+0x411550),caller,sizeof(caller));
    const unsigned char index[]={0x8b,0x44,0x24,0x10,0xd9,0x9c,0x24,0xa0,0,0,0,0x8d,0x84,0x84,0xa0,0,0,0};
    memcpy(reinterpret_cast<void*>(image+0x4114c3),index,sizeof(index));
    *reinterpret_cast<unsigned char*>(image+0x40bcff)=0xa3;
    *reinterpret_cast<uintptr_t*>(image+0x40bd00)=image+0xce6e3c;
}
bool GoldVisualGuards() {
    constexpr const wchar_t* flags[]={L"S7_EXTENDED_DETAIL",L"S7_DETAILED_SHADOWS",
        L"S7_EXTENDED_SHADOW_RANGE",L"S7_ULTRA_SHADOWS"};
    // Preserve a developer's inherited selection after this fixture.
    std::array<std::wstring,4> inherited{};std::array<bool,4> present{};
    for (size_t i=0;i<4;++i) {
        wchar_t value[32768]{};SetLastError(ERROR_SUCCESS);
        const DWORD length=GetEnvironmentVariableW(flags[i],value,32768);
        present[i]=length || GetLastError()!=ERROR_ENVVAR_NOT_FOUND;
        inherited[i]=value;SetEnvironmentVariableW(flags[i],nullptr);
    }
    const auto check=[]() {
        GoldVisualFixture();std::vector<Patch> none;
        if (!GoldVisualPatches(none) || !none.empty()) return false;
        SetEnvironmentVariableW(L"S7_EXTENDED_DETAIL",L"1");
        SetEnvironmentVariableW(L"S7_DETAILED_SHADOWS",L"1");
        SetEnvironmentVariableW(L"S7_EXTENDED_SHADOW_RANGE",L"1");
        SetEnvironmentVariableW(L"S7_ULTRA_SHADOWS",L"1");
        std::vector<Patch> patches;
        if (!GoldVisualPatches(patches) || patches.size()!=11) return false;
        for (const auto& patch:patches) if (!Write(patch,true)) return false;
        if (*reinterpret_cast<uintptr_t*>(image+0x1593ca)!=reinterpret_cast<uintptr_t>(&GoldLodScale)
            || *reinterpret_cast<uint32_t*>(image+0x40c960)!=8192
            || *reinterpret_cast<uintptr_t*>(image+0x40ea42)!=reinterpret_cast<uintptr_t>(&GoldShadowFarEnd)) return false;
        for (const auto& patch:patches) if (!Write(patch,false)) return false;
        // Each mapped instruction, relocation and stack-index/owner guard is
        // independently corrupted. Rejection must not write any game memory.
        for (uintptr_t rva : {0x1593c6u,0x1593cau,0x3a71a6u,0x3a71aau,
            0x40c95fu,0x40db4fu,0x40e81fu,0x40e821u,0x40e832u,0x40e834u,
            0x40e85cu,0x40e85eu,0x40e876u,0x40e878u,0x40e88fu,0x40e891u,
            0x40ea40u,0x40ea42u,0x411550u,0x41155cu,0x411561u,0x4114c3u,0x4114ceu,
            0x40bcffu,0x40bd00u}) {
            auto* at=reinterpret_cast<unsigned char*>(image+rva);*at^=1;
            std::vector<Patch> rejected;const bool accepted=GoldVisualPatches(rejected);*at^=1;
            if (accepted) return false;
        }
        SetEnvironmentVariableW(L"S7_ULTRA_SHADOWS",L"0");
        std::vector<Patch> detailed;
        if (!GoldVisualPatches(detailed) || detailed.size()!=10
            || detailed[2].after[2]!=0x10) return false;
        for (const wchar_t* flag : {L"S7_EXTENDED_DETAIL",L"S7_DETAILED_SHADOWS",
            L"S7_EXTENDED_SHADOW_RANGE",L"S7_ULTRA_SHADOWS"}) {
            for (const wchar_t* invalid : {L"",L"2",L"true",L"01"}) {
                SetEnvironmentVariableW(flag,invalid);std::vector<Patch> rejected;
                if (GoldVisualPatches(rejected)) return false;
            }
            SetEnvironmentVariableW(flag,L"1");
        }
        return true;
    };
    const bool valid=check();
    for (size_t i=0;i<4;++i) SetEnvironmentVariableW(flags[i],present[i] ? inherited[i].c_str() : nullptr);
    return valid;
}
bool GoldFilterAbi(unsigned int size, unsigned int cascade, bool owned, bool inverse_valid) {
    std::array<unsigned int,0x90/4> renderer{};
    renderer[0x78/4]=size;
    const float inverse=1.f/static_cast<float>(size);
    for (size_t i=0;i<4;++i) memcpy(&renderer[0x7c/4+i],&inverse,4);
    if (!inverse_valid) renderer[0x88/4]^=1;
    gold_shadow_owner=reinterpret_cast<uintptr_t*>(image+0xce6e3c);
    *gold_shadow_owner=owned ? reinterpret_cast<uintptr_t>(renderer.data()) : 0;
    auto* object=renderer.data();uintptr_t vector=0;
    unsigned int before=0,after=0,pushed=0,observed_ax=0,observed_dx=0,observed_si=0,
        observed_di=0,observed_bx=0,flags_before=0,flags_after=0;
    const float x87_input=3.25f;float x87_output=0;
    __asm {
        mov before, esp
        sub esp, 020h
        mov eax, cascade
        mov [esp+010h], eax
        mov ebx, object
        mov esi, 0x11223344
        mov edi, 0x55667788
        fld x87_input
        mov eax, 0x12345678
        mov edx, 0x34567890
        stc
        pushfd
        pop flags_before
        call GoldShadowFilterBridge
        pop pushed
        pushfd
        pop flags_after
        mov observed_ax, eax
        mov observed_dx, edx
        mov observed_si, esi
        mov observed_di, edi
        mov observed_bx, ebx
        mov vector, ecx
        fstp x87_output
        add esp, 020h
        mov after, esp
    }
    const bool enhanced=owned && inverse_valid && (size==4096 || size==8192) && cascade>=2 && cascade<=3;
    const uintptr_t expected=enhanced ? reinterpret_cast<uintptr_t>(GoldFilterStep)
        : reinterpret_cast<uintptr_t>(renderer.data())+0x7c;
    return before==after && pushed==1 && observed_ax==0x12345678 && observed_dx==0x34567890
        && observed_si==0x11223344 && observed_di==0x55667788 && observed_bx==reinterpret_cast<uintptr_t>(object)
        && ((flags_before^flags_after)&0x8d5)==0 && x87_output==x87_input && vector==expected;
}
bool GoldFilterCases() {
    for (unsigned int size : {2048u,4096u,8192u})
        for (unsigned int cascade : {0u,1u,2u,3u,4u})
            for (bool owner : {false,true})
                for (bool inverse : {false,true})
                    if (!GoldFilterAbi(size,cascade,owner,inverse)) return false;
    return true;
}
}
