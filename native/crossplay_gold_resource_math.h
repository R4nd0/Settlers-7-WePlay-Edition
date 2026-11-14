#pragma once
// Opt-in resource geometry candidate; the caller retains executable and fresh-world binding.
// Original allocation, CRC calculation and desync checks remain active.
#include "crossplay_building_point_math.h"
#include "crossplay_load_progress_codec.h"
#include <cmath>
namespace GoldResourceMath {
using namespace CrossplayNativeCodec;
inline uintptr_t original=0x7436a0,main_address=0x10f835c;inline DWORD owner=0;inline bool (*ready)()=nullptr;
inline uint32_t corrected=0,excluded=0;inline bool installed=false;
inline constexpr uintptr_t tables[]={15988644,15988988,15989324,15989660,15990020,15993116};
inline bool Table(uintptr_t p){for(auto value:tables)if(value==p)return true;return false;}
inline bool Writable(void* p,size_t size) {
    MEMORY_BASIC_INFORMATION info{};if(!p || !size || VirtualQuery(p,&info,sizeof(info))!=sizeof(info))return false;
    const uintptr_t at=reinterpret_cast<uintptr_t>(p),base=reinterpret_cast<uintptr_t>(info.BaseAddress);
    return info.State==MEM_COMMIT && !(info.Protect&(PAGE_GUARD|PAGE_NOACCESS))
        && (info.Protect&(PAGE_READWRITE|PAGE_WRITECOPY|PAGE_EXECUTE_READWRITE|PAGE_EXECUTE_WRITECOPY))
        && at>=base && size<=info.RegionSize && at-base<=info.RegionSize-size;
}
inline bool Finite(BuildingPointMath::Point p){return std::isfinite(p.x)&&std::isfinite(p.y);}
struct Vector {uintptr_t begin,end,capacity;};
inline bool VectorPair(const Vector& out,const uintptr_t* input,uint32_t& count) {
    if(out.end<out.begin || out.capacity<out.end || (out.end-out.begin)%24 || input[1]<input[0] || (input[1]-input[0])%24)return false;
    count=static_cast<uint32_t>((input[1]-input[0])/24);
    return count<=4096 && out.end-out.begin==count*24 && (!count || (Detail::Readable(reinterpret_cast<void*>(input[0]),count*24) && Writable(reinterpret_cast<void*>(out.begin),count*24)));
}
inline void __stdcall Recompute(void* object) {
    const DWORD error=GetLastError();
    {LoadProgressDetail::FpScope fp;
    if(owner==GetCurrentThreadId() && ready && ready()) {
        using namespace BuildingPointMath;
        auto* bytes=static_cast<unsigned char*>(object);bool valid=Detail::Readable(object,0x64)&&Writable(object,0x64)&&Table(*reinterpret_cast<uintptr_t*>(object));
        uintptr_t props=0;Vector output{};uintptr_t input[2]{};uint32_t count=0;Point position{};float angle=0;
        if(valid) {
            const auto main=*reinterpret_cast<const uintptr_t*>(main_address);valid=Detail::Readable(reinterpret_cast<void*>(main),0x1a0);
            if(valid) {
                const auto manager=*reinterpret_cast<const uintptr_t*>(main+0x19c);valid=Detail::Readable(reinterpret_cast<void*>(manager),8);
                if(valid) {
                    const auto table=*reinterpret_cast<const uintptr_t*>(manager+4),index=*reinterpret_cast<const uint32_t*>(bytes+4);
                    valid=index<65536 && Detail::Readable(reinterpret_cast<void*>(table+index*4),4);
                    if(valid){props=*reinterpret_cast<const uintptr_t*>(table+index*4);valid=Detail::Readable(reinterpret_cast<void*>(props),0x68);}
                }
            }
        }
        if(valid) {
            memcpy(&position,bytes+0x20,8);memcpy(&angle,bytes+0x28,4);valid=Finite(position)&&std::isfinite(angle)&&std::fabs(angle)<=1000000.0f;
            memcpy(&output,bytes+0x58,12);memcpy(input,reinterpret_cast<void*>(props+0x60),8);
            valid=valid&&VectorPair(output,input,count);
            for(uint32_t j=0;valid&&j<count;++j){Point pair[2]{};memcpy(pair,reinterpret_cast<void*>(input[0]+j*24),16);valid=Finite(pair[0])&&Finite(pair[1]);}
        }
        if(valid) {
            const auto rotation=RotateDegrees(angle);
            for(uint32_t j=0;j<count;++j) {
                Point pair[2]{};memcpy(pair,reinterpret_cast<void*>(input[0]+j*24),16);
                pair[0]=Transform(pair[0],position,rotation);pair[1]=Transform(pair[1],position,rotation);
                memcpy(reinterpret_cast<void*>(output.begin+j*24),pair,16);
            }
            ++corrected;
        } else ++excluded;
    }}
    SetLastError(error);
}
__declspec(naked) inline void Bridge() {
    __asm {
        push ebp
        mov ebp,esp
        sub esp,4
        mov dword ptr [ebp-4],ecx
        call dword ptr [original]
        pushfd
        pushad
        push dword ptr [ebp-4]
        call Recompute
        popad
        popfd
        mov esp,ebp
        pop ebp
        ret
    }
}
// Full table, method, RTTI and numeric-helper pins are generated from the exact
// analysis image; the enclosing diagnostic manifest retains its executable pin.
inline bool Guard();
inline bool Install(DWORD thread,bool (*predicate)()) {
    if(installed || original!=0x7436a0 || main_address!=0x10f835c || thread!=GetCurrentThreadId() || !predicate || !Guard())return false;
    DWORD protection[6]{};unsigned changed=0;bool restored=true;
    for(unsigned i=0;i<6;++i) {
        auto* slot=reinterpret_cast<uintptr_t*>(tables[i]+39*4);
        if(!VirtualProtect(slot,4,PAGE_READWRITE,&protection[i]))break;
        *slot=reinterpret_cast<uintptr_t>(&Bridge);MemoryBarrier();DWORD ignored=0;
        ++changed;if(!VirtualProtect(slot,4,protection[i],&ignored)){restored=false;break;}
    }
    if(changed!=6 || !restored) {
        for(unsigned i=0;i<changed;++i){auto* slot=reinterpret_cast<uintptr_t*>(tables[i]+39*4);DWORD temporary=0,ignored=0;if(VirtualProtect(slot,4,PAGE_READWRITE,&temporary)){*slot=original;MemoryBarrier();VirtualProtect(slot,4,protection[i],&ignored);}}
        return false;
    }
    owner=thread;ready=predicate;installed=true;return true;
}
}

namespace GoldResourceMath {
inline bool Guard() {
    uint64_t hash=14695981039346656037ull;
    struct Region {uintptr_t address;size_t length;};
    const Region regions[]={{0x7436a0,412},{0x4e9910,61},{0xf3f7a0,164},{0x72acb0,8},{0xa302e0,8},{0x72ac60,8},{0x72ac50,8},{0x742b60,8},{0x742b40,8},{0x62a850,8},{0x6639a0,8},{0x663990,8},{0x62a870,8},{0x62a860,8},{0x62a890,8},{0x62a880,8},{0x62a8b0,8},{0x62a8a0,8},{0x62a8c0,8},{0x62a8d0,8},{0x6639c0,8},{0x6639b0,8},{0xa30300,8},{0x72b5a0,8},{0x72acc0,8},{0x72acd0,8},{0x743640,8},{0x743660,8},{0x743680,8},{0x72ac90,8},{0x72aca0,8},{0x656f50,8},{0x72adb0,8},{0x72ae50,8},{0x72bbf0,8},{0x72b150,8},{0x742fe0,8},{0x7430a0,8},{0x7431a0,8},{0x72b180,8},{0x72b3e0,8},{0x72b580,8},{0x7436a0,8},{0xf7e828,20},{0x10bbf04,19},{0xf3f8f8,164},{0x72da30,8},{0x72da50,8},{0x72da40,8},{0x72da70,8},{0x72da60,8},{0x72df90,8},{0x62a8e0,8},{0x62a960,8},{0x72da80,8},{0x72dba0,8},{0x742ed0,8},{0x742f60,8},{0x72dec0,8},{0x72da20,8},{0x72df30,8},{0xf7ead0,20},{0x10bc2e4,18},{0xf3fa48,164},{0x72fbe0,8},{0x730070,8},{0x72fbf0,8},{0x72fc60,8},{0x72fe40,8},{0x72feb0,8},{0x72ffb0,8},{0xf7ed3c,20},{0x10bc6d0,18},{0xf3fb98,164},{0x731df0,8},{0x732250,8},{0x731e00,8},{0x731e90,8},{0x7320a0,8},{0x732180,8},{0x7321e0,8},{0xf7efa8,20},{0x10bcac8,18},{0xf3fd00,164},{0x734000,8},{0x734450,8},{0x734020,8},{0x7340a0,8},{0x7342f0,8},{0x734360,8},{0x7343c0,8},{0xf7f260,20},{0x10bcf60,19},{0xf40918,164},{0xbae9fe,8},{0x742a00,8},{0x742ac0,8},{0xf7f7bc,20},{0x10bbf20,22}};
    for(const auto& r:regions){if(!Detail::Readable(reinterpret_cast<void*>(r.address),r.length))return false;for(size_t i=0;i<r.length;++i)hash=(hash^reinterpret_cast<const unsigned char*>(r.address)[i])*1099511628211ull;}
    return hash==0x8f1e635d3f83cff8ull;
}
}
