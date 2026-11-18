#pragma once
#include <windows.h>
#include <cstdint>
#include <cstring>

namespace neo_consumer_sort {

struct HistorySites {
    uintptr_t sort, compare, compare_return, recurse_left, recurse_right, preselection;
};

inline constexpr HistorySites Ubisoft{0x7a28e0,0x78f380,0x7a2989,0x7a2939,0x7a2956,0x78ff2a};
inline constexpr HistorySites Steam{0x7a26b0,0x78f150,0x7a2759,0x7a2709,0x7a2726,0x78fcfa};

inline uintptr_t Address(uintptr_t base,uintptr_t address) {
    return base+address-0x400000;
}

inline bool CallAt(uintptr_t base,uintptr_t return_address,uintptr_t target) {
    const auto* call=reinterpret_cast<const unsigned char*>(Address(base,return_address)-5);
    int32_t displacement=0;
    memcpy(&displacement,call+1,sizeof(displacement));
    return *call==0xe8 && static_cast<int64_t>(return_address)+displacement==static_cast<int64_t>(target);
}

struct Stack {
    ULONG_PTR low=0,high=0;
    bool Read(uintptr_t frame,size_t fields) const {
        return frame>=low && frame<=high && high-frame>=fields*sizeof(uintptr_t)
            && frame%alignof(uintptr_t)==0;
    }
};

inline bool History(const HistorySites& sites,uintptr_t base,uintptr_t caller,
                    uintptr_t frame,uintptr_t list,const Stack& stack) {
    if(caller!=Address(base,sites.compare_return))return false;
    for(unsigned depth=0;depth<32 && stack.Read(frame,2);++depth) {
        const auto* words=reinterpret_cast<const uintptr_t*>(frame);
        const uintptr_t parent=words[0],from=words[1];
        if(from==Address(base,sites.preselection))
            return stack.Read(parent,2) && parent>frame && parent>=0x100 && list==parent-0x100
                && CallAt(base,sites.preselection,sites.sort) && CallAt(base,sites.compare_return,sites.compare);
        if(parent!=frame+0x30)return false;
        if(from==Address(base,sites.recurse_left)) {
            if(!CallAt(base,sites.recurse_left,sites.sort))return false;
        } else if(from!=Address(base,sites.recurse_right) || !CallAt(base,sites.recurse_right,sites.sort)) {
            return false;
        }
        frame=parent;
    }
    return false;
}

inline bool History(bool steam,uintptr_t base,uintptr_t caller,uintptr_t frame,uintptr_t list) {
    const auto& sites=steam?Steam:Ubisoft;
    if(caller!=Address(base,sites.compare_return))return false;
    Stack stack{};
    GetCurrentThreadStackLimits(&stack.low,&stack.high);
    return History(sites,base,caller,frame,list,stack);
}

inline bool Gold(uintptr_t base,uintptr_t bridge,uintptr_t wrapper,const Stack& stack) {
    constexpr uintptr_t sort=0x930e80,merge=0x93b5b0,compare=0x91ad00;
    if(!stack.Read(bridge,3))return false;
    const auto* call=reinterpret_cast<const uintptr_t*>(bridge);
    if(call[2]!=Address(base,0x93b5f3) || call[0]!=bridge+0x24)return false;
    const uintptr_t merge_frame=call[0];
    if(!stack.Read(merge_frame,5) || wrapper!=merge_frame+0x10)return false;
    const auto* merge_words=reinterpret_cast<const uintptr_t*>(merge_frame);
    const uintptr_t sort_frame=merge_words[0];
    if(merge_words[1]<base)return false;
    const uintptr_t merge_return=merge_words[1]-base+0x400000;
    if(sort_frame!=merge_frame+0x17c || !stack.Read(sort_frame,4)
       || (merge_return!=0x930f62 && merge_return!=0x930fb2 && merge_return!=0x93102e))return false;
    const auto* sort_words=reinterpret_cast<const uintptr_t*>(sort_frame);
    return sort_words[1]==Address(base,0x91b536) && sort_words[2]==sort_frame+0xd0 && sort_words[3]==merge_words[4]
        && CallAt(base,0x91b536,sort) && CallAt(base,merge_return,merge) && CallAt(base,0x93b5f3,compare);
}

}
