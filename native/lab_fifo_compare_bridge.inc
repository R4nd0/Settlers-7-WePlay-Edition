__declspec(naked) void CompareNeoFifoTransfersBridge() {
    __asm {
        pushfd
        pushad
        mov edi,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        push dword ptr [edi+20]
        push dword ptr [edi+24]
        call CompareNeoFifoTransfers
        add esp,8
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
