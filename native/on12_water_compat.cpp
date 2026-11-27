#include "pch.h"
#include "on12_water_compat.h"
#include "water_on12_identity.h"
#include <atomic>
namespace On12WaterCompat {
std::atomic<std::uint32_t> status{0},pairs{0},zeros{0},conversions{0},cameraLow{0},cameraHigh{0},cameraChanges{0},sequence{0};
std::atomic<std::uint32_t> gateSteps{0};
std::atomic_flag cameraLock=ATOMIC_FLAG_INIT;
std::uint64_t lastCamera=0;
bool Eligible(){
    static const bool eligible=[](){
        const DWORD error=GetLastError();PROCESS_MITIGATION_CHILD_PROCESS_POLICY policy{};
        const bool image=WaterOn12Identity::Module(GetModuleHandleW(nullptr),WaterOn12Identity::Game);
        const bool queried=GetProcessMitigationPolicy(GetCurrentProcess(),ProcessChildProcessPolicy,&policy,sizeof(policy))!=FALSE;
        gateSteps.store((image?1u:0u)|(queried?2u:0u)|(policy.NoChildProcessCreation?4u:0u),std::memory_order_relaxed);
        const bool value=image&&queried&&policy.NoChildProcessCreation;
        SetLastError(error);if(value)status.fetch_or(1,std::memory_order_relaxed);return value;
    }();return eligible;
}
bool Matches(const void* data,std::size_t bytes,std::size_t expected,const char* pin){
    if(bytes!=expected||!Eligible())return false;std::string hash;
    return WaterOn12Identity::Hash(data,bytes,hash)&&hash==pin;
}
bool Vertex(const void* data,std::size_t bytes){
    const bool value=Matches(data,bytes,2592,"f190d66ee1ae43637341a76e5b644cc63bf3c2a5b5c94ea9286764e13f1c16c0")
        ||Matches(data,bytes,2044,"f4f7f42cc6d18d57c58d1d86dfecedf93a9e4204c7b90b11f7030127f1ffc391");
    if(value)status.fetch_or(4,std::memory_order_relaxed);return value;
}
bool Pixel(const void* data,std::size_t bytes){
    const bool value=Matches(data,bytes,3740,"dd65ba0b9d79c7c4d9ad9904404388230556b194eedb8cbdfbd4fa99f44eee5b")
        ||Matches(data,bytes,3068,"b3a2fd86f7a05faa87acfb21687e9e45db664371d5b94e4d6e8230b02079f06b");
    if(value)status.fetch_or(8,std::memory_order_relaxed);return value;
}
void DeviceReady(){Eligible();status.fetch_or(2,std::memory_order_relaxed);}
void Converted(){conversions.fetch_add(1,std::memory_order_relaxed);}
void Observe(bool enabled,const std::uint32_t* scattering,const std::uint32_t* camera){
    if(!enabled)return;const auto count=pairs.fetch_add(1,std::memory_order_relaxed)+1;
    bool zero=true;for(unsigned i=0;i<32;++i)if(scattering[i]&0x7fffffffu)zero=false;
    if(zero)zeros.fetch_add(1,std::memory_order_relaxed);
    if(count%120||cameraLock.test_and_set(std::memory_order_acquire))return;
    std::uint64_t hash=14695981039346656037ull;const auto* bytes=reinterpret_cast<const unsigned char*>(camera);
    for(unsigned i=0;i<96;++i){hash^=bytes[i];hash*=1099511628211ull;}
    sequence.fetch_add(1,std::memory_order_acq_rel);
    if(lastCamera&&lastCamera!=hash)cameraChanges.fetch_add(1,std::memory_order_relaxed);lastCamera=hash;
    cameraLow.store(static_cast<std::uint32_t>(hash),std::memory_order_relaxed);cameraHigh.store(static_cast<std::uint32_t>(hash>>32),std::memory_order_relaxed);
    sequence.fetch_add(1,std::memory_order_release);cameraLock.clear(std::memory_order_release);
}
}
extern "C" DWORD WINAPI WePlayOn12WaterQuery(void* opaque){
    using namespace On12WaterCompat;switch(reinterpret_cast<std::uintptr_t>(opaque)){
    case 0:return status.load(std::memory_order_acquire);case 1:return pairs.load(std::memory_order_relaxed);
    case 2:return zeros.load(std::memory_order_relaxed);case 3:return cameraLow.load(std::memory_order_relaxed);
    case 4:return cameraHigh.load(std::memory_order_relaxed);case 5:return cameraChanges.load(std::memory_order_relaxed);
    case 6:return sequence.load(std::memory_order_acquire);case 7:return conversions.load(std::memory_order_relaxed);
    case 8:return gateSteps.load(std::memory_order_relaxed);
    default:return 0xffffffffu;}
}
