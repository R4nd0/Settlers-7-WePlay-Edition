#pragma once
#include "crossplay_event_codec.h"
#include <array>

// Cleanup primitive only. The caller must prove the captured original START
// interval and retire its preparation observers before leaving the host SDK
// session. Active-session calls retain the complete original implementation.
// An absent SDK session has no metadata to update; no game result, Ready state
// or world-start permission is produced by this guard.
namespace CrossplayHistorySdkCleanup {
inline void MetadataEntry();
struct MetadataGuard {
    struct State {DWORD owner=0;uintptr_t backend=0,session=0;bool installed=false;unsigned skips=0;void (__cdecl* report)()=nullptr;};
    static State& Current(){static State s;return s;}
    static constexpr uintptr_t Site=0xed1e33;
    static bool Call(){
        if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(Site),5))return false;
        const auto* at=reinterpret_cast<const unsigned char*>(Site);int32_t delta=0;memcpy(&delta,at+1,4);
        return at[0]==0xe8 && Site+5+delta==reinterpret_cast<uintptr_t>(&MetadataEntry);
    }
    static bool Methods(){
        using CrossplayNativeCodec::Detail::Readable;std::array<unsigned char,30> wrapper{};
        if(!Readable(reinterpret_cast<void*>(0xed1e20),wrapper.size()) || !Readable(reinterpret_cast<void*>(0xed61a0),335))return false;
        memcpy(wrapper.data(),reinterpret_cast<void*>(0xed1e20),wrapper.size());
        const unsigned char original[5]={0xe8,0x68,0x43,0,0};
        if(Current().installed){if(!Call())return false;memcpy(wrapper.data()+19,original,5);}
        else if(memcmp(wrapper.data()+19,original,5))return false;
        uint64_t hash=14695981039346656037ull;for(auto b:wrapper)hash=(hash^b)*1099511628211ull;
        if(hash!=0xd007d4193e479f84ull)return false;
        hash=14695981039346656037ull;const auto* code=reinterpret_cast<const unsigned char*>(0xed61a0);
        for(unsigned i=0;i<335;++i)hash=(hash^code[i])*1099511628211ull;
        const unsigned char getter[10]={0x55,0x8b,0xec,0xa1,0x04,0x6f,0x37,0x01,0x5d,0xc3};
        return hash==0x93cd8d3d7143b7a0ull && Readable(reinterpret_cast<void*>(0xed6c40),sizeof(getter))
            && !memcmp(reinterpret_cast<void*>(0xed6c40),getter,sizeof(getter));
    }
#ifdef S7_CAPTURE_CLEANUP_TESTING
    inline static bool (__cdecl* test)(void*)=nullptr;
#endif
    static bool __fastcall Skip(void* backend){
        struct Error {DWORD value=GetLastError();~Error(){SetLastError(value);}} error;
#ifdef S7_CAPTURE_CLEANUP_TESTING
        if(test)return test(backend);
#endif
        auto& s=Current();using CrossplayNativeCodec::Detail::Readable;
        if(!s.installed || s.owner!=GetCurrentThreadId() || reinterpret_cast<uintptr_t>(backend)!=s.backend || !Methods()
            || !Readable(reinterpret_cast<void*>(0x1376f04),4) || *reinterpret_cast<const uintptr_t*>(0x1376f04)!=s.backend
            || !Readable(backend,0xa50))return false;
        if(*reinterpret_cast<const uintptr_t*>(static_cast<const unsigned char*>(backend)+0xa4c))return false;
        ++s.skips;if(s.report)s.report();return true;
    }
    static bool Install(uint32_t own,void (__cdecl* report)()){
        struct Error {DWORD value=GetLastError();~Error(){SetLastError(value);}} error;auto& s=Current();
        using CrossplayNativeCodec::Detail::Readable;
        if(s.installed || !Methods() || !Readable(reinterpret_cast<void*>(0x1376f04),4))return false;
        const auto backend=*reinterpret_cast<const uintptr_t*>(0x1376f04);
        if(!Readable(reinterpret_cast<void*>(backend),0xa50))return false;
        const auto session=*reinterpret_cast<const uintptr_t*>(backend+0xa4c);
        if(!Readable(reinterpret_cast<void*>(session),0x28)
            || *reinterpret_cast<const uintptr_t*>(session)!=0x113e3ac
            || *reinterpret_cast<const uint32_t*>(session+0x20)!=own || *reinterpret_cast<const uint32_t*>(session+0x24)!=own)return false;
        auto* at=reinterpret_cast<unsigned char*>(Site);unsigned char old[5]{};memcpy(old,at,5);
        unsigned char patch[5]={0xe8};const int32_t delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&MetadataEntry)-(Site+5));memcpy(patch+1,&delta,4);
        DWORD protection=0,unused=0;if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&protection))return false;
        s={GetCurrentThreadId(),backend,session,true,0,report};memcpy(at,patch,5);
        const bool flushed=FlushInstructionCache(GetCurrentProcess(),at,5)!=FALSE;
        const bool restored=VirtualProtect(at,5,protection,&unused)!=FALSE;
        if(!flushed || !restored){DWORD temporary=0;if(!VirtualProtect(at,5,PAGE_EXECUTE_READWRITE,&temporary))__fastfail(7);
            memcpy(at,old,5);const bool f=FlushInstructionCache(GetCurrentProcess(),at,5)!=FALSE;
            const bool p=VirtualProtect(at,5,protection,&unused)!=FALSE;if(!f || !p)__fastfail(7);s={};return false;}
        return true;
    }
};
#ifdef S7_CAPTURE_CLEANUP_TESTING
inline uintptr_t test_forward=0;
#endif
__declspec(naked) inline void MetadataEntry(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    mov ecx,dword ptr [edi+24]
    call MetadataGuard::Skip
    test al,al
    jz forward
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    ret 4
forward:
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
}
#ifdef S7_CAPTURE_CLEANUP_TESTING
__asm {jmp dword ptr [test_forward]}
#else
__asm {push 0xed61a0
    ret}
#endif
}
}
