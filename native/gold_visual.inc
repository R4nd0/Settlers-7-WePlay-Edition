// Independently mapped Gold renderer. All addresses below are RVAs in the
// pinned Gold image, not History Edition addresses or simulation operands.
const float GoldLodScale = 1.f / 200.f;
const float GoldShadowSpan = 19100.f, GoldShadowRatio = 192.f;
const float GoldShadowEnd = 19200.f, GoldShadowFarEnd = 76800.f;
const float GoldNearBlend = .92f;
const float GoldFilterStep[] = {1.f/2048.f,1.f/2048.f,1.f/2048.f,1.f/2048.f};
uintptr_t* gold_shadow_owner = nullptr;

// Gold keeps the cascade index at original ESP+10h and passes the vector in
// ECX. EAX already contains the shader callback; ESI is not the cascade index.
// Reproduce PUSH 1 below the return address and preserve EAX/EDX/flags/x87.
__declspec(naked) void GoldShadowFilterBridge() {
    __asm {
        pushfd
        push eax
        lea ecx, [ebx+07ch]
        mov eax, gold_shadow_owner
        cmp [eax], ebx
        jne finished
        mov eax, [esp+01ch]
        cmp eax, 2
        jb finished
        cmp eax, 3
        ja finished
        mov eax, [ebx+078h]
        cmp eax, 8192
        je map8192
        cmp eax, 4096
        jne finished
        mov eax, 039800000h
        jmp inverse
    map8192:
        mov eax, 039000000h
    inverse:
        cmp [ecx], eax
        jne finished
        cmp [ecx+4], eax
        jne finished
        cmp [ecx+8], eax
        jne finished
        cmp [ecx+12], eax
        jne finished
        mov ecx, offset GoldFilterStep
    finished:
        pop eax
        popfd
        push edx
        mov edx, [esp+4]
        mov dword ptr [esp+4], 1
        xchg edx, [esp]
        ret
    }
}

bool GoldVisualFlag(const wchar_t* name, bool& enabled) {
    wchar_t value[3]{};SetLastError(ERROR_SUCCESS);
    const DWORD length=GetEnvironmentVariableW(name,value,3);
    if (!length && GetLastError()==ERROR_ENVVAR_NOT_FOUND) { enabled=false;return true; }
    if (length!=1 || (value[0]!=L'0' && value[0]!=L'1')) return false;
    enabled=value[0]==L'1';return true;
}
bool GoldFloatOperand(std::vector<Patch>& patches, uintptr_t rva, unsigned char first,
    unsigned char second, uintptr_t original_rva, const float* replacement) {
    const auto* opcode=reinterpret_cast<const unsigned char*>(image+rva-2);
    if (opcode[0]!=first || opcode[1]!=second) return false;
    const uintptr_t original=image+original_rva, after=reinterpret_cast<uintptr_t>(replacement);
    return AddBytes(patches,rva,&original,&after,4);
}
bool GoldLodOperand(std::vector<Patch>& patches, uintptr_t rva) {
    const unsigned char opcode[]={0xf3,0x0f,0x59,0x05};
    if (memcmp(reinterpret_cast<void*>(image+rva-4),opcode,4)) return false;
    const uintptr_t original=image+0xb626e4,after=reinterpret_cast<uintptr_t>(&GoldLodScale);
    return AddBytes(patches,rva,&original,&after,4);
}
bool GoldFilterPatch(std::vector<Patch>& patches) {
    const unsigned char original[]={0x6a,1,0x8d,0x4b,0x7c};
    const unsigned char before[]={0x8b,0x93,0xac,0x19,0,0,0x8b,0x83,0xb0,0x19,0,0};
    const unsigned char after[]={0x51,0x52,0xff,0xd0};
    const unsigned char index[]={0x8b,0x44,0x24,0x10,0xd9,0x9c,0x24,0xa0,0,0,0,0x8d,0x84,0x84,0xa0,0,0,0};
    // Verify which global owns this 0x251c-byte renderer, independently of
    // adjacent atmospheric renderers with different layouts.
    const unsigned char owner_store[]={0xa3};
    const uintptr_t owner=image+0xce6e3c;
    if (memcmp(reinterpret_cast<void*>(image+0x411550),before,sizeof(before))
        || memcmp(reinterpret_cast<void*>(image+0x411561),after,sizeof(after))
        || memcmp(reinterpret_cast<void*>(image+0x4114c3),index,sizeof(index))
        || memcmp(reinterpret_cast<void*>(image+0x40bcff),owner_store,1)
        || *reinterpret_cast<uintptr_t*>(image+0x40bd00)!=owner) return false;
    unsigned char replacement[5]={0xe8,0,0,0,0};
    const uint32_t relative=static_cast<uint32_t>(reinterpret_cast<uintptr_t>(GoldShadowFilterBridge)-(image+0x411561));
    memcpy(replacement+1,&relative,4);
    if (!AddBytes(patches,0x41155c,original,replacement,5)) return false;
    gold_shadow_owner=reinterpret_cast<uintptr_t*>(owner);
    return true;
}
bool GoldVisualPatches(std::vector<Patch>& patches) {
    bool detail=false,shadows=false,range=false,ultra=false;
    if (!GoldVisualFlag(L"S7_EXTENDED_DETAIL",detail)
        || !GoldVisualFlag(L"S7_DETAILED_SHADOWS",shadows)
        || !GoldVisualFlag(L"S7_EXTENDED_SHADOW_RANGE",range)
        || !GoldVisualFlag(L"S7_ULTRA_SHADOWS",ultra)) return false;
    // Gold also inlines the resolution setter during world creation. Both
    // operands must agree so starting/loading a world does not reset detail.
    if (detail && (!GoldLodOperand(patches,0x1593ca) || !GoldLodOperand(patches,0x3a71aa))) return false;
    if (shadows || ultra) {
        const unsigned char original[]={0xb8,0,8,0,0};
        const unsigned char replacement[]={0xb8,0,static_cast<unsigned char>(ultra ? 0x20 : 0x10),0,0};
        if (!AddBytes(patches,0x40c95f,original,replacement,5)
            || !AddBytes(patches,0x40db4f,original,replacement,5) || !GoldFilterPatch(patches)) return false;
    }
    if (ultra && !GoldFloatOperand(patches,0x40e85e,0xd8,0x0d,0xb62774,&GoldNearBlend)) return false;
    if (range && (!GoldFloatOperand(patches,0x40e821,0xd9,0x05,0xb62778,&GoldShadowSpan)
        || !GoldFloatOperand(patches,0x40e834,0xd9,0x05,0xb6243c,&GoldShadowRatio)
        || !GoldFloatOperand(patches,0x40e878,0xd9,0x05,0xb62770,&GoldShadowEnd)
        || !GoldFloatOperand(patches,0x40e891,0xd9,0x05,0xb6276c,&GoldShadowFarEnd)
        // Gold reuses the far terminal to build the combined culling volume.
        || !GoldFloatOperand(patches,0x40ea42,0xd9,0x05,0xb6276c,&GoldShadowFarEnd))) return false;
    return true;
}
