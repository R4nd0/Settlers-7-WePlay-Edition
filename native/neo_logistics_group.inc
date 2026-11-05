// Deliberate UI/AI group reorders have no effect in this mode. Native default
// initialization still runs; explicit individual commands use the task bridge.
uintptr_t neo_group_resume=0;
uint32_t __cdecl NeoSuppressGroup(const uint32_t* frame) {
    const auto error=GetLastError();uint32_t skip=0;
    __try {
        uint32_t turn=0;const auto priority=NeoFifoOwner()?NeoFifoWorld(turn):0;
        if(priority && priority==frame[6]) {
            NeoFifoEpoch(priority,turn);
            const auto caller=NeoLogisticsCaller(frame[9]);
            skip=neo_fifo_enabled && neo_fifo_fresh && (caller==0xa13f39 || caller==0x7bc59f)?1:0;
        }
    } __except(EXCEPTION_EXECUTE_HANDLER) {++neo_fifo_intent_errors;}
    SetLastError(error);return skip;
}
__declspec(naked) void NeoGroupBridge() {
    __asm {
        pushfd
        pushad
        mov edi,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        push edi
        call NeoSuppressGroup
        add esp,4
        test eax,eax
        jnz suppressed
        fxrstor [esp]
        mov esp,edi
        popad
        popfd
        push ebp
        mov ebp,esp
        push -1
        jmp dword ptr [neo_group_resume]
suppressed:
        fxrstor [esp]
        mov esp,edi
        popad
        popfd
        ret 8
    }
}
