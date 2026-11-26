// Bounded immutable-shader identity cache. Strong references prevent address
// reuse from admitting a different object. Clear explicitly before Reset;
// never release COM objects from a CRT global destructor during DLL teardown.
#pragma once
#include <array>
#include <cstddef>
#include <cstdint>

namespace WaterOn12ShaderCache {
template<class T,std::size_t Capacity> struct Cache {
    static_assert(Capacity&&!(Capacity&(Capacity-1)),"Power-of-two capacity required");
    struct Entry {T* shader=nullptr;bool target=false;};
    std::array<Entry,Capacity> entries{};
    Entry* last=nullptr;
    std::uint64_t hits=0,misses=0,full=0;
    std::size_t count=0;
    Cache()=default;
    Cache(const Cache&)=delete;
    Cache& operator=(const Cache&)=delete;
    std::size_t Start(T* shader) const {
        const auto address=reinterpret_cast<std::uintptr_t>(shader);
        return ((address>>4)^(address>>13))&(Capacity-1);
    }
    bool Lookup(T* shader,bool& target) {
        if(!shader)return false;
        if(last&&last->shader==shader){target=last->target;++hits;return true;}
        const auto start=Start(shader);
        for(std::size_t i=0;i<Capacity;++i) {
            auto& entry=entries[(start+i)&(Capacity-1)];
            if(!entry.shader)break;
            if(entry.shader==shader){last=&entry;target=entry.target;++hits;return true;}
        }
        ++misses;return false;
    }
    bool Store(T* shader,bool target) {
        if(!shader)return false;
        const auto start=Start(shader);
        for(std::size_t i=0;i<Capacity;++i) {
            auto& entry=entries[(start+i)&(Capacity-1)];
            if(entry.shader==shader){last=&entry;return entry.target==target;}
            if(!entry.shader){shader->AddRef();entry={shader,target};last=&entry;++count;return true;}
        }
        ++full;return false; // Caller uses its full verified classification.
    }
    void Clear() {
        last=nullptr;
        for(auto& entry:entries){T* shader=entry.shader;entry={};if(shader)shader->Release();}
        count=0;
    }
};
}
