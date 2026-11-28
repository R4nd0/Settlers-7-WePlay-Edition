#pragma once
#include "on12_resource_lifetime.h"
#include "water_on12_identity.h"
#include <array>
#include <initializer_list>

namespace On12Graphics {
// Exactly one complete CALL is changed inside its aligned eight-byte block.
// Adjacent instruction bytes stay unchanged; no instruction is relocated and
// both the old and new targets remain mapped for the entire process lifetime.
inline bool ExchangePoolCall(void* address,std::uint64_t before,std::uint64_t after)noexcept {
    if((reinterpret_cast<std::uintptr_t>(address)&7)||!WaterOn12Identity::Range(address,8))return false;
    DWORD previous=0,ignored=0;if(!VirtualProtect(address,8,PAGE_EXECUTE_READWRITE,&previous))return false;
    const auto observed=InterlockedCompareExchange64(static_cast<volatile LONG64*>(address),static_cast<LONG64>(after),static_cast<LONG64>(before));
    const bool changed=static_cast<std::uint64_t>(observed)==before;
    const bool flushed=!changed||FlushInstructionCache(GetCurrentProcess(),address,8)!=FALSE;
    const bool restored=VirtualProtect(address,8,previous,&ignored)!=FALSE;
    return changed&&flushed&&restored;
}

class DoodadLifetimes {
public:
    DoodadLifetimes()=default;
    DoodadLifetimes(const DoodadLifetimes&)=delete;
    // The caller retains this object for process lifetime. The containing DLL
    // is pinned before the callback can become reachable.
    bool Install()noexcept {
        static_assert(sizeof(void*)==4,"Exact original x86 resource-pool ABI");
        if(installed_)return installed_==this&&Healthy();
        try {
            const auto module=GetModuleHandleW(nullptr);
            if(!WaterOn12Identity::Module(module,WaterOn12Identity::Game))return false;
            base_=reinterpret_cast<std::uintptr_t>(module);
            if(!Code(0x104350,133,"28ec15593b6667f4c5e0357f601d5d9f31558f06e3de130673be8f0a666be8e2",{})
                ||!Code(0x1044d0,259,"06748770345442e34bde91f46cbf43a65c96ec1b9e217aa855f7ec3c07e5e949",{6,0x17,0x94,0x9b,0xa5,0xac,0xd6,0xdd}))return false;
            auto* block=reinterpret_cast<unsigned char*>(base_+0x104358);
            const std::array<unsigned char,8> expected{{0x8b,0x4e,0x20,0xe8,0x70,1,0,0}};
            std::uint64_t before=0,after=0;std::array<unsigned char,8> changed=expected;
            const auto relative=static_cast<DWORD>(reinterpret_cast<std::uintptr_t>(&Construct)-(base_+0x104360));
            std::memcpy(changed.data()+4,&relative,4);std::memcpy(&before,expected.data(),8);std::memcpy(&after,changed.data(),8);
            if(std::memcmp(block,expected.data(),8))return false;
            original_=reinterpret_cast<Allocate>(base_+0x1044d0);HMODULE pinned=nullptr;
            if(!GetModuleHandleExW(GET_MODULE_HANDLE_EX_FLAG_FROM_ADDRESS|GET_MODULE_HANDLE_EX_FLAG_PIN,reinterpret_cast<const wchar_t*>(&Construct),&pinned))return false;
            // Publication precedes patching; even a failed protection restore
            // leaves a valid, pinned passthrough callback rather than dangling code.
            if(InterlockedCompareExchangePointer(reinterpret_cast<void* volatile*>(&installed_),this,nullptr))return false;
            if(!ExchangePoolCall(block,before,after)){registry_.Invalidate();return false;}
            block_=block;after_=after;return true;
        }catch(...){registry_.Invalidate();return false;}
    }
    bool Healthy()const noexcept {
        if(installed_!=this||!block_||!registry_.Healthy())return false;
        std::uint64_t value=0;SIZE_T received=0;
        return ReadProcessMemory(GetCurrentProcess(),block_,&value,8,&received)&&received==8&&value==after_;
    }
    std::uint64_t Constructions()const noexcept {return registry_.Constructions();}
    std::uint64_t Resolve(std::uintptr_t matrix,const float* expected_world)const noexcept {
        if(!expected_world||matrix<0x10128||matrix>=0xffff0000||!Healthy())return 0;
        const auto object=matrix-0x128;const auto token=registry_.Find(object);if(!token)return 0;
        std::array<unsigned char,0x198> value{};SIZE_T count=0;
        if(!ReadProcessMemory(GetCurrentProcess(),reinterpret_cast<const void*>(object),value.data(),value.size(),&count)||count!=value.size())return 0;
        DWORD primary=0,secondary=0;std::memcpy(&primary,value.data(),4);std::memcpy(&secondary,value.data()+4,4);
        if(primary!=base_+0xd31d08||secondary!=base_+0xd31cfc||value[0x194]!=1
            ||std::memcmp(value.data()+0x128,expected_world,64)||registry_.Find(object)!=token)return 0;
        return token;
    }
private:
    using Allocate=void* (__thiscall*)(void*);
    bool Code(DWORD rva,size_t bytes,const char* expected,std::initializer_list<size_t> relocations)const {
        if(bytes>512||!WaterOn12Identity::Belongs(reinterpret_cast<const void*>(base_+rva),reinterpret_cast<HMODULE>(base_))
            ||!WaterOn12Identity::Range(reinterpret_cast<const void*>(base_+rva),bytes))return false;
        std::array<unsigned char,512> copy{};std::memcpy(copy.data(),reinterpret_cast<const void*>(base_+rva),bytes);
        for(const auto offset:relocations){if(offset+4>bytes)return false;DWORD address=0;std::memcpy(&address,copy.data()+offset,4);address=address-static_cast<DWORD>(base_)+0x400000;std::memcpy(copy.data()+offset,&address,4);}
        std::string actual;return WaterOn12Identity::Hash(copy.data(),static_cast<ULONG>(bytes),actual)&&actual==expected;
    }
    static void* __fastcall Construct(void* pool,void*) {
        auto* instance=installed_;void* object=instance->original_(pool);
        // Capture and restore the original function's FP/last-error result.
        WaterOn12Identity::Guard guard;
        instance->registry_.Constructed(reinterpret_cast<std::uintptr_t>(object));return object;
    }
    inline static DoodadLifetimes* installed_=nullptr;
    ResourceLifetimes registry_;Allocate original_=nullptr;
    std::uintptr_t base_=0;unsigned char* block_=nullptr;std::uint64_t after_=0;
};
}
