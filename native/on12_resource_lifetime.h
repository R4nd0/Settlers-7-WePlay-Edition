#pragma once
#include <windows.h>
#include <cstdint>
#include <limits>
#include <unordered_map>

namespace On12Graphics {
// A pool slot number survives reconstruction. Only an observed construction
// grants a token; first sight of an address never invents a prior lifetime.
class ResourceLifetimes {
public:
    explicit ResourceLifetimes(size_t capacity=65536):capacity_(capacity){}
    ResourceLifetimes(const ResourceLifetimes&)=delete;
    ResourceLifetimes& operator=(const ResourceLifetimes&)=delete;
    std::uint64_t Constructed(std::uintptr_t address)noexcept {
        if(!address)return 0;Exclusive lock(mutex_);
        if(failed_||next_==(std::numeric_limits<std::uint64_t>::max)()){failed_=true;return 0;}
        try {
            auto found=entries_.find(address);
            if(found==entries_.end()){
                if(entries_.size()>=capacity_){failed_=true;return 0;}
                found=entries_.emplace(address,0).first;
            }
            return found->second=++next_;
        }catch(...){failed_=true;return 0;}
    }
    std::uint64_t Find(std::uintptr_t address)const noexcept {
        Shared lock(mutex_);if(failed_)return 0;
        const auto found=entries_.find(address);return found==entries_.end()?0:found->second;
    }
    // Invalidating one lifetime never removes a later construction at that
    // address. This also permits delayed destruction notifications.
    bool Retired(std::uintptr_t address,std::uint64_t token)noexcept {
        Exclusive lock(mutex_);const auto found=entries_.find(address);
        if(failed_||!token||found==entries_.end()||found->second!=token)return false;
        found->second=0;return true;
    }
    bool Healthy()const noexcept {Shared lock(mutex_);return !failed_;}
    std::uint64_t Constructions()const noexcept {Shared lock(mutex_);return next_;}
    void Invalidate()noexcept {Exclusive lock(mutex_);failed_=true;}
private:
    struct Exclusive {SRWLOCK& value;explicit Exclusive(SRWLOCK& v):value(v){AcquireSRWLockExclusive(&value);}~Exclusive(){ReleaseSRWLockExclusive(&value);}};
    struct Shared {SRWLOCK& value;explicit Shared(SRWLOCK& v):value(v){AcquireSRWLockShared(&value);}~Shared(){ReleaseSRWLockShared(&value);}};
    mutable SRWLOCK mutex_=SRWLOCK_INIT;
    size_t capacity_;std::uint64_t next_=0;bool failed_=false;
    std::unordered_map<std::uintptr_t,std::uint64_t> entries_;
};
}
