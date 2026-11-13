#pragma once
#include <windows.h>
#include <stdint.h>

// Passive forwarding primitive, excluded from ordinary adapters. This header
// installs nothing. A separately guarded installer must bind the exact native
// target and validate object ownership before inspecting any borrowed path.
// Observation is a request, never proof of a successful resource read.
namespace CrossplayResourceOpenBridge::Detail {
using Callback=void (__stdcall*)(void*,const void*,uint32_t);
inline uintptr_t original=0;
inline Callback callback=nullptr;
inline volatile LONG callback_failed=0;

inline void __stdcall Record(void* collection,const void* path,uint32_t mode) {
    // No native data is dereferenced by the bridge itself. An observer fault
    // cannot suppress or replace the original file-system call.
    __try {if(callback)callback(collection,path,mode);}
    __except(EXCEPTION_EXECUTE_HANDLER) {InterlockedExchange(&callback_failed,1);}
}

// Original CFileSystemCollection::Open: ECX=this, path/mode on the stack, ret8.
// Restore its exact incoming machine state before tail forwarding, including
// x87/SSE/MXCSR and LastError. The original method supplies the final result,
// flags, error, exception behavior and stack cleanup.
__declspec(naked) inline void Forward() {
    __asm {
        pushfd
        pushad
        mov ebp,esp
        sub esp,544
        and esp,0xfffffff0
        fxsave [esp]
        call GetLastError
        mov dword ptr [esp+512],eax
        push dword ptr [ebp+44]
        push dword ptr [ebp+40]
        push dword ptr [ebp+24]
        call Record
        push dword ptr [esp+512]
        call SetLastError
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        jmp dword ptr [original]
    }
}
}
