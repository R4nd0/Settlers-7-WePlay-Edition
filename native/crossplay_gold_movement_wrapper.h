#pragma once
#include "crossplay_gold_movement_precision.h"

// Original movement owner for the separately guarded path/precision port.
// The native method runs once with its actual arguments and original return.
namespace GoldMovementWrapper {
using Update=uint32_t(__thiscall*)(void*,uint32_t,void*);
inline Update original=nullptr;
inline bool installed=false,installing=false;
inline DWORD owner=0;
inline constexpr uintptr_t Target=0x70c760,SecondAtan=0x70ca7b,FirstAtan=0x70c69b;
inline constexpr unsigned char Prefix[]={0x55,0x8b,0xec,0x83,0xe4,0xf8};
inline uint32_t __fastcall Entry(void* self,void*,uint32_t duration,void* output){
    const DWORD incoming=GetLastError();GoldMovementPrecision::MovementScope precision{};
    GoldMovementPrecision::BeginMovement(self,precision);
    SetLastError(incoming);uint32_t result=0;DWORD returned=incoming;
    __try {result=original(self,duration,output);returned=GetLastError();}
    __finally {GoldMovementPrecision::EndMovement(precision);}
    SetLastError(returned);return result;
}
inline bool Immutable();
inline bool Call(uintptr_t at){
    using namespace GoldMovementPrecision;
    if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<const void*>(at),5)
        || *reinterpret_cast<const unsigned char*>(at)!=0xe8)return false;
    int32_t delta=0;memcpy(&delta,reinterpret_cast<const void*>(at+1),4);
    const auto target=static_cast<uintptr_t>(at+5+delta);
    return target==0xdc5418 || ((installing || installed) && original_path
        && target==reinterpret_cast<uintptr_t>(&Atan2));
}
inline bool OwnedBody(){
    using namespace CrossplayNativeCodec;
    if((!installing && !installed) || !original || owner!=GetCurrentThreadId() || !Immutable()
        || !Detail::Readable(reinterpret_cast<const void*>(Target),1938) || !Call(FirstAtan) || !Call(SecondAtan))return false;
    const auto* native=reinterpret_cast<const unsigned char*>(Target);int32_t delta=0;memcpy(&delta,native+1,4);
    if(native[0]!=0xe9 || native[5]!=0x90 || static_cast<uintptr_t>(Target+5+delta)!=reinterpret_cast<uintptr_t>(&Entry))return false;
    unsigned char body[1938]{};memcpy(body,native,sizeof(body));memcpy(body,Prefix,sizeof(Prefix));
    constexpr auto offset=SecondAtan-Target;body[offset]=0xe8;
    delta=static_cast<int32_t>(0xdc5418-(SecondAtan+5));memcpy(body+offset+1,&delta,4);
    uint64_t hash=14695981039346656037ull;
    for(auto byte:body)hash=(hash^byte)*1099511628211ull;
    return hash==0x10af38e858644708ull;
}
inline bool Install(DWORD thread,bool (*ready)()){
    using namespace CrossplayNativeCodec;
    struct ErrorScope {DWORD value=GetLastError();~ErrorScope(){SetLastError(value);}} error;
    LoadProgressDetail::FpScope fp;
    if(installed || installing || original || owner || !ready || thread!=GetCurrentThreadId()
        || !Immutable() || GoldMovementPrecision::Hash(Target,1938)!=0x10af38e858644708ull
        || !GoldMovementPrecision::Guard() || !Call(FirstAtan) || !Call(SecondAtan))return false;
    if(memcmp(reinterpret_cast<const void*>(Target),Prefix,sizeof(Prefix)))return false;
    auto* replay=static_cast<unsigned char*>(VirtualAlloc(nullptr,32,MEM_RESERVE|MEM_COMMIT,PAGE_READWRITE));
    if(!replay)return false;
    memcpy(replay,Prefix,6);replay[6]=0xe9;
    int32_t delta=static_cast<int32_t>(Target+6-(reinterpret_cast<uintptr_t>(replay)+11));memcpy(replay+7,&delta,4);
    DWORD protection=0;
    if(!VirtualProtect(replay,32,PAGE_EXECUTE_READ,&protection) || !FlushInstructionCache(GetCurrentProcess(),replay,11)){
        VirtualFree(replay,0,MEM_RELEASE);return false;
    }
    auto* site=reinterpret_cast<unsigned char*>(Target);DWORD previous=0;
    if(!VirtualProtect(site,6,PAGE_EXECUTE_READWRITE,&previous)){VirtualFree(replay,0,MEM_RELEASE);return false;}
    original=reinterpret_cast<Update>(replay);owner=thread;installing=true;
    unsigned char jump[]={0xe9,0,0,0,0,0x90};delta=static_cast<int32_t>(reinterpret_cast<uintptr_t>(&Entry)-(Target+5));memcpy(jump+1,&delta,4);memcpy(site,jump,6);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),site,6)!=FALSE;DWORD ignored=0;
    const bool protected_again=VirtualProtect(site,6,previous,&ignored)!=FALSE;
    if(!flushed || !protected_again || !GoldMovementPrecision::Install(thread,ready,reinterpret_cast<uintptr_t>(&Entry),&OwnedBody)){
        DWORD temporary=0;if(!VirtualProtect(site,6,PAGE_EXECUTE_READWRITE,&temporary))__fastfail(7);
        memcpy(site,Prefix,6);
        const bool rolled=FlushInstructionCache(GetCurrentProcess(),site,6)!=FALSE;
        const bool restored=VirtualProtect(site,6,previous,&ignored)!=FALSE;
        if(!rolled || !restored)__fastfail(7);
        original=nullptr;owner=0;installing=false;VirtualFree(replay,0,MEM_RELEASE);return false;
    }
    installed=true;installing=false;return true;
}
}
#include "crossplay_gold_movement_wrapper_pins.h"
