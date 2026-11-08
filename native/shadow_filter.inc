// Higher map allocations otherwise shrink the native PCF footprint. Restore
// the 2048-map sampling step only for the two distant cascades. The kernel
// radius also controls noise rotation, so changing it would change both.
const float ShadowFilterStep[] = {
    1.f / 2048.f, 1.f / 2048.f, 1.f / 2048.f, 1.f / 2048.f
};
uintptr_t* shadow_filter_owner_slot = nullptr;

__declspec(naked) void SelectShadowFilterStep() {
    __asm {
        pushfd
        push ecx
        lea eax, [ebx+07ch]
        mov ecx, shadow_filter_owner_slot
        cmp [ecx], ebx
        jne finished
        cmp esi, 2
        jb finished
        cmp esi, 3
        ja finished
        mov ecx, [ebx+078h]
        cmp ecx, 8192
        je map8192
        cmp ecx, 4096
        jne finished
        mov ecx, 039800000h // IEEE754 1/4096
        jmp validate_inverse
    map8192:
        mov ecx, 039000000h // IEEE754 1/8192
    validate_inverse:
        cmp [eax], ecx
        jne finished
        cmp [eax+4], ecx
        jne finished
        cmp [eax+8], ecx
        jne finished
        cmp [eax+12], ecx
        jne finished
        mov eax, offset ShadowFilterStep
    finished:
        pop ecx
        popfd
        // Reproduce PUSH 1 beneath the CALL return address, preserving EDX.
        // The following original PUSH EAX receives the selected shader vector.
        push edx
        mov edx, [esp+4]
        mov dword ptr [esp+4], 1
        xchg edx, [esp]
        ret
    }
}

const Patch ShadowFilterPatch = {
    0x911a01, {0x6a,0x01,0x8d,0x43,0x7c}, &SelectShadowFilterStep, false
};

bool PrepareShadowFilter() {
    const unsigned char index[] = {0xf3,0x0f,0x10,0x84,0xb4,0xc8,0,0,0};
    const unsigned char before[] = {0x83,0xc4,0x0c};
    const unsigned char after[] = {
        0x50,0xff,0xb3,0xac,0x19,0,0,0x8b,0x83,0xb0,0x19,0,0,0xff,0xd0
    };
    const auto* at = PatchAddress(ShadowFilterPatch);
    if (memcmp(at, OriginalBytes(ShadowFilterPatch), 5)
        || memcmp(at-0x52, index, sizeof(index))
        || memcmp(at-3, before, sizeof(before))
        || memcmp(at+5, after, sizeof(after))) return false;
    shadow_filter_owner_slot = Native<uintptr_t*>(0x137a030);
    return true;
}
