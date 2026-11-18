__declspec(naked) void CompareNeoFifoTransfersBridge() {
    __asm {
        pushfd
        pushad
        mov edi,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
#ifdef S7_NEO_LOGISTICS
        push dword ptr [edi+16]
        push dword ptr [edi+36]
        push dword ptr [edi+8]
#endif
        push dword ptr [edi+20]
        push dword ptr [edi+24]
        call CompareNeoFifoTransfers
#ifdef S7_NEO_LOGISTICS
        add esp,20
#else
        add esp,8
#endif
#ifdef S7_NEO_LOGISTICS
        cmp eax,2
        je original_compare
#endif
        mov dword ptr [edi+28],eax
        fxrstor [esp]
        mov esp,edi
        popad
        popfd
        ret
#ifdef S7_NEO_LOGISTICS
original_compare:
        fxrstor [esp]
        mov esp,edi
        popad
        popfd
        mov eax,dword ptr [neo_compare_global]
        mov eax,dword ptr [eax]
        jmp dword ptr [neo_compare_resume]
#endif
    }
}
