// Entry observer: preserve the native entry state, replay the five guarded
// prologue bytes, then resume the original function. No native call is skipped.
uintptr_t lab_priority_task_resume = 0, lab_priority_group_resume = 0;
__declspec(naked) void LabPriorityTaskBridge() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push 0
        push edi
        call ObserveLabPriorityEntry
        add esp, 8
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        push ebp
        mov ebp, esp
        push -1
        jmp dword ptr [lab_priority_task_resume]
    }
}
__declspec(naked) void LabPriorityGroupBridge() {
    __asm {
        pushfd
        pushad
        mov edi, esp
        sub esp, 528
        and esp, 0xfffffff0
        fxsave [esp]
        push 1
        push edi
        call ObserveLabPriorityEntry
        add esp, 8
        fxrstor [esp]
        mov esp, edi
        popad
        popfd
        push ebp
        mov ebp, esp
        push -1
        jmp dword ptr [lab_priority_group_resume]
    }
}
