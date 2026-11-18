#pragma once

#include "crossplay_shared_engine_mode.h"
#include "workshop_angle_values.h"

namespace GoldWorkshopAngles {
constexpr uintptr_t Constructor=0xb0a0e0,ConstructorCall=0xb0d78f,WorkshopVtable=0xf5eccc;
constexpr size_t AnglesOffset=0x64;
inline uintptr_t original=Constructor;
inline DWORD owner=0;
inline bool (*ready)()=nullptr;

inline bool Guard() {
    struct Region {uintptr_t address; unsigned size; uint64_t hash;};
    static constexpr Region regions[]={
        {0xb09a10,1743,0xddb6520f5df8ccacull},
        {Constructor,389,0xccfd7e868b991773ull},
        {0xb0c600,1258,0xdc2a9f3a86c56544ull},
        {ConstructorCall,5,0x73135dc39e514c2eull},
        {0xf23248,4,0x4c15547f9ce6a6ffull},
        {0xf372f4,4,0x4a62557f9b751432ull},
        {0xf465c4,4,0x863d6adce3b1a68aull},
        {WorkshopVtable,60,0xcfe716092b46d023ull},
        {0xf62264,4,0x4ce1bb7f9d94bc68ull},
        {0xf62360,8,0xfdebac349c709ad3ull},
        {0xf62460,8,0xa8031c3227732f3bull},
        {0xf62510,8,0xfe218c349c9df3e3ull},
        {0xf62688,4,0x4d554b7f9df6eea6ull},
        {0xf62790,4,0x4a62547f9b75127full},
        {0xf627e4,4,0x4b2ebc7f9c23299bull},
        {0xf62a50,8,0x9e2dad81f1449b0full},
        {0xf62a58,4,0x22d4167e63d996b1ull},
        {0xf6df14,28,0x4987beb94f086d57ull},
        {0xf90488,28,0x5756caacafe7b06cull},
        {0xf90828,28,0x249ee52b599117c1ull},
        {0xf90b04,20,0x5c6ee2dcde8490dcull},
        {0xf90b18,16,0xaab991e276865bb1ull},
        {0xf90b28,16,0xe554297a6e6210d2ull},
        {0xf90b3c,28,0xec1adcdde5265227ull},
        {0x10ace9c,4,0xbe0c8c9099715296ull},
    };
    for(const auto& region:regions)
        if(SharedEngineMode::Hash(region.address,region.size)!=region.hash)return false;
    return true;
}

inline void __cdecl Initialize(void* object) {
    const DWORD error=GetLastError();
    constexpr auto bytes=sizeof(WorkshopAngleValues::Bits);
    if(owner==GetCurrentThreadId() && ready && ready()
        && CrossplayNativeCodec::Detail::Readable(object,AnglesOffset+bytes)
        && *static_cast<const uintptr_t*>(object)==WorkshopVtable)
        memcpy(static_cast<unsigned char*>(object)+AnglesOffset,WorkshopAngleValues::Bits,bytes);
    SetLastError(error);
}

__declspec(naked) inline void Construct() {
    __asm {
        push dword ptr [esp+4]
        call dword ptr [original]
        pushfd
        pushad
        mov ebp,esp
        sub esp,528
        and esp,0xfffffff0
        fxsave [esp]
        push dword ptr [ebp+28]
        call Initialize
        add esp,4
        fxrstor [esp]
        mov esp,ebp
        popad
        popfd
        ret 4
    }
}

inline bool Install(DWORD thread,bool (*predicate)()) {
    struct ErrorScope {
        DWORD error=GetLastError();
        ~ErrorScope(){SetLastError(error);}
    } saved_error;
    if(owner || thread!=GetCurrentThreadId() || !predicate || original!=Constructor || !Guard())return false;
    auto* site=reinterpret_cast<unsigned char*>(ConstructorCall);
    unsigned char before[5]{};
    memcpy(before,site,sizeof(before));
    int32_t delta=0;
    memcpy(&delta,before+1,sizeof(delta));
    if(before[0]!=0xe8 || ConstructorCall+sizeof(before)+delta!=original)return false;
    unsigned char patch[5]={0xe8};
    delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&Construct)-(ConstructorCall+sizeof(patch)));
    memcpy(patch+1,&delta,sizeof(delta));
    DWORD old=0;
    if(!VirtualProtect(site,sizeof(patch),PAGE_EXECUTE_READWRITE,&old))return false;
    memcpy(site,patch,sizeof(patch));
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,sizeof(patch))!=0;
    DWORD unused=0;
    const bool protected_again=VirtualProtect(site,sizeof(patch),old,&unused)!=0;
    if(!flushed || !protected_again){
        if(!VirtualProtect(site,sizeof(before),PAGE_EXECUTE_READWRITE,&unused))__fastfail(7);
        memcpy(site,before,sizeof(before));
        const bool rolled=FlushInstructionCache(GetCurrentProcess(),site,sizeof(before))!=0;
        const bool restored=VirtualProtect(site,sizeof(before),old,&unused)!=0;
        if(!rolled || !restored)__fastfail(7);
        return false;
    }
    owner=thread;
    ready=predicate;
    return true;
}
}
