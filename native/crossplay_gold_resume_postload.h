#pragma once
#include "crossplay_gold_logistics_binding.h"
#include <array>
// Gold's original postload resets CRequestTask, unlike both History editions.
// Preserve only an authenticated saved request at the two original postload
// callers. No native state/wait/Ready/clock/checksum is written by this port.
namespace CrossplayGoldResume {
using GoldLogisticsBinding::ErrorScope;
using GoldLogisticsBinding::Word;
struct Saved {
    uint32_t table=0,id=0,state=0,base=0,cached=0,priority=0,wait=0,group=0,bucket=0,parent=0,flags=0,birth=0,last=0,player=0;
};
inline std::array<Saved,65536> saved{};
inline uint32_t saved_count=0,checkpoint_turn=0,first_pass=0,second_pass=0;
inline DWORD owner=0;
inline bool installed=false,invalid=false,attempted=false,validated=false;
inline bool (*authority)()=nullptr;
inline void (*report)(bool,const char*)=nullptr;
inline uintptr_t predicate_resume=0,scheduler_resume=0;
struct Hook {uintptr_t at=0,target=0;unsigned size=0;unsigned char before[6]{},after[6]{};};
inline std::array<Hook,3> hooks{};
inline bool Known(uint32_t table){return table==0xf4c684 || table==0xf4c74c || table==0xf4c7e4;}
inline bool Read(uintptr_t object,Saved& row){
    uint32_t flags=0,player=0;
    if(!Word(object,row.table) || !Known(row.table) || !Word(object+8,row.id) || !row.id || row.id==UINT32_MAX
        || !Word(object+4,row.state) || !Word(object+0xc,row.base) || !Word(object+0x10,row.cached) || !Word(object+0x14,row.priority)
        || !Word(object+0x18,row.wait) || !Word(object+0x1c,row.group) || !Word(object+0x20,row.bucket) || !Word(object+0x24,row.parent)
        || !Word(object+0x28,flags) || !Word(object+0x2c,row.birth) || !Word(object+0x30,row.last) || !Word(object+0x34,player))return false;
    row.flags=flags&65535u;row.player=player&255u;
    return row.player<25 && row.group<=10 && row.bucket<=10 && row.wait<=INT32_MAX;
}
inline bool Same(const Saved& a,const Saved& b){
    return a.table==b.table && a.id==b.id && a.state==b.state && a.base==b.base && a.cached==b.cached && a.priority==b.priority
        && a.wait==b.wait && a.group==b.group && a.bucket==b.bucket && a.parent==b.parent && a.flags==b.flags
        && a.birth==b.birth && a.last==b.last && a.player==b.player;
}
inline void Reject(const char* reason){invalid=true;if(report)report(false,reason);}
inline void __cdecl Capture(const uint32_t* frame){
    ErrorScope error;
    if(!installed || attempted || GetCurrentThreadId()!=owner)return;
    uint32_t table=0;
    if(!Word(frame[1],table) || table!=0xf2805c)return;
    Saved row;
    if(frame[0]<0x10034 || !Read(frame[0]-0x34,row)
        || static_cast<int32_t>(row.birth)>static_cast<int32_t>(checkpoint_turn)
        || static_cast<int32_t>(row.last)>static_cast<int32_t>(checkpoint_turn))return Reject("saved-request-source-excluded");
    auto& previous=saved[row.id&65535u];
    if(previous.id){if(!Same(previous,row))Reject("saved-request-repeated-source-excluded");return;}
    previous=row;++saved_count;
}
#ifdef S7_GOLD_RESUME_POSTLOAD_TESTING
inline bool (*test_decide)(const uint32_t*)=nullptr;
#endif
inline bool __cdecl Preserve(const uint32_t* frame){
    ErrorScope error;
#ifdef S7_GOLD_RESUME_POSTLOAD_TESTING
    if(test_decide)return test_decide(frame);
#endif
    if(!installed || attempted || GetCurrentThreadId()!=owner || (frame[9]!=0x902dce && frame[9]!=0x902ea6))return false;
    uint32_t table=0;const auto object=frame[6];
    if(!Word(object,table) || table!=0xf4c684)return false;
    Saved row;GoldLogisticsBinding::World world;uint32_t bound=0;
    if(invalid || !authority || !authority() || !Read(object,row) || !GoldLogisticsBinding::CaptureWorld(world)
        || !GoldLogisticsBinding::Task(world,row.id,bound) || bound!=object
        || !saved[row.id&65535u].id || !Same(saved[row.id&65535u],row)
        || (row.state!=0 && row.state!=1 && row.state!=6)){
        Reject("saved-request-postload-identity-or-authority-excluded");return false;
    }
    if(frame[9]==0x902dce)++first_pass;else ++second_pass;
    return true;
}
inline bool Validate(uint32_t expected_main){
    GoldLogisticsBinding::World world;uint32_t begin=0,end=0;
    if(invalid || !saved_count || !authority || !authority() || !GoldLogisticsBinding::CaptureWorld(world)
        || world.main!=expected_main || !Word(world.priority+0x164c,begin) || !Word(world.priority+0x1650,end)
        || begin<0x10000 || begin>end || (end-begin)%8 || (end-begin)/8>65536)return false;
    uint32_t matched=0;
    for(uint32_t at=begin;at<end;at+=8){
        uint32_t id=0,object=0,bound=0;if(!Word(at,id) || !Word(at+4,object))return false;
        if(!object)continue;
        Saved live;
        if(!Read(object,live) || live.id!=id || !GoldLogisticsBinding::Task(world,id,bound) || bound!=object
            || !saved[id&65535u].id || !Same(saved[id&65535u],live))return false;
        ++matched;
    }
    return matched==saved_count;
}
inline void __cdecl BeforeScheduler(const uint32_t* frame){
    ErrorScope error;
    if(!installed || attempted || GetCurrentThreadId()!=owner)return;
    attempted=true;
    try{validated=Validate(frame[10]);}catch(...){validated=false;}
    if(!validated)return Reject("saved-request-postload-full-registry-excluded");
    if(report)report(true,"saved-request-postload-original-state-preserved-before-scheduler");
}
__declspec(naked) inline void SerializerExit(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push edi
    call Capture
    add esp,4
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    pop edi
    pop esi
    pop ebp
    ret 8
}}
__declspec(naked) inline void SchedulerEntry(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push edi
    call BeforeScheduler
    add esp,4
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    push ebp
    mov ebp,esp
    push -1
    jmp dword ptr [scheduler_resume]
}}
__declspec(naked) inline void Predicate(){__asm{
    pushfd
    pushad
    cld
    mov edi,esp
    sub esp,528
    and esp,0xfffffff0
    fxsave [esp]
    push edi
    call Preserve
    add esp,4
    test al,al
    jz original
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    xor al,al
    ret
original:
    fxrstor [esp]
    mov esp,edi
    popad
    popfd
    mov eax,dword ptr [ecx]
    mov edx,dword ptr [eax+0x1c]
    jmp dword ptr [predicate_resume]
}}
inline uint64_t Hash(uintptr_t at,unsigned size){
    if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(at),size))return 0;
    uint64_t value=14695981039346656037ull;
    for(unsigned i=0;i<size;++i)value=(value^reinterpret_cast<const unsigned char*>(at)[i])*1099511628211ull;
    return value;
}
inline bool Methods(){
    return GoldLogisticsBinding::OriginalGuards() && Hash(0x946750,320)==0x18abd9fbaebad71cull
        && Hash(0x901300,1121)==0xa1d923e7d2cc3e42ull && Hash(0x949a90,18)==0x335103c9432c50f2ull
        && Hash(0x902d60,398)==0x4436f1fc0a332341ull && Hash(0xf4c6c8,4)==0x27d7652dc429d973ull
        && Hash(0xf4c6a0,4)==0xd1fce427e570f253ull;
}
inline bool Patch(uintptr_t at,const unsigned char* before,const unsigned char* after,unsigned size){
    if(!CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(at),size) || memcmp(reinterpret_cast<void*>(at),before,size))return false;
    DWORD previous=0,ignored=0;
    if(!VirtualProtect(reinterpret_cast<void*>(at),size,PAGE_EXECUTE_READWRITE,&previous))return false;
    memcpy(reinterpret_cast<void*>(at),after,size);
    const bool flushed=FlushInstructionCache(GetCurrentProcess(),reinterpret_cast<void*>(at),size)!=FALSE;
    const bool restored=VirtualProtect(reinterpret_cast<void*>(at),size,previous,&ignored)!=FALSE;
    if(!flushed || !restored || memcmp(reinterpret_cast<void*>(at),after,size))__fastfail(7);
    return true;
}
inline bool Install(Crossplay::Edition edition,DWORD thread,uint32_t turn,bool (*authorized)(),void (*notice)(bool,const char*)){
    ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    if(edition!=Crossplay::Edition::Gold)return edition==Crossplay::Edition::UbisoftHistory || edition==Crossplay::Edition::SteamHistory;
    if(installed || thread!=GetCurrentThreadId() || !authorized || !notice || !Methods())return false;
    hooks={{{0x94688a,reinterpret_cast<uintptr_t>(&SerializerExit),6,{},{}},
        {0x901300,reinterpret_cast<uintptr_t>(&SchedulerEntry),5,{},{}},
        {0x949a90,reinterpret_cast<uintptr_t>(&Predicate),5,{},{}}}};
    for(auto& hook:hooks){
        memcpy(hook.before,reinterpret_cast<void*>(hook.at),hook.size);hook.after[0]=0xe9;
        const auto displacement=static_cast<int32_t>(hook.target-hook.at-5);memcpy(hook.after+1,&displacement,4);
        if(hook.size==6)hook.after[5]=0x90;
    }
    owner=thread;checkpoint_turn=turn;authority=authorized;report=notice;predicate_resume=0x949a95;scheduler_resume=0x901305;
    for(size_t i=0;i<3;++i)if(!Patch(hooks[i].at,hooks[i].before,hooks[i].after,hooks[i].size)){
        while(i){--i;if(!Patch(hooks[i].at,hooks[i].after,hooks[i].before,hooks[i].size))__fastfail(7);}
        return false;
    }
    installed=true;return true;
}
inline bool OwnsHooks(){
    for(const auto& hook:hooks)if(!hook.at || hook.size<5 || hook.size>6
        || !CrossplayNativeCodec::Detail::Readable(reinterpret_cast<void*>(hook.at),hook.size)
        || memcmp(reinterpret_cast<void*>(hook.at),hook.after,hook.size))return false;
    return true;
}
// Call on the same owner after native world/session teardown. Never restore over
// a foreign hook. A later resume must install afresh and recapture its source.
inline bool Release(){
    ErrorScope error;CrossplayNativeCodec::LoadProgressDetail::FpScope fp;
    if(!installed || owner!=GetCurrentThreadId() || !OwnsHooks())return false;
    for(size_t i=0;i<hooks.size();++i)if(!Patch(hooks[i].at,hooks[i].after,hooks[i].before,hooks[i].size)){
        while(i){--i;if(!Patch(hooks[i].at,hooks[i].before,hooks[i].after,hooks[i].size))__fastfail(7);}
        return false;
    }
    installed=false;owner=0;authority=nullptr;report=nullptr;
    invalid=attempted=validated=false;checkpoint_turn=saved_count=first_pass=second_pass=0;
    memset(saved.data(),0,sizeof(saved));hooks={};predicate_resume=scheduler_resume=0;
    return true;
}
}
