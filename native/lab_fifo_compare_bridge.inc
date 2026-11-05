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
        mov dword ptr [edi+28],eax
        fxrstor [esp]
        mov esp,edi
        popad
        popfd
        ret
    }
}
